#pragma once

#include "plugins/Biquad.h"
#include "plugins/dsp/DelayLine.h"
#include "plugins/patina/Parts.h"

#include <array>
#include <cstddef>

// Patina's Chamber stage: a short space behind the sound, one of three (a
// Space): a small room (early reflections, then four lines mixed by a
// Hadamard matrix), two springs (dispersive allpass chains in feedback loops)
// or Dattorro's plate. The stage makes the wet signal; Patina mixes it in.
// Real-time safe except prepare(). JUCE-free.

namespace northlight::plugins::patina
{

class Chamber
{
public:
    // The smoothed controls Patina sets each sub-block: the wet level (Age and
    // Breath applied), the decay (0..1) and the tone (-1 dark .. +1 bright).
    float mix = 0, tone = 0, decay = 0;

    void prepare(double sampleRate);
    void reset() noexcept;
    // Per sub-block, for a Space.
    void controls(int space) noexcept;
    // The space's level against the dry signal.
    float wetGain() const noexcept;
    // The wet signal for a stereo sample.
    void sample(float l, float r, float& wl, float& wr) noexcept;

private:
    // A small, boxy room.
    struct Room
    {
        std::array<dsp::DelayLine, 2> input;
        std::array<std::array<int, 6>, 2> tap{};
        std::array<dsp::DelayLine, 4> line;
        std::array<int, 4> length{};
        std::array<float, 4> gain{}, damp{};
        float dampK = 0.5f;
        std::array<Biquad, 2> box;

        void prepare(double fs);
        void reset() noexcept;
        void sample(float l, float r, float& wl, float& wr) noexcept;
    };

    // Two springs, one a side.
    struct Spring
    {
        static constexpr float coefficient = 0.62f;
        static constexpr size_t stages = 20;
        struct AllpassState
        {
            float x1 = 0.0f, y1 = 0.0f;
        };
        std::array<std::array<AllpassState, stages>, 2> chain{};
        std::array<dsp::DelayLine, 2> line;
        std::array<int, 2> length{};
        std::array<float, 2> gain{};
        std::array<Biquad, 2> low, high;

        void prepare(double fs);
        void reset() noexcept;
        void sample(float l, float r, float& wl, float& wr) noexcept;
    };

    // Dattorro's figure-of-eight plate.
    struct Plate
    {
        // Dattorro's plate, at its design rate.
        static constexpr double designRate = 29761.0;
        std::array<Allpass, 4> diffuser;
        std::array<Allpass, 2> tankAp1, tankAp2;
        std::array<dsp::DelayLine, 2> delay1, delay2;
        std::array<float, 2> damp{};
        std::array<int, 2> delay1Length{1, 1}, delay2Length{1, 1};
        std::array<int, 14> tap{};
        float excursion = 1.0f;
        float bandwidth = 0, decay = 0.5f, dampK = 0.5f, bandK = 0.5f, phase = 0;
        double loopSeconds = 0.7;

        void prepare(double fs);
        void reset() noexcept;
        void sample(float l, float r, float& wl, float& wr, double fs) noexcept;
    };

    double fs_ = 48000.0;
    int space_ = 0;
    Room room_;
    Spring spring_;
    Plate plate_;
    std::array<Biquad, 2> in_, low_, high_;
    float designed_ = -1.0e9f;
};

} // namespace northlight::plugins::patina
