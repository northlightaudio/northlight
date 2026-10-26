#pragma once

#include "plugins/Biquad.h"
#include "plugins/dsp/DelayLine.h"
#include "plugins/dsp/Random.h"
#include "plugins/patina/Parts.h"

#include <array>
#include <cstddef>

// Patina's Wear stage: the medium's age. Lost highs (a low-pass with a low
// bump and a little low cut), dropouts (random dips, the highs lost deeper)
// and azimuth skew (the channels drifting apart in time, tone and level). The
// skew lines always run. Real-time safe except prepare(). JUCE-free.

namespace northlight::plugins::patina
{

class Wear
{
public:
    // The smoothed controls Patina sets each sub-block (Age and Breath
    // applied, 0..1).
    float loss = 0, dropouts = 0, skew = 0;

    void prepare(double sampleRate);
    void reset() noexcept;
    // Per sub-block of `n` samples (`seconds` long).
    void controls(int n, float seconds, bool first, Accumulator& acc) noexcept;
    // Every sample, on or off.
    void push(float l, float r) noexcept
    {
        skewLine_[0].push(l);
        skewLine_[1].push(r);
    }
    // One channel's worn sample, `t` (0..1] of the way through the sub-block.
    float sample(size_t ch, float t) noexcept
    {
        // Under a third of a millisecond: linear interpolation is enough, and
        // the side it dulls is the lagging one.
        const float x = skewLine_[ch].linear(skewFrom_[ch] + (skewTo_[ch] - skewFrom_[ch]) * t);
        const float gain = dropFrom_ + (dropTo_ - dropFrom_) * t;
        return filter(ch, x) * gain * skewGain_[ch];
    }

private:
    float filter(size_t ch, float x) noexcept
    {
        return bump_[ch].process(high_[ch].process(low_[ch].process(x)));
    }

    double fs_ = 48000.0;
    std::array<dsp::DelayLine, 2> skewLine_;
    std::array<Biquad, 2> low_, high_, bump_;
    std::array<float, 2> skewFrom_{}, skewTo_{}, skewGain_{1.0f, 1.0f}, designed_{-1.0f, -1.0f};
    Contour skewShape_, skewLevel_;
    dsp::XorShift32 rng_{0xd3a7u};
    int dropRemaining_ = 0;
    float dropDip_ = 0, dropDepthDb_ = 0, dropFrom_ = 1, dropTo_ = 1;
};

} // namespace northlight::plugins::patina
