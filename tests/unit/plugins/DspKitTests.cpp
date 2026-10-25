// The shared DSP building blocks in src/plugins/dsp: each piece on its own.
#include "plugins/dsp/Random.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace northlight::plugins;
using Catch::Approx;

TEST_CASE("XorShift32 follows Marsaglia's sequence and never sticks at zero", "[plugins][dsp]")
{
    dsp::XorShift32 rng{1u};
    CHECK(rng.bits() == 270369u);
    CHECK(rng.bits() == 67634689u);

    dsp::XorShift32 zero{0u};
    CHECK(zero.state == 1u);
    CHECK(zero.bits() != 0u);
}

TEST_CASE("XorShift32's mappings stay in their ranges", "[plugins][dsp]")
{
    dsp::XorShift32 rng{0x9E3779B9u};
    double sumUniform = 0.0, sumWord = 0.0;
    constexpr int n = 20000;
    for (int i = 0; i < n; ++i)
    {
        const float u = rng.uniform();
        REQUIRE(u >= 0.0f);
        REQUIRE(u < 1.0f);
        const float b = rng.bipolar();
        REQUIRE(b >= -1.0f);
        REQUIRE(b < 1.0f);
        const float w = rng.bipolarWord();
        REQUIRE(w >= -1.0f);
        REQUIRE(w <= 1.0f);
        const float uw = rng.unitWord();
        REQUIRE(uw >= 0.0f);
        REQUIRE(uw <= 1.0f);
        sumUniform += u;
        sumWord += w;
    }
    CHECK(sumUniform / n == Approx(0.5).margin(0.02));
    CHECK(sumWord / n == Approx(0.0).margin(0.02));
}

TEST_CASE("XorShift32's mappings are the documented functions of its bits", "[plugins][dsp]")
{
    dsp::XorShift32 a{12345u}, b{12345u};
    const std::uint32_t bits = b.bits();
    CHECK(a.uniform() == static_cast<float>(bits >> 8) / 16777216.0f);
    dsp::XorShift32 c{12345u};
    CHECK(c.bipolarWord() == static_cast<float>(bits) * (2.0f / 4294967296.0f) - 1.0f);
}

TEST_CASE("Lcg follows the Numerical Recipes recurrence and maps its top 24 bits", "[plugins][dsp]")
{
    dsp::Lcg rng{0u};
    CHECK(rng.bits() == 1013904223u);
    CHECK(rng.bits() == 1013904223u * 1664525u + 1013904223u);

    dsp::Lcg a{0x2545F491u}, b{0x2545F491u}, c{0x2545F491u};
    const double unit = a.unitDouble();
    CHECK(static_cast<double>(b.unit()) == unit);
    CHECK(c.bipolar() == static_cast<float>(unit * 2.0 - 1.0));
    for (int i = 0; i < 10000; ++i)
    {
        const float x = a.bipolar();
        REQUIRE(x >= -1.0f);
        REQUIRE(x < 1.0f);
    }
}

TEST_CASE("Drift is a smooth curve that stays in -1..1", "[plugins][dsp]")
{
    dsp::XorShift32 rng{0x2545F491u};
    dsp::Drift drift;
    float previous = drift.next(rng, 100.0f);
    float largestStep = 0.0f;
    for (int i = 0; i < 20000; ++i)
    {
        const float x = drift.next(rng, 100.0f);
        REQUIRE(x >= -1.0f);
        REQUIRE(x <= 1.0f);
        largestStep = std::max(largestStep, std::abs(x - previous));
        previous = x;
    }
    // A segment of at least 60 samples moves at most 2 * 1.5 / 60 per sample.
    CHECK(largestStep < 0.06f);
}
