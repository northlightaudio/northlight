#pragma once

#include "plugins/dsp/DelayLine.h"
#include "plugins/dsp/Random.h"
#include "plugins/patina/Parts.h"

#include <array>
#include <cstddef>
#include <utility>

// Patina's Drift stage: a transport's wow and flutter (a Machine's), as a
// delay line whose length wobbles, apart on the two sides with Stereo. The
// line always runs, so switching the stage on is seamless. Real-time safe
// except prepare(). JUCE-free.

namespace northlight::plugins::patina
{

class Drift
{
public:
    // The smoothed controls Patina sets each sub-block: wow and flutter (Age
    // and Breath applied, 0..1), the rate (0..1, 0.5 = as built) and the stereo
    // spread (0..1).
    float wow = 0, flutter = 0, rate = 0.5f, stereo = 0;

    void prepare(double sampleRate);
    void reset() noexcept;
    // Per sub-block of `n` samples, for a Machine: where the delay glides to.
    void controls(int machine, int n, float slowSmooth, bool first, Accumulator& acc) noexcept;
    // Every sample, on or off.
    void push(float l, float r) noexcept
    {
        line_[0].push(l);
        line_[1].push(r);
    }
    // One channel's delayed sample, `t` (0..1] of the way through the sub-block.
    float read(size_t ch, float t) const noexcept
    {
        return line_[ch].hermite(from_[ch] + (to_[ch] - from_[ch]) * t);
    }
    // Once a block: keeps the scrape's state out of denormals.
    void flush() noexcept;

private:
    struct Wobble
    {
        float wowPhase = 0.0f, flutterPhase = 0.0f;
        Contour wowRate, wowShape, wowOffset, flutterShape;
        dsp::XorShift32 rng;
    };

    // One channel's wow and flutter shapes (each about -1..1) for the machine.
    std::pair<float, float> shapes(size_t ch, int machine, float seconds, float rateScale) noexcept;

    double fs_ = 48000.0;
    std::array<dsp::DelayLine, 2> line_;
    std::array<Wobble, 2> wobble_;
    std::array<float, 2> from_{}, to_{}, scrape_{};
    float wowDepth_ = 0, flutterDepth_ = 0;
};

} // namespace northlight::plugins::patina
