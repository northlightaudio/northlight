#pragma once

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Fractional reads from a delay line on a power-of-two ring (a read wraps with a
// mask), and the interpolation kernels they use.

namespace northlight::plugins::dsp
{

// x0 + t (x1 - x0).
inline float interpolateLinear(float x0, float x1, float t) noexcept
{
    return x0 + t * (x1 - x0);
}

// Four-point cubic Hermite (Catmull-Rom) between x0 and x1, t in 0..1; xm1 lies
// before x0 and x2 after x1.
inline float interpolateHermite(float xm1, float x0, float x1, float x2, float t) noexcept
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

// A linear read `delay` samples behind `write` in a power-of-two buffer.
inline float readLinear(const float* buffer, int mask, int write, float delay) noexcept
{
    const float position = static_cast<float>(write) - delay;
    const float floor = std::floor(position);
    const int i = static_cast<int>(floor);
    const float t = position - floor;
    return interpolateLinear(buffer[i & mask], buffer[(i + 1) & mask], t);
}

// A cubic Hermite read `delay` samples behind `write` (delay >= 3).
inline float readHermite(const float* buffer, int mask, int write, float delay) noexcept
{
    const float position = static_cast<float>(write) - delay;
    const float floor = std::floor(position);
    const int i = static_cast<int>(floor);
    const float t = position - floor;
    return interpolateHermite(
        buffer[(i - 1) & mask], buffer[i & mask], buffer[(i + 1) & mask], buffer[(i + 2) & mask], t);
}

// A delay line that owns its power-of-two ring. at(0) is the newest sample.
struct DelayLine
{
    std::vector<float> data;
    int mask = 0, write = 0;

    // Room for `maxDelay` samples and an interpolator's reach past it.
    void prepare(int maxDelay)
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("DelayLine::prepare");
        int size = 16;
        while (size < maxDelay + 8)
            size <<= 1;
        data.assign(static_cast<size_t>(size), 0.0f);
        mask = size - 1;
        write = 0;
    }
    void clear() noexcept { std::fill(data.begin(), data.end(), 0.0f); }
    void push(float x) noexcept
    {
        data[static_cast<size_t>(write)] = x;
        write = (write + 1) & mask;
    }
    float at(int d) const noexcept { return data[static_cast<size_t>((write - 1 - d) & mask)]; }
    float linear(float d) const noexcept
    {
        const int i = static_cast<int>(d);
        return interpolateLinear(at(i), at(i + 1), d - static_cast<float>(i));
    }
    // Four-point Hermite (d >= 1).
    float hermite(float d) const noexcept
    {
        d = std::max(1.0f, d);
        const int i = static_cast<int>(d);
        return interpolateHermite(at(i - 1), at(i), at(i + 1), at(i + 2), d - static_cast<float>(i));
    }
};

} // namespace northlight::plugins::dsp
