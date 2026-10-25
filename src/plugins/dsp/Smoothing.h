#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <numbers>

// One-pole smoothing coefficients, in the two forms the processors use:
//
//   pole: y = pole * y + (1 - pole) * x   (the pole itself, e^(-1/tau))
//   step: y += step * (x - y)             (1 - pole)
//
// Times are a time constant (the follower reaches 63% of a step after it), in
// seconds or milliseconds; a frequency is the -3 dB corner of the one-pole low-pass.
// Each comes in float and double: a caller keeps the precision (and so the exact
// coefficient) it had. Clamp at the call site; the helpers do not.

namespace northlight::plugins::dsp
{

template <std::floating_point T> T poleForSeconds(T seconds, T sampleRate) noexcept
{
    return std::exp(T(-1) / (sampleRate * seconds));
}

template <std::floating_point T> T poleForMs(T ms, T sampleRate) noexcept
{
    return std::exp(T(-1) / (sampleRate * ms * T(0.001)));
}

template <std::floating_point T> T stepForSeconds(T seconds, T sampleRate) noexcept
{
    return T(1) - std::exp(T(-1) / (seconds * sampleRate));
}

// The step for a follower updated once every `samples` samples (once a block).
template <std::floating_point T> T blockStepForSeconds(T seconds, T sampleRate, T samples) noexcept
{
    return T(1) - std::exp(-samples / (sampleRate * seconds));
}

template <std::floating_point T> T poleForHz(T hz, T sampleRate) noexcept
{
    return std::exp(T(-2) * std::numbers::pi_v<T> * hz / sampleRate);
}

template <std::floating_point T> T stepForHz(T hz, T sampleRate) noexcept
{
    return T(1) - poleForHz(hz, sampleRate);
}

// The pole of a decay that falls 60 dB over `ms` (at least one sample).
template <std::floating_point T> T decay60ForMs(T ms, T sampleRate) noexcept
{
    return std::exp(std::log(T(0.001)) / std::max(T(1), ms * T(0.001) * sampleRate));
}

} // namespace northlight::plugins::dsp
