// The shared DSP building blocks in src/plugins/dsp: each piece on its own.
#include "plugins/dsp/Random.h"
#include "plugins/dsp/Smoothing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

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

TEST_CASE("A one-pole follower reaches 63% of a step after its time constant", "[plugins][dsp]")
{
    constexpr double rate = 48000.0;
    const double step = dsp::stepForSeconds(0.01, rate);
    CHECK(step == Approx(1.0 - dsp::poleForSeconds(0.01, rate)));
    double y = 0.0;
    for (int i = 0; i < 480; ++i)
        y += step * (1.0 - y);
    CHECK(y == Approx(1.0 - std::exp(-1.0)).margin(1.0e-3));

    // Milliseconds are seconds / 1000, in either precision.
    CHECK(dsp::poleForMs(10.0, rate) == Approx(dsp::poleForSeconds(0.01, rate)));
    CHECK(dsp::poleForMs(10.0f, 48000.0f) == Approx(dsp::poleForSeconds(0.01f, 48000.0f)));
}

TEST_CASE("A block step equals the per-sample step applied once a sample", "[plugins][dsp]")
{
    const double perSample = dsp::stepForSeconds(0.05, 44100.0);
    const double block = dsp::blockStepForSeconds(0.05, 44100.0, 64.0);
    CHECK(1.0 - block == Approx(std::pow(1.0 - perSample, 64.0)));
}

TEST_CASE("A one-pole low-pass from a corner frequency is down 3 dB there", "[plugins][dsp]")
{
    constexpr double rate = 48000.0, hz = 500.0;
    const double step = dsp::stepForHz(hz, rate);
    CHECK(step == Approx(1.0 - dsp::poleForHz(hz, rate)));
    // |H| of y += k (x - y) at hz: k / |1 - (1 - k) e^{-jw}|.
    const double w = 2.0 * std::numbers::pi * hz / rate;
    const double re = 1.0 - (1.0 - step) * std::cos(w), im = (1.0 - step) * std::sin(w);
    const double magnitude = step / std::sqrt(re * re + im * im);
    CHECK(20.0 * std::log10(magnitude) == Approx(-3.0).margin(0.1));
}

TEST_CASE("A 60 dB decay falls by a factor of 1000 over its time", "[plugins][dsp]")
{
    constexpr double rate = 48000.0;
    const double pole = dsp::decay60ForMs(250.0, rate);
    CHECK(std::pow(pole, 0.25 * rate) == Approx(0.001));
    // Shorter than a sample still takes one.
    CHECK(dsp::decay60ForMs(0.0, rate) == Approx(0.001));
    CHECK(dsp::decay60ForMs(250.0f, 48000.0f) == Approx(static_cast<float>(pole)));
}
