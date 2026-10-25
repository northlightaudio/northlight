#include "plugins/Oversampler.h"

#include "plugins/dsp/Denormals.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace northlight::plugins
{

HalfBandDesign designHalfBand(int order, double transition)
{
    HalfBandDesign result;
    result.order = std::clamp(order, 2, HalfBandDesign::maxOrder);
    double k = std::tan((1.0 - transition * 2.0) * std::numbers::pi / 4.0);
    k *= k;
    const double kksqrt = std::pow(1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
    const double e4 = e * e * e * e;
    const double q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
    const int filterOrder = result.order * 2 + 1;
    for (int index = 0; index < result.order; ++index)
    {
        const int c = index + 1;
        double num = 0.0;
        for (int i = 0, sign = 1;; ++i, sign = -sign)
        {
            const double term =
                std::pow(q, i * (i + 1)) * std::sin((i * 2 + 1) * c * std::numbers::pi / filterOrder) * sign;
            num += term;
            if (std::abs(term) <= 1.0e-100 || i > 64)
                break;
        }
        double den = 0.0;
        for (int i = 1, sign = -1;; ++i, sign = -sign)
        {
            const double term =
                std::pow(q, i * i) * std::cos(i * 2 * c * std::numbers::pi / filterOrder) * sign;
            den += term;
            if (std::abs(term) <= 1.0e-100 || i > 64)
                break;
        }
        num *= std::pow(q, 0.25);
        den += 0.5;
        const double ww = num / den;
        const double wwsq = ww * ww;
        const double x = std::sqrt((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        result.c[static_cast<size_t>(index)] = static_cast<float>((1.0 - x) / (1.0 + x));
    }
    return result;
}

void HalfBandChains::flush() noexcept
{
    for (auto* v : {&x, &y})
        for (auto& s : *v)
            s = dsp::flush(s, 1.0e-20f);
}

namespace
{
// Stage 0 sits next to the base rate and must be sharp; stages 1 and 2 only
// clear images an octave or more above the audio band. Designed at load, so the
// real-time thread never runs the design.
const std::array<HalfBandDesign, 3> stageDesigns{
    designHalfBand(8, 0.04),
    designHalfBand(6, 0.12),
    designHalfBand(4, 0.17),
};
} // namespace

void Oversampler::setFactor(int factor) noexcept
{
    stages_ = factor >= 8 ? 3 : factor >= 4 ? 2 : factor >= 2 ? 1 : 0;
    reset();
}

void Oversampler::reset() noexcept
{
    for (auto& c : up_)
        c.reset();
    for (auto& c : down_)
        c.reset();
}

void Oversampler::flush() noexcept
{
    for (auto& c : up_)
        c.flush();
    for (auto& c : down_)
        c.flush();
}

void Oversampler::up(float x, float* out) noexcept
{
    const auto& d = stageDesigns;
    out[0] = x;
    int n = 1;
    for (int s = 0; s < stages_; ++s)
    {
        // Each sample becomes two, in time order.
        float in[maxFactor];
        std::copy(out, out + n, in);
        auto& chains = up_[static_cast<size_t>(s)];
        for (int i = 0; i < n; ++i)
        {
            float a = in[i], b = in[i];
            chains.run(d[static_cast<size_t>(s)], a, b);
            out[2 * i] = a;
            out[2 * i + 1] = b;
        }
        n *= 2;
    }
}

float Oversampler::down(const float* in) noexcept
{
    const auto& d = stageDesigns;
    if (stages_ == 0)
        return in[0];
    float buffer[maxFactor];
    int n = 1 << stages_;
    std::copy(in, in + n, buffer);
    for (int s = stages_ - 1; s >= 0; --s)
    {
        auto& chains = down_[static_cast<size_t>(s)];
        for (int i = 0; i < n / 2; ++i)
        {
            float later = buffer[2 * i + 1], earlier = buffer[2 * i];
            chains.run(d[static_cast<size_t>(s)], later, earlier);
            buffer[i] = 0.5f * (later + earlier);
        }
        n /= 2;
    }
    return buffer[0];
}

} // namespace northlight::plugins
