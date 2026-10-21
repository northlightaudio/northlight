#include "engine/Resampling.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace northlight::engine;
using Catch::Approx;

namespace
{
double correlation(const std::vector<float>& data, double rate, std::int64_t start, double hz)
{
    double acc = 0.0, energy = 0.0;
    for (size_t i = 0; i < data.size(); ++i)
    {
        const double expected =
            0.5 * std::sin(2.0 * std::numbers::pi * hz *
                           static_cast<double>(start + static_cast<std::int64_t>(i)) / rate);
        acc += data[i] * expected;
        energy += expected * expected;
    }
    return acc / energy;
}
} // namespace

TEST_CASE("Hermite interpolation reproduces the input at integer positions and is exact for lines",
          "[engine][resample]")
{
    CHECK(hermite(1.0f, 2.0f, 3.0f, 4.0f, 0.0f) == 2.0f);
    CHECK(hermite(1.0f, 2.0f, 3.0f, 4.0f, 1.0f) == Approx(3.0f));
    CHECK(hermite(1.0f, 2.0f, 3.0f, 4.0f, 0.25f) == Approx(2.25f));
    CHECK(hermite(0.0f, 0.0f, 0.0f, 0.0f, 0.5f) == 0.0f);
}

TEST_CASE("Resampling 44.1 kHz to 48 kHz keeps a sine aligned with no lag, from any start",
          "[engine][resample]")
{
    const double sourceRate = 44100.0, targetRate = 48000.0;
    const double ratio = sourceRate / targetRate;
    std::vector<float> source(44100);
    for (size_t i = 0; i < source.size(); ++i)
        source[i] = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 *
                                                       static_cast<double>(i) / sourceRate));

    for (std::int64_t start : {std::int64_t{0}, std::int64_t{1024}, std::int64_t{5000}, std::int64_t{30001}})
    {
        std::int64_t sourceStart = 0;
        int sourceCount = 0;
        sourceRangeFor(ratio, start, 1024, sourceStart, sourceCount);
        std::vector<float> chunk(1024);
        // Feed only the needed range, as the stream reader does.
        std::vector<float> window(static_cast<size_t>(sourceCount), 0.0f);
        for (int i = 0; i < sourceCount; ++i)
        {
            const auto s = sourceStart + i;
            if (s >= 0 && static_cast<size_t>(s) < source.size())
                window[static_cast<size_t>(i)] = source[static_cast<size_t>(s)];
        }
        resampleHermite(window.data(), sourceStart, sourceCount, ratio, start, 1024, chunk.data());
        CHECK(correlation(chunk, targetRate, start, 1000.0) > 0.999);
        // And it is better aligned than one sample either way.
        CHECK(correlation(chunk, targetRate, start - 1, 1000.0) <
              correlation(chunk, targetRate, start, 1000.0));
        CHECK(correlation(chunk, targetRate, start + 1, 1000.0) <
              correlation(chunk, targetRate, start, 1000.0));
    }
}

TEST_CASE("Resampling downwards (96 kHz to 48 kHz) and at unity are exact", "[engine][resample]")
{
    std::vector<float> source(9600);
    for (size_t i = 0; i < source.size(); ++i)
        source[i] = static_cast<float>(i);
    std::vector<float> out(100);
    resampleHermite(source.data(), 0, static_cast<std::int64_t>(source.size()), 2.0, 50, 100, out.data());
    for (int i = 0; i < 100; ++i)
        CHECK(out[static_cast<size_t>(i)] == Approx(static_cast<float>((50 + i) * 2)));
    resampleHermite(source.data(), 0, static_cast<std::int64_t>(source.size()), 1.0, 7, 100, out.data());
    for (int i = 0; i < 100; ++i)
        CHECK(out[static_cast<size_t>(i)] == Approx(static_cast<float>(7 + i)));
}
