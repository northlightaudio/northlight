#include "plugins/fm/Fm.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace northlight::plugins::fm
{

namespace
{
constexpr double twoPi = 2.0 * std::numbers::pi;

// A wave's shape at phase 0..1, before its mean is removed.
double rawWave(Wave wave, double p) noexcept
{
    const double s = std::sin(twoPi * p);
    switch (wave)
    {
        case Wave::sine:
            return s;
        case Wave::half:
            return p < 0.5 ? s : 0.0;
        case Wave::rectified:
            return std::abs(s);
        case Wave::quarters:
            return (p < 0.25 || (p >= 0.5 && p < 0.75)) ? std::abs(s) : 0.0;
        case Wave::doubleSine:
            return p < 0.5 ? std::sin(2.0 * twoPi * p) : 0.0;
        case Wave::doubleHalf:
            return p < 0.5 ? std::abs(std::sin(2.0 * twoPi * p)) : 0.0;
        case Wave::softSquare:
            return std::tanh(3.0 * s);
        case Wave::triangle:
            return p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0;
    }
    return s;
}

using Tables = std::array<std::array<float, tableSize + 1>, waveCount>;

Tables build() noexcept
{
    Tables tables{};
    for (int w = 0; w < waveCount; ++w)
    {
        const auto wave = static_cast<Wave>(w);
        std::array<double, tableSize> raw{};
        double mean = 0.0;
        for (int i = 0; i < tableSize; ++i)
        {
            raw[static_cast<size_t>(i)] = rawWave(wave, static_cast<double>(i) / tableSize);
            mean += raw[static_cast<size_t>(i)];
        }
        mean /= tableSize;
        double peak = 0.0;
        for (auto& v : raw)
        {
            v -= mean;
            peak = std::max(peak, std::abs(v));
        }
        auto& table = tables[static_cast<size_t>(w)];
        for (int i = 0; i < tableSize; ++i)
            table[static_cast<size_t>(i)] = static_cast<float>(raw[static_cast<size_t>(i)] / peak);
        table[tableSize] = table[0];
    }
    return tables;
}

// Built at static initialisation, before any audio thread runs.
const Tables tables = build();
} // namespace

const std::array<std::array<float, tableSize + 1>, waveCount>& waveTables() noexcept
{
    return tables;
}

float waveAt(Wave wave, float phase) noexcept
{
    const float wrapped = phase - std::floor(phase);
    return lookup(tables[static_cast<size_t>(wave)], phaseOffset(wrapped));
}

double besselJ(int n, double x) noexcept
{
    // J_-n = (-1)^n J_n, and J_n(-x) = (-1)^n J_n(x).
    const int order = n < 0 ? -n : n;
    double sign = (n < 0 && (order % 2) != 0) ? -1.0 : 1.0;
    if (x < 0.0)
    {
        x = -x;
        if ((order % 2) != 0)
            sign = -sign;
    }
    if (x == 0.0)
        return order == 0 ? sign : 0.0;
    // Miller's backward recurrence from well above both the order and x,
    // normalised by J_0 + 2 (J_2 + J_4 + ...) = 1; stable at any index.
    const int top =
        2 * ((std::max(order, static_cast<int>(x)) +
              static_cast<int>(std::sqrt(40.0 * std::max(order, static_cast<int>(x)) + 40.0)) + 20) /
             2);
    double next = 0.0, current = 1.0e-30, result = 0.0, even = 0.0;
    for (int k = top; k > 0; --k)
    {
        const double previous = 2.0 * k / x * current - next;
        next = current;
        current = previous;
        if (std::abs(current) > 1.0e10)
        {
            // Keep the numbers in range; the ratio is what matters.
            current *= 1.0e-10;
            next *= 1.0e-10;
            result *= 1.0e-10;
            even *= 1.0e-10;
        }
        if (k - 1 == order)
            result = current;
        if ((k - 1) % 2 == 0 && k - 1 > 0)
            even += current;
    }
    const double norm = current + 2.0 * even; // current is now the unnormalised J_0
    return sign * result / norm;
}

} // namespace northlight::plugins::fm
