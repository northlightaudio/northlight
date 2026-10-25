// The shared DSP building blocks in src/plugins/dsp: each piece on its own.
#include "plugins/dsp/DelayLine.h"
#include "plugins/dsp/PolyBlep.h"
#include "plugins/dsp/Random.h"
#include "plugins/dsp/Shapers.h"
#include "plugins/dsp/Smoothing.h"
#include "plugins/dsp/SpringTank.h"
#include "plugins/dsp/Svf.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

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

TEST_CASE("The interpolators pass through their samples and Hermite is exact on a cubic", "[plugins][dsp]")
{
    CHECK(dsp::interpolateLinear(2.0f, 4.0f, 0.0f) == 2.0f);
    CHECK(dsp::interpolateLinear(2.0f, 4.0f, 0.25f) == 2.5f);
    CHECK(dsp::interpolateHermite(1.0f, 2.0f, 3.0f, 4.0f, 0.0f) == 2.0f);
    CHECK(dsp::interpolateHermite(1.0f, 2.0f, 3.0f, 4.0f, 1.0f) == Approx(3.0f));
    // Catmull-Rom reproduces a quadratic exactly: x^2 at -1, 0, 1, 2.
    CHECK(dsp::interpolateHermite(1.0f, 0.0f, 1.0f, 4.0f, 0.5f) == Approx(0.25f));
}

TEST_CASE("A delay line reads back what was pushed, whole and fractional", "[plugins][dsp]")
{
    dsp::DelayLine line;
    line.prepare(100);
    REQUIRE(line.data.size() >= 108);
    REQUIRE((line.data.size() & (line.data.size() - 1)) == 0);
    for (int i = 0; i < 300; ++i)
        line.push(static_cast<float>(i)); // a ramp, so every read is exact
    CHECK(line.at(0) == 299.0f);
    CHECK(line.at(10) == 289.0f);
    CHECK(line.linear(10.5f) == Approx(288.5f));
    CHECK(line.hermite(20.25f) == Approx(278.75f));

    // The free reads on a ring of the caller's own agree with the line.
    const int write = line.write; // the next slot, one past the newest
    CHECK(dsp::readLinear(line.data.data(), line.mask, write, 11.5f) == Approx(288.5f));
    CHECK(dsp::readHermite(line.data.data(), line.mask, write, 21.25f) == Approx(278.75f));

    line.clear();
    CHECK(line.at(5) == 0.0f);
}

TEST_CASE("The state-variable filter's outputs split a tone at its cutoff", "[plugins][dsp]")
{
    constexpr double rate = 48000.0;
    const auto g = static_cast<float>(std::tan(std::numbers::pi * 1000.0 / rate));
    constexpr float k = 1.4142135f; // Q = 1/sqrt(2)
    const auto levels = [&](double hz)
    {
        dsp::Svf svf;
        double low = 0.0, band = 0.0, high = 0.0, in = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            const auto x = static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / rate));
            float l, b, h;
            svf.run(x, g, k, l, b, h);
            if (i >= 24000)
            {
                low += l * l;
                band += b * b;
                high += h * h;
                in += x * x;
            }
        }
        return std::array<double, 3>{
            10.0 * std::log10(low / in), 10.0 * std::log10(band / in), 10.0 * std::log10(high / in)};
    };
    const auto at = levels(1000.0);
    CHECK(at[0] == Approx(-3.0).margin(0.2)); // Butterworth low and high: -3 dB at the corner
    CHECK(at[2] == Approx(-3.0).margin(0.2));
    const auto low = levels(100.0), high = levels(10000.0);
    CHECK(low[0] > -0.1);
    CHECK(low[2] < -35.0);
    CHECK(high[2] > -0.1);
    CHECK(high[0] < -35.0);

    dsp::Svf svf;
    float l, b, h;
    svf.run(1.0f, g, k, l, b, h);
    svf.reset();
    CHECK(svf.ic1 == 0.0f);
    CHECK(svf.ic2 == 0.0f);
}

TEST_CASE("PolyBLEP corrects only the samples either side of a jump", "[plugins][dsp]")
{
    constexpr double dt = 0.01;
    CHECK(dsp::polyBlep(0.5, dt) == 0.0f);
    CHECK(dsp::polyBlep(0.0, dt) == -1.0f);                  // halfway down the step, from above
    CHECK(dsp::polyBlep(1.0 - 1.0e-12, dt) == Approx(1.0f)); // and from below
    // The saw is continuous across its wrap: the corrected ends meet near zero.
    CHECK(std::abs(dsp::blepSaw(0.0, dt)) < 1.0e-6f);
    CHECK(std::abs(dsp::blepSaw(1.0 - 1.0e-12, dt)) < 1.0e-6f);
    // Away from the edges the shapes are the naive ones.
    CHECK(dsp::blepSaw(0.25, dt) == -0.5f);
    CHECK(dsp::blepSquare(0.25, dt) == 1.0f);
    CHECK(dsp::blepSquare(0.75, dt) == -1.0f);
    CHECK(std::abs(dsp::blepSquare(0.5, dt)) < 1.0e-6f);
}

TEST_CASE("A spring tank returns an impulse a loop later, chirped, and dies away", "[plugins][dsp]")
{
    constexpr double rate = 48000.0, loopSeconds = 0.04;
    dsp::SpringTank tank;
    tank.prepare(dsp::SpringTuning{}, rate, loopSeconds, 4000.0, 0.7f);
    std::vector<float> out(48000);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = tank.process(i == 0 ? 1.0f : 0.0f);

    // Energy in each loop-long window: the second pass is quieter than the first,
    // and the tail has died away by the end.
    const auto loop = static_cast<size_t>(loopSeconds * rate);
    const auto energy = [&](size_t from)
    {
        double e = 0.0;
        for (size_t i = from; i < from + loop; ++i)
            e += static_cast<double>(out[i]) * out[i];
        return e;
    };
    const double first = energy(0), second = energy(loop), late = energy(out.size() - loop);
    CHECK(first > 0.0);
    CHECK(second < first);
    CHECK(late < first * 1.0e-6);

    tank.reset();
    float sum = 0.0f;
    for (int i = 0; i < 4000; ++i)
        sum += std::abs(tank.process(0.0f));
    CHECK(sum == 0.0f);
}

TEST_CASE("logCosh is tanh's antiderivative and stays finite far out", "[plugins][dsp]")
{
    CHECK(dsp::logCosh(0.0) == 0.0);
    for (const double x : {-3.0, -0.4, 0.2, 1.5})
    {
        constexpr double h = 1.0e-5;
        CHECK((dsp::logCosh(x + h) - dsp::logCosh(x - h)) / (2.0 * h) == Approx(std::tanh(x)).margin(1.0e-8));
        CHECK(dsp::logCosh(x) == Approx(std::log(std::cosh(x))));
    }
    CHECK(dsp::logCosh(1000.0) == Approx(1000.0 - std::numbers::ln2));
}

TEST_CASE("The tube curve passes through zero and its integral is its antiderivative", "[plugins][dsp]")
{
    for (const double bias : {0.25, 0.3})
    {
        CHECK(dsp::tube(0.0, bias) == 0.0);
        CHECK(dsp::tube(0.7, bias) == dsp::tube(0.7, bias, std::tanh(bias)));
        CHECK(dsp::tube(0.5, bias) < -dsp::tube(-0.5, bias)); // asymmetric: the top flattens first
        constexpr double h = 1.0e-5;
        const double x = 0.6;
        CHECK((dsp::tubeIntegral(x + h, bias) - dsp::tubeIntegral(x - h, bias)) / (2.0 * h) ==
              Approx(dsp::tube(x, bias)).margin(1.0e-8));
    }
}

TEST_CASE("An ADAA1 step is the curve's mean over the step, or its middle for a tiny one", "[plugins][dsp]")
{
    const auto tanhAt = [](double m) { return std::tanh(m); };
    // A long step: the antiderivative's slope.
    const double y = dsp::adaa1(1.0, 0.0, dsp::logCosh(1.0), dsp::logCosh(0.0), 1.0e-6, tanhAt);
    CHECK(y == Approx(dsp::logCosh(1.0)));
    // Below the threshold: the curve at the middle.
    const double z = dsp::adaa1(0.5 + 1.0e-7, 0.5, 123.0, 0.0, 1.0e-6, tanhAt);
    CHECK(z == Approx(std::tanh(0.5 + 0.5e-7)));
}
