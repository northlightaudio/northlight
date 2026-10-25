#pragma once

// Gain computers for the dynamics processors. Real-time safe.

namespace northlight::plugins::dsp
{

// The soft-knee curve: the gain reduction (dB, >= 0) for a level `overDb`
// above the threshold, with `slope` = 1 - 1/ratio and a knee `kneeDb` wide
// (> 0) centred on the threshold. Nothing below the knee, the straight slope
// above it, and inside it the quadratic that meets both.
inline float softKneeReductionDb(float overDb, float slope, float kneeDb) noexcept
{
    if (2.0f * overDb <= -kneeDb)
        return 0.0f;
    if (2.0f * overDb >= kneeDb)
        return overDb * slope;
    const float x = overDb + 0.5f * kneeDb;
    return slope * x * x / (2.0f * kneeDb);
}

} // namespace northlight::plugins::dsp
