// Pulsar's voices: modelled on the TR-808, the same at any sample rate.
#include "plugins/Pulsar.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace northlight::plugins;

namespace
{
std::vector<float> defaults()
{
    std::vector<float> params;
    for (const auto& p : pulsar::parameterInfo())
        params.push_back(p.defaultValue);
    return params;
}

double rms(const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to && i < x.size(); ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(std::max<size_t>(1, to - from)));
}

void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> w(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> u(1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto x = a[i + k], y = a[i + k + len / 2] * u;
                a[i + k] = x + y;
                a[i + k + len / 2] = x - y;
                u *= w;
            }
        }
    }
}

// How bright a sound is: the centre of its spectrum (on a log scale) across the
// audible band, 40 Hz to 16 kHz.
double brightness(const std::vector<float>& x, size_t samples, double sampleRate)
{
    const size_t n = std::min(samples, x.size());
    size_t size = 1;
    while (size < n)
        size <<= 1;
    std::vector<std::complex<double>> a(size);
    for (size_t i = 0; i < n; ++i)
        a[i] =
            x[i] *
            (0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n)));
    fft(a);
    double weighted = 0.0, total = 0.0;
    for (size_t k = 1; k < size / 2; ++k)
    {
        const double hz = static_cast<double>(k) * sampleRate / static_cast<double>(size);
        if (hz < 40.0 || hz > 16000.0)
            continue;
        const double power = std::norm(a[k]);
        weighted += power * std::log2(hz);
        total += power;
    }
    return total > 0.0 ? std::pow(2.0, weighted / total) : 0.0;
}

// The frequency from the upward zero crossings between two times (seconds).
double frequency(const std::vector<float>& x, double sampleRate, double from, double to)
{
    std::vector<size_t> crossings;
    for (auto i = static_cast<size_t>(from * sampleRate); i + 1 < x.size() && i < to * sampleRate; ++i)
        if (x[i] < 0.0f && x[i + 1] >= 0.0f)
            crossings.push_back(i);
    if (crossings.size() < 2)
        return 0.0;
    return sampleRate * static_cast<double>(crossings.size() - 1) /
           static_cast<double>(crossings.back() - crossings.front());
}
} // namespace

TEST_CASE("Pulsar's pads sound the same at any sample rate", "[plugins][pulsar]")
{
    const auto params = defaults();
    for (int pad = 0; pad < pulsar::padCount; ++pad)
    {
        INFO(pulsar::padName(pad));
        const auto at = [&](double rate)
        {
            const auto out = pulsar::renderPad(params.data(), pad, rate, static_cast<int>(rate * 0.3));
            const auto window = static_cast<size_t>(rate * 0.2);
            return std::pair{rms(out, 0, window), brightness(out, window, rate)};
        };
        const auto [level44, bright44] = at(44100.0);
        const auto [level96, bright96] = at(96000.0);
        CHECK(std::abs(level96 / level44 - 1.0) < 0.1);
        CHECK(std::abs(bright96 / bright44 - 1.0) < 0.1);
    }
}

TEST_CASE("Pulsar's pads stay finite and die away at every sample rate", "[plugins][pulsar]")
{
    auto params = defaults();
    for (int pad = 0; pad < pulsar::padCount; ++pad)
        params[static_cast<size_t>(pulsar::paramIndex(pad, pulsar::decay))] = 3000.0f;
    for (const double rate : {44100.0, 48000.0, 96000.0, 192000.0})
        for (int pad = 0; pad < pulsar::padCount; ++pad)
        {
            INFO(rate << " " << pulsar::padName(pad));
            const auto out = pulsar::renderPad(params.data(), pad, rate, static_cast<int>(rate * 3.2));
            CHECK(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v); }));
            const auto peak = *std::max_element(
                out.begin(), out.end(), [](float a, float b) { return std::abs(a) < std::abs(b); });
            CHECK(std::abs(peak) > 0.1f);
            CHECK(std::abs(peak) < 3.0f);
            CHECK(rms(out, out.size() - static_cast<size_t>(rate * 0.1), out.size()) < 1.0e-3);
        }
}

TEST_CASE("Pulsar's kick settles on its tuned note", "[plugins][pulsar]")
{
    auto params = defaults();
    const auto tuneAt = static_cast<size_t>(pulsar::paramIndex(pulsar::kick, pulsar::tune));
    const auto out = pulsar::renderPad(params.data(), pulsar::kick, 48000.0, 24000);
    CHECK(std::abs(frequency(out, 48000.0, 0.15, 0.45) - 48.0) < 1.0);
    params[tuneAt] = 12.0f;
    const auto up = pulsar::renderPad(params.data(), pulsar::kick, 48000.0, 24000);
    CHECK(std::abs(frequency(up, 48000.0, 0.15, 0.45) - 96.0) < 2.0);
}

TEST_CASE("Pulsar's kick drops further in pitch with more punch", "[plugins][pulsar]")
{
    auto params = defaults();
    const auto punchAt = static_cast<size_t>(pulsar::paramIndex(pulsar::kick, pulsar::snap));
    // The first half cycle: as long as half a period of its pitch then.
    const auto early = [&](float punch)
    {
        params[punchAt] = punch;
        const auto out = pulsar::renderPad(params.data(), pulsar::kick, 48000.0, 4800);
        size_t fall = 1;
        while (fall + 1 < out.size() && !(out[fall] > 0.0f && out[fall + 1] <= 0.0f))
            ++fall;
        return 48000.0 / (2.0 * static_cast<double>(fall + 1));
    };
    const double soft = early(0.0f), hard = early(1.0f);
    CHECK(soft < 56.0); // an 808's gentle drop
    CHECK(hard > soft * 1.5);
}

TEST_CASE("Pulsar's clap is a run of bursts before its tail", "[plugins][pulsar]")
{
    const auto params = defaults();
    const auto out = pulsar::renderPad(params.data(), pulsar::clap, 48000.0, 4800);
    // The level in 1 ms steps: each burst is a rise after a fall.
    std::vector<double> level;
    for (size_t at = 0; at + 48 <= 2400; at += 48)
        level.push_back(rms(out, at, at + 48));
    int rises = 0;
    for (size_t i = 1; i < level.size(); ++i)
        if (level[i] > level[i - 1] * 1.4 && level[i - 1] > 0.0)
            ++rises;
    CHECK(rises >= 3);
}

TEST_CASE("Pulsar's hats are bright and its kick is deep", "[plugins][pulsar]")
{
    const auto params = defaults();
    const auto bright = [&](int pad)
    { return brightness(pulsar::renderPad(params.data(), pad, 48000.0, 9600), 9600, 48000.0); };
    CHECK(bright(pulsar::closedHat) > 6000.0);
    CHECK(bright(pulsar::openHat) > 6000.0);
    CHECK(bright(pulsar::kick) < 150.0);
    CHECK(bright(pulsar::rim) > 1000.0);
}
