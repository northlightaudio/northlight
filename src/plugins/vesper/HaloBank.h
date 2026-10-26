#pragma once

#include "plugins/VocalListener.h"

#include <array>
#include <cstdint>
#include <vector>

namespace northlight::plugins::vesper
{

// Halo: sympathetic strings. Six feedback combs, each tuned to a note the singer
// held, ring with what goes into the reverb, so
// the tail sounds the pitches just sung and their harmonics, in tune with the
// singer however the singer is tuned. A note is held when its pitch runs on for
// 120 ms without a jump (100 cents in a frame) or a glide of 150 cents away from its
// centre (its average pitch), so a vibrato counts; the string follows the centre
// while the note is sung. A note no string holds goes to a free or
// the quietest one, faded out over 30 ms first so a retune never clicks. A one-pole
// low-pass in each loop makes them ring like strings, not buzz; its delay is taken
// off the loop so the pitch stays true. Mono in and out. Real-time safe after
// prepare(). JUCE-free.
class HaloBank
{
public:
    static constexpr int voiceCount = 6;
    static constexpr float jumpCents = 100.0f, glideCents = 150.0f, sameNoteCents = 50.0f;
    static constexpr double steadySeconds = 0.12, centreSeconds = 0.3, retuneSeconds = 0.03;
    static constexpr float dampingHz = 4000.0f;

    void prepare(double sampleRate);
    void clear() noexcept;

    // Once a block: how long a string rings (RT60).
    void setDecay(float seconds) noexcept;

    // A Listener frame: a note held that no string holds is given one, and the
    // string holding the note being sung follows its centre.
    void hear(const vocal::Hearing& h) noexcept;

    // One sample in; the strings' sum out.
    float process(float x) noexcept;

    // Whether any string still sounds (or is about to).
    bool ringing() const noexcept;

    // The notes held.
    std::array<float, voiceCount> notes() const noexcept;

    // Once a block, after setDecay: each string's loop gain for its note.
    void tune() noexcept;

    void flushDenormals() noexcept;

private:
    struct Voice
    {
        std::vector<float> buffer;
        float hz = 0.0f, pendingHz = 0.0f;
        float delay = 1.0f, feedback = 0.0f, drive = 0.0f, gain = 0.0f, low = 0.0f, energy = 0.0f;
        std::int64_t lastHeard = 0;
    };

    static float cents(float a, float b) noexcept;
    void retune(Voice& v) noexcept;
    void setLoop(Voice& v) noexcept;

    std::array<Voice, voiceCount> voices_;
    double sampleRate_ = 48000.0;
    int mask_ = 0, write_ = 0;
    float lowStep_ = 0.0f, loopLag_ = 0.0f, fadeStep_ = 0.0f, envelopeStep_ = 0.0f;
    float decaySeconds_ = 3.0f;
    float candidateHz_ = 0.0f, centre_ = 0.0f, last_ = 0.0f, centreStep_ = 0.0f;
    int steady_ = 0, steadyFrames_ = 48, current_ = -1;
    std::int64_t clock_ = 0;
};

} // namespace northlight::plugins::vesper
