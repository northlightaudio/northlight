#pragma once

// A topology-preserving (trapezoidal) state-variable filter, 12 dB/octave, after
// Zavalishin and Simper: g = tan(pi fc / fs), k = 1/Q. It stays stable up to the
// top of the band and under fast modulation. One channel; the caller owns g and k,
// so a voice can share them across its channels and stages.

namespace northlight::plugins::dsp
{

struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;

    // Low, band and high for one input.
    void run(float x, float g, float k, float& low, float& band, float& high) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + g * a1 * v3;
        const float v2 = ic2 + g * v1;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = x - k * v1 - v2;
    }
    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

} // namespace northlight::plugins::dsp
