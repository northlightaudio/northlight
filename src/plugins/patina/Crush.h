#pragma once

#include "plugins/Biquad.h"
#include "plugins/dsp/Random.h"
#include "plugins/patina/Parts.h"

#include <array>
#include <cstddef>

// Patina's Crush stage: an early sampler's bits and rate (a Flavour: filtered
// converters, none, mu-law companding, or an unsteady clock). One converter
// clock serves both sides. Real-time safe. JUCE-free.

namespace northlight::plugins::patina
{

class Crush
{
public:
    // The smoothed controls Patina sets each sub-block: how far the bits and
    // the rate are crushed (Age and Breath applied, 0 = clean .. 1 = the most).
    float bitsAmount = 0, rateAmount = 0;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    // Per sub-block, for a Flavour.
    void controls(int flavour, Accumulator& acc) noexcept;
    // One channel's sample; the left (ch 0) comes first, as it ticks the clock.
    float sample(size_t ch, float x) noexcept;

private:
    double fs_ = 48000.0;
    int flavour_ = 0;
    std::array<std::array<Biquad, 4>, 2> pre_, post_;
    std::array<float, 2> held_{};
    float phase_ = 1, threshold_ = 1, designed_ = -1, bitsNow_ = 16, rateNow_ = 48000, levels_ = 32768;
    bool filtered_ = false, tick_ = false;
    dsp::XorShift32 rng_{0xc0ffeeu};
};

} // namespace northlight::plugins::patina
