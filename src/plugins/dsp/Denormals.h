#pragma once

#include <cmath>
#include <concepts>

// Flushing a decaying state to zero once it is too small to hear, so a tail never
// runs on in denormals (which cost far more per operation on some CPUs, and FTZ is
// not set on every thread a processor runs on, such as a test's). Each caller
// keeps its own tolerance: moving one changes when a tail ends, and so the output.

namespace northlight::plugins::dsp
{

template <std::floating_point T> T flush(T x, T tolerance) noexcept
{
    return std::abs(x) < tolerance ? T(0) : x;
}

} // namespace northlight::plugins::dsp
