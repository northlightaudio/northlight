#pragma once

#include "plugins/Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// The reverb core Bloom and Vesper share: a feedback delay network of eight
// modulated lines, mixed by a Householder matrix, with an allpass in each line for
// density and four input diffusers a side. Damping and low cut work inside the loop
// as shelves whose depth follows each line's gain, so the highs and lows decay a
// set number of times faster than the decay at any decay time. Freeze takes the
// loop's losses away. Shimmer pitch-shifts part of each pass with two grain heads
// per line. A governor watches the network's energy and holds it under a ceiling,
// so no setting runs away.
//
// What is around the network (pre-delay, swell, tone, width, ducking, mix) belongs
// to the effect. The effect also smooths the per-sample controls and passes them in
// a Frame, so each effect keeps its own glide times. JUCE-free.

namespace northlight::plugins::fdn
{

inline constexpr int lineCount = 8;

// What makes one reverb's network its own: line lengths, allpasses, diffusers and
// how far the lines can stretch. Fixed for the life of a processor.
struct Tuning
{
    std::array<float, lineCount> lineMs{};    // the lines' lengths at scale 1
    std::array<float, lineCount> allpassMs{}; // the allpass in each line (fixed: changing it would click)
    std::array<float, 4> diffuserLeftMs{}, diffuserRightMs{};
    float maxScale = 1.0f;             // the longest the lines are stretched (a scale of lineMs)
    float maxModSeconds = 0.0f;        // the deepest modulation sweep, either side
    float shimmerWindowSeconds = 0.0f; // the shimmer's grain; 0 leaves no room for shimmer
    float bandDecayRatio =
        4.0f;               // highs past the damping and lows under the low cut decay this many times faster
    float earlyGain = 0.6f; // the diffused input in the tail before the first pass returns
    float energyCeiling = 0.5f; // the governor's ceiling: the RMS of one line
};

// The mean line length (at scale 1) and the mean allpass length, in seconds.
float meanLineSeconds(const Tuning& tuning) noexcept;
float meanAllpassSeconds(const Tuning& tuning) noexcept;
// The tail's RT60 at a frequency, from the loop's damping and low cut: the decay
// where the filters leave the signal alone, shorter where they act.
float decayAt(const Tuning& tuning,
              float hz,
              float decaySeconds,
              float scale,
              float dampingHz,
              float lowCutHz) noexcept;

// The one-pole coefficient for a corner frequency.
float onePole(float hz, double sampleRate) noexcept;

// The smallest power of two at least n.
int powerOfTwoAtLeast(double n) noexcept;

// A linear read `delay` samples behind `write` in a power-of-two buffer.
inline float readLinear(const float* buffer, int mask, int write, float delay) noexcept
{
    const float position = static_cast<float>(write) - delay;
    const float floor = std::floor(position);
    const int i = static_cast<int>(floor);
    const float t = position - floor;
    return buffer[i & mask] + t * (buffer[(i + 1) & mask] - buffer[i & mask]);
}

// A cubic Hermite read `delay` samples behind `write` (delay >= 3).
inline float readHermite(const float* buffer, int mask, int write, float delay) noexcept
{
    const float position = static_cast<float>(write) - delay;
    const float floor = std::floor(position);
    const int i = static_cast<int>(floor);
    const float t = position - floor;
    const float x0 = buffer[(i - 1) & mask], x1 = buffer[i & mask];
    const float x2 = buffer[(i + 1) & mask], x3 = buffer[(i + 2) & mask];
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
}

// A Schroeder allpass with a fixed length.
struct Allpass
{
    std::vector<float> buffer;
    int pos = 0;

    float process(float x, float g) noexcept
    {
        const float delayed = buffer[static_cast<size_t>(pos)];
        const float w = x + g * delayed;
        buffer[static_cast<size_t>(pos)] = w;
        if (++pos == static_cast<int>(buffer.size()))
            pos = 0;
        return delayed - g * w;
    }
    void prepare(float seconds, double sampleRate)
    {
        buffer.assign(static_cast<size_t>(std::max(1.0, seconds * sampleRate)), 0.0f);
        pos = 0;
    }
    void clear() noexcept { std::fill(buffer.begin(), buffer.end(), 0.0f); }
};

// Set once a block.
struct BlockSettings
{
    float decaySeconds = 2.0f; // RT60
    float endScale = 1.0f;     // the line scale the block ends near (the decay's gains are set for it)
    float modSamples = 0.0f;   // the modulation's sweep now (part of each line's length)
    float dampingHz = 6000.0f;
    float lowCutHz = 80.0f;
    float diffusion = 0.8f; // 0..1: the input diffusers and each line's allpass
    float lfoHz = 0.5f;     // the modulation's rate (each line a little off it)
    // Each line's shimmer pitch ratio (1 leaves it alone).
    std::array<float, lineCount> pitchRatios{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
};

// Set every sample: the effect's smoothed controls.
struct Frame
{
    float scale = 1.0f;   // the lines' scale of their lengths
    float sweep = 0.0f;   // the modulation's sweep in samples
    float freeze = 0.0f;  // 0..1: how far the loop's losses are taken away
    float shimmer = 0.0f; // 0..1: the shimmer's amount (it also guards what the lines hold)
    float keep = 1.0f;    // the unshifted sound's share of each pass
    float shift = 0.0f;   // the shifted sound's share
    bool shimmering = false;
};

class FeedbackNetwork
{
public:
    // Allocates the lines for the tuning at this rate. Not real-time.
    void prepare(const Tuning& tuning, double sampleRate);
    // Silences every line, allpass and filter.
    void clear() noexcept;

    void beginBlock(const BlockSettings& settings) noexcept;
    // The input diffusers, a chain a side.
    void diffuse(float& left, float& right) noexcept;
    // One sample: the diffused input (already scaled) in, the tail out.
    void
    process(const Frame& frame, float sendLeft, float sendRight, float& tailLeft, float& tailRight) noexcept;

    // The network's energy: the smoothed mean square of a line.
    float energy() const noexcept { return energy_; }
    // A non-finite value would ring for ever; the effect clears the network when it sees one.
    bool diverged() const noexcept { return !std::isfinite(energy_) || !std::isfinite(governor_); }

private:
    struct Line
    {
        std::vector<float> buffer;
        Allpass allpass;
        float high = 0.0f, low = 0.0f; // the damping's and the low cut's one-pole memories
        float lfoCos = 1.0f, lfoSin = 0.0f;
        float grain = 0.0f; // the shimmer's grain phase, 0..1
        Biquad guard;       // the shimmer's anti-alias low-pass on what is written
        Biquad shiftLowCut; // the shifted path's high-pass
    };

    Tuning tuning_;
    double sampleRate_ = 48000.0;
    std::array<Line, lineCount> lines_;
    int lineMask_ = 0, lineWrite_ = 0;
    std::array<std::array<Allpass, 4>, 2> diffusers_;
    float energy_ = 0.0f, governor_ = 1.0f;
    float energyStep_ = 0.0f, governorDown_ = 0.0f, governorUp_ = 0.0f;
    float windowSamples_ = 1.0f;
    // Set by beginBlock.
    float diffuserGain_ = 0.0f, loopAllpassGain_ = 0.0f, dampStep_ = 0.0f, lowStep_ = 0.0f;
    std::array<float, lineCount> baseSamples_{}, lineGain_{}, bandGain_{}, grainStep_{}, lfoRotCos_{},
        lfoRotSin_{};
    std::array<bool, lineCount> up_{};
    std::array<float, lineCount> guardRatios_{}; // the ratios the guards were designed for (0: not yet)
};

} // namespace northlight::plugins::fdn
