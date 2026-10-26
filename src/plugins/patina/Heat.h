#pragma once

#include "plugins/Biquad.h"
#include "plugins/patina/Parts.h"

#include <array>
#include <cstddef>

// Patina's Heat stage: a circuit's saturation. A character's emphasis, the
// driven curve with first-order antiderivative anti-aliasing, a DC blocker,
// the de-emphasis, tape's head bump or the fuzz's top end, then a tone tilt
// about 900 Hz. Real-time safe except prepare(). JUCE-free.

namespace northlight::plugins::patina
{

class Heat
{
public:
    // The smoothed controls Patina sets each sub-block: the amount (Age and
    // Breath applied, 0..1) and the tone (-1 dark .. +1 bright).
    float amount = 0, tone = 0;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    // Per sub-block, for a Character; `first` is the first sub-block since prepare().
    void controls(int character, bool first) noexcept;
    // One channel's sample, `t` (0..1] of the way through the sub-block.
    float sample(size_t ch, float x, float t, Accumulator& acc) noexcept;

private:
    // The curve's integral at previous_, and what it was worked out with.
    struct Integral
    {
        double value = 0.0;
        int character = -1; // -1: not worked out
        double deadZone = 0.0;
    };

    double fs_ = 48000.0;
    int character_ = 0;
    std::array<Biquad, 2> pre_, post_, bump_, tiltLow_, tiltHigh_;
    std::array<double, 2> previous_{};
    std::array<Integral, 2> previousF_{};
    std::array<float, 2> dc_{}, dcIn_{};
    float driveFrom_ = 0, driveTo_ = 0, compFrom_ = 1, compTo_ = 1;
    double deadZone_ = 0.02;
    int designed_ = -1;
    float shaped_ = -1, toneShaped_ = -1;
};

} // namespace northlight::plugins::patina
