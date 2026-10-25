#pragma once

// PolyBLEP oscillators: a naive shape with a two-sample polynomial correction at
// each jump, which takes most of the aliasing away for almost no cost. Phases are
// 0..1 and dt is the phase step per sample (frequency / sample rate).

namespace northlight::plugins::dsp
{

// The band-limiting correction at a discontinuity at phase 0, for phase t.
inline float polyBlep(double t, double dt) noexcept
{
    if (t < dt)
    {
        const double x = t / dt;
        return static_cast<float>(x + x - x * x - 1.0);
    }
    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt;
        return static_cast<float>(x * x + x + x + 1.0);
    }
    return 0.0f;
}

// A rising saw, -1..1.
inline float blepSaw(double phase, double dt) noexcept
{
    return static_cast<float>(2.0 * phase - 1.0) - polyBlep(phase, dt);
}

// A square, +1 for the first half of the cycle.
inline float blepSquare(double phase, double dt) noexcept
{
    double half = phase + 0.5;
    if (half >= 1.0)
        half -= 1.0;
    return (phase < 0.5 ? 1.0f : -1.0f) + polyBlep(phase, dt) - polyBlep(half, dt);
}

} // namespace northlight::plugins::dsp
