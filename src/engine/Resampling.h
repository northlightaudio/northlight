#pragma once

#include <cmath>
#include <cstdint>

// Sample-rate conversion by 4-point cubic Hermite interpolation, evaluated at the
// exact source position of each output sample: output sample t maps to source
// position t * ratio, so there is no state, no lookback delay and no drift, and a
// read can start anywhere. Quality is adequate for monitoring and playback of
// imported media; a windowed-sinc converter for bounces arrives in Phase 9.

namespace northlight::engine
{

inline float hermite(float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c = 0.5f * (x1 - xm1);
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + 0.5f * (x2 - x0);
    const float b = w + a;
    return ((((a * t) - b) * t + c) * t) + x0;
}

// Produces numOutput samples starting at output position outputStart from a source
// array of inputLength samples that begins at source position inputStart. `ratio`
// is source samples per output sample. Source positions outside the array read as
// zero, so callers pad by two samples each side for seamless chunks.
inline void resampleHermite(const float* input,
                            std::int64_t inputStart,
                            std::int64_t inputLength,
                            double ratio,
                            std::int64_t outputStart,
                            int numOutput,
                            float* output) noexcept
{
    const auto at = [&](std::int64_t sourceIndex) -> float
    {
        const auto i = sourceIndex - inputStart;
        return i >= 0 && i < inputLength ? input[i] : 0.0f;
    };
    for (int i = 0; i < numOutput; ++i)
    {
        const double position = static_cast<double>(outputStart + i) * ratio;
        const auto n = static_cast<std::int64_t>(std::floor(position));
        const auto t = static_cast<float>(position - static_cast<double>(n));
        output[i] = hermite(at(n - 1), at(n), at(n + 1), at(n + 2), t);
    }
}

// Source samples needed to produce [outputStart, outputStart + numOutput): returns
// the first source index and the count, including the interpolation margins.
inline void sourceRangeFor(double ratio,
                           std::int64_t outputStart,
                           int numOutput,
                           std::int64_t& sourceStart,
                           int& sourceCount) noexcept
{
    const auto first = static_cast<std::int64_t>(std::floor(static_cast<double>(outputStart) * ratio)) - 1;
    const auto last =
        static_cast<std::int64_t>(std::floor(static_cast<double>(outputStart + numOutput) * ratio)) + 2;
    sourceStart = first;
    sourceCount = static_cast<int>(last - first + 1);
}

} // namespace northlight::engine
