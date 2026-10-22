#pragma once

#include <cstdint>
#include <vector>

// Finds clicks in audio after the fact (the output tap runs it over what the callback
// handed the driver). Two kinds:
//
//   jump  a sample the recent signal does not predict. Each channel is cut into
//         frames; a linear predictor (order 16, fitted to the frame) predicts every
//         sample from the 16 before it, and a sample whose prediction error is far
//         above the frame's typical error is a jump. Music, even drums, is well
//         predicted at this order; a discontinuity is not. A waveform with hard
//         edges (a naive sawtooth or square) has one every period, so an error
//         that repeats the shape of the error one period earlier (any period from
//         minPeriod samples to maxPeriodSeconds) is part of the sound and not
//         reported. Repeats at a multiple of the device block size are never
//         excused: they are what a block-boundary bug looks like. An instant
//         attack is still a discontinuity, so it reports.
//   gap   a short run of exact digital silence on every channel inside a signal:
//         the signal was there, went to zero for at most maxGapSeconds, and came
//         back. Longer silences (the transport stopped) are not gaps.
//
// A cut to exact silence, and the first samples after any exact zero (the stream
// starting, a reset, the end of a gap), are not jumps; the gap check covers them. Detections within
// holdSeconds of each other merge into one. JUCE-free and not real-time (it allocates on construction and
// sorts).

namespace northlight::engine
{

struct Glitch
{
    enum class Kind
    {
        jump,
        gap,
    };
    Kind kind = Kind::jump;
    std::int64_t frame = 0; // first frame of the glitch, counted from the first frame processed
    int channel = -1;       // the channel, or -1 when it was on more than one (always for a gap)
    double size = 0.0;      // jump: prediction error (linear); gap: length in frames
};

class GlitchDetector
{
public:
    static constexpr int order = 16;
    static constexpr int frameSize = 1024;
    static constexpr double errorRatio = 12.0;        // times the frame's typical error
    static constexpr double errorFloor = 0.03;        // and at least this (linear, about -30 dBFS)
    static constexpr double holdSeconds = 0.005;      // detections closer than this merge
    static constexpr double maxGapSeconds = 0.25;     // longer silences are not gaps
    static constexpr int minGapFrames = 4;            // shorter runs of zeros are not gaps
    static constexpr double gapLevelFloor = 1.0e-3;   // the signal before a gap must be at least this
    static constexpr double maxPeriodSeconds = 0.025; // longest waveform period checked (40 Hz)
    static constexpr int minPeriod = 16;              // shortest, in samples
    static constexpr int matchWindow = 8;      // samples of error around it compared with the period before
    static constexpr double periodMatch = 0.7; // correlation that makes it a repeat

    GlitchDetector(int numChannels, double sampleRate);

    // Interleaved frames; glitches found are appended to `out` in frame order for
    // this call. Jumps are found a frame (frameSize) after the audio arrives.
    void process(const float* interleaved, int numFrames, std::vector<Glitch>& out);
    // Forgets the signal so far (a break in the audio); frame numbers carry on.
    void reset();
    // The device block size, so errors repeating at that spacing are not excused
    // as a waveform's period (0 when unknown).
    void setBlockSize(int frames) noexcept { blockSize_ = frames; }

    std::int64_t framesProcessed() const noexcept { return processed_; }

private:
    struct Channel
    {
        std::vector<float> history; // the order samples before the frame
        std::vector<float> frame;
        std::vector<double> residual; // signed errors: the past (pastLength_) then this frame
    };
    void analyseFrames(std::vector<Glitch>& found);
    bool repeatsEarlier(const std::vector<double>& residual, int index) const;

    int numChannels_;
    double sampleRate_;
    std::int64_t hold_;
    std::int64_t maxGap_;
    int maxLag_;
    int pastLength_;
    int blockSize_ = 0;
    std::vector<Channel> channels_;
    int filled_ = 0;              // frames in the current analysis frame
    std::int64_t frameStart_ = 0; // absolute frame of the analysis frame's first sample
    std::int64_t processed_ = 0;

    // Gap state.
    double level_ = 0.0; // smoothed absolute level of the loudest channel
    double levelCoefficient_ = 0.0;
    double levelAtRunStart_ = 0.0;
    std::int64_t zeroRun_ = 0;

    std::vector<double> scratch_, window_, coefficients_, residual_;
};

} // namespace northlight::engine
