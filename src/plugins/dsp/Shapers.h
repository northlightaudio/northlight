#pragma once

#include <cmath>
#include <numbers>

// Saturation curves with their antiderivatives, and the first-order
// antiderivative anti-aliasing (ADAA1) step that uses them: the output is the
// mean of the curve over each step between samples, which takes most of a
// curve's aliasing away without oversampling.

namespace northlight::plugins::dsp
{

// log(cosh x), the antiderivative of tanh, without overflow for large |x|.
inline double logCosh(double x) noexcept
{
    const double a = std::abs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - std::numbers::ln2;
}

// A biased tanh, a valve's asymmetric curve: tanh(x + bias) - tanh(bias), so it
// passes through zero, and its antiderivative. `offset` is tanh(bias), for a
// caller that works it out once.
inline double tube(double x, double bias, double offset) noexcept
{
    return std::tanh(x + bias) - offset;
}
inline double tube(double x, double bias) noexcept
{
    return tube(x, bias, std::tanh(bias));
}
inline double tubeIntegral(double x, double bias, double offset) noexcept
{
    return logCosh(x + bias) - x * offset;
}
inline double tubeIntegral(double x, double bias) noexcept
{
    return tubeIntegral(x, bias, std::tanh(bias));
}

// One ADAA1 step from `previous` to `x`, given the antiderivative at both ends.
// Below `threshold` the quotient loses its precision, so a shorter step takes the
// curve at its middle instead. The threshold is the caller's: changing it
// changes the output.
template <class Curve>
double adaa1(double x, double previous, double fx, double fPrevious, double threshold, Curve&& curve) noexcept
{
    const double dx = x - previous;
    return std::abs(dx) > threshold ? (fx - fPrevious) / dx : curve(0.5 * (x + previous));
}

} // namespace northlight::plugins::dsp
