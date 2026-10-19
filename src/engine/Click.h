#pragma once

#include "core/Model.h"

#include <cstdint>

// The metronome: a short decaying sine burst on every beat, pitched up on the first
// beat of each bar. Real-time safe and allocation-free; the only state is the burst
// in progress, so it can run across count-in and playback without a reset.

namespace aural::engine
{

class ClickGenerator
{
public:
    // Adds clicks for the timeline span [start, start + numSamples) into left and
    // right. `start` may be negative (count-in before the start position); beats are
    // placed on multiples of samplesPerBeat from zero.
    void render(core::SamplePosition start,
                int numSamples,
                double sampleRate,
                double samplesPerBeat,
                int beatsPerBar,
                float gain,
                float* left,
                float* right) noexcept;

    // Drops any burst in progress (on stop).
    void reset() noexcept { remaining_ = 0; }

    static constexpr double accentHz = 1500.0;
    static constexpr double beatHz = 1000.0;
    static constexpr double burstSeconds = 0.03;

private:
    void trigger(bool accent, double sampleRate) noexcept;

    int remaining_ = 0;
    int length_ = 0;
    double phase_ = 0.0;
    double phaseStep_ = 0.0;
};

} // namespace aural::engine
