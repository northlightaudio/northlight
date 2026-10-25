#pragma once

#include <algorithm>
#include <cmath>

// YIN pitch detection (de Cheveigné and Kawahara, 2002), the parts the tuner
// (Polaris) and the Vesper Listener share. Both run on the audio thread over
// buffers they own, so nothing here allocates. JUCE-free.

namespace northlight::plugins::yin
{

// The squared difference d(tau) of a window with itself shifted, over the first
// `span` samples, for tau in [from, to).
inline void difference(const float* x, int span, int from, int to, float* d) noexcept
{
    for (int tau = from; tau < to; ++tau)
    {
        float sum = 0.0f;
        const float* a = x;
        const float* b = x + tau;
        for (int j = 0; j < span; ++j)
        {
            const float diff = a[j] - b[j];
            sum += diff * diff;
        }
        d[tau] = sum;
    }
}

// YIN's pick from the differences raw[0..tauMax]: the cumulative mean normalised
// difference (into `d`), the first dip under `threshold` (or the deepest under
// `fallback`), refined between samples on the raw differences (YIN's step 5: the
// normalised curve is skewed between lags). The period in samples (0 for none);
// `clarity` (optional) is 1 less the chosen dip, 0 for none.
inline float pick(const float* raw,
                  float* d,
                  int tauMin,
                  int tauMax,
                  float threshold,
                  float fallback,
                  float* clarity) noexcept
{
    float running = 0.0f;
    d[0] = 1.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += raw[tau];
        d[tau] = running > 0.0f ? raw[tau] * static_cast<float>(tau) / running : 1.0f;
    }
    int best = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
        if (d[tau] < threshold)
        {
            while (tau + 1 <= tauMax && d[tau + 1] < d[tau])
                ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        int deepest = tauMin;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (d[tau] < d[deepest])
                deepest = tau;
        if (d[deepest] < fallback)
            best = deepest;
    }
    if (best < 0)
    {
        if (clarity != nullptr)
            *clarity = 0.0f;
        return 0.0f;
    }
    if (clarity != nullptr)
        *clarity = std::clamp(1.0f - d[best], 0.0f, 1.0f);
    // A high note's period is a few samples: measured over as many periods as the
    // lags reach and divided back, its sub-sample error shrinks with them.
    const int periods = std::max(1, tauMax / best - 1);
    int at = best * periods;
    for (int tau = std::max(1, at - periods); tau <= std::min(tauMax - 1, at + periods); ++tau)
        if (raw[tau] < raw[at])
            at = tau;
    float period = static_cast<float>(at);
    if (at > 1 && at < tauMax)
    {
        const float a = raw[at - 1], b = raw[at], c = raw[at + 1];
        const float denominator = a - 2.0f * b + c;
        if (std::abs(denominator) > 1.0e-12f)
            period += std::clamp(0.5f * (a - c) / denominator, -0.5f, 0.5f);
    }
    return period / static_cast<float>(periods);
}

} // namespace northlight::plugins::yin
