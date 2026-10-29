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

} // namespace northlight::plugins::fm
