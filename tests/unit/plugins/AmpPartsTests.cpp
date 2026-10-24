// The amp modeller's shared parts: the oversampler, the triode stage and the
// zero-latency convolver.
#include "engine/rt/RealtimeGuard.h"
#include "plugins/Convolver.h"
#include "plugins/Oversampler.h"
#include "plugins/ToneStack.h"
#include "plugins/Triode.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;

namespace
{
// The amplitude at `hz` of x[from, to) sampled at `rate` (a Hann-windowed DFT bin).
double amplitudeAt(const std::vector<float>& x, double hz, double rate, size_t from, size_t to)
{
    std::complex<double> sum = 0.0;
    double weight = 0.0;
    const double n = static_cast<double>(to - from);
    for (size_t i = from; i < to; ++i)
    {
        const double k = static_cast<double>(i - from);
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * k / n);
        sum += static_cast<double>(x[i]) * w * std::polar(1.0, -2.0 * std::numbers::pi * hz * k / rate);
        weight += w;
    }
    return 2.0 * std::abs(sum) / weight;
}

double db(double x)
{
    return 20.0 * std::log10(std::max(1.0e-12, x));
}

std::vector<float> sine(double hz, double rate, int n, float amplitude = 0.5f)
{
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t>(i)] =
            amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / rate));
    return x;
}

// xorshift noise in -1..1.
std::vector<float> noise(int n, std::uint32_t seed)
{
    std::vector<float> x(static_cast<size_t>(n));
    for (auto& v : x)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        v = static_cast<float>(seed) * (2.0f / 4294967296.0f) - 1.0f;
    }
    return x;
}
} // namespace

TEST_CASE("The oversampler's round trip is flat across the audio band at every factor", "[plugins][amp]")
{
    constexpr double rate = 48000.0;
    for (int factor : {2, 4, 8})
    {
        plugins::Oversampler os;
        os.setFactor(factor);
        REQUIRE(os.factor() == factor);
        for (double hz : {100.0, 1000.0, 10000.0, 18000.0})
        {
            os.reset();
            const auto in = sine(hz, rate, 9600);
            std::vector<float> out(in.size());
            float raised[plugins::Oversampler::maxFactor];
            for (size_t i = 0; i < in.size(); ++i)
            {
                os.up(in[i], raised);
                out[i] = os.down(raised);
            }
            INFO("factor " << factor << ", " << hz << " Hz");
            CHECK(db(amplitudeAt(out, hz, rate, 2400, 9600) / 0.5) == Approx(0.0).margin(0.1));
        }
    }
}

TEST_CASE("The oversampler's round trip adds only a few samples of delay", "[plugins][amp]")
{
    // The group delay at 1 kHz from the phase of the output against the input.
    constexpr double rate = 48000.0, hz = 1000.0;
    plugins::Oversampler os;
    os.setFactor(4);
    const auto in = sine(hz, rate, 4800);
    std::vector<float> out(in.size());
    float raised[plugins::Oversampler::maxFactor];
    for (size_t i = 0; i < in.size(); ++i)
    {
        os.up(in[i], raised);
        out[i] = os.down(raised);
    }
    std::complex<double> a = 0.0, b = 0.0;
    for (size_t i = 2400; i < 4800; ++i)
    {
        const auto e = std::polar(1.0, -2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate);
        a += static_cast<double>(in[i]) * e;
        b += static_cast<double>(out[i]) * e;
    }
    double phase = std::arg(a) - std::arg(b);
    while (phase < 0.0)
        phase += 2.0 * std::numbers::pi;
    const double delaySamples = phase / (2.0 * std::numbers::pi * hz) * rate;
    CHECK(delaySamples < 8.0);
}

TEST_CASE("Upsampling leaves the images of a high tone far below it", "[plugins][amp]")
{
    constexpr double rate = 48000.0, hz = 15000.0;
    for (int factor : {2, 4, 8})
    {
        plugins::Oversampler os;
        os.setFactor(factor);
        const auto in = sine(hz, rate, 8192);
        std::vector<float> raised(in.size() * static_cast<size_t>(factor));
        for (size_t i = 0; i < in.size(); ++i)
            os.up(in[i], raised.data() + i * static_cast<size_t>(factor));
        const double high = rate * factor;
        const size_t from = raised.size() / 4, to = raised.size();
        const double tone = amplitudeAt(raised, hz, high, from, to);
        INFO("factor " << factor);
        CHECK(db(tone / 0.5) == Approx(0.0).margin(0.2));
        // The image of the first stage (base rate minus the tone) and, above 2x,
        // the next stage's (twice the base rate minus it).
        CHECK(db(amplitudeAt(raised, rate - hz, high, from, to) / tone) < -80.0);
        if (factor >= 4)
            CHECK(db(amplitudeAt(raised, 2.0 * rate - hz, high, from, to) / tone) < -60.0);
    }
}

TEST_CASE("A curve table interpolates its points and integrates them exactly", "[plugins][amp]")
{
    plugins::CurveTable t;
    t.build([](double x) { return x * x; }, -2.0, 2.0, 401);
    CHECK(t.value(0.5) == Approx(0.25).margin(1.0e-4));
    CHECK(t.value(5.0) == Approx(4.0)); // holds its end value
    // The integral of x^2 from 0 is x^3 / 3 (the table's from-zero reference).
    CHECK(t.integral(1.5) == Approx(1.125).margin(1.0e-3));
    CHECK(t.integral(-1.0) == Approx(-1.0 / 3.0).margin(1.0e-3));
    // Past the end the integral grows with the held value.
    CHECK(t.integral(3.0) - t.integral(2.0) == Approx(4.0).margin(1.0e-6));
}

TEST_CASE("A 12AX7 stage solved from Koren's model has the gain and operating point of the real circuit",
          "[plugins][amp]")
{
    const auto curve = plugins::solveTriode({});
    // A 100k / 1.5k bypassed 12AX7 stage from 270 V idles near 1 to 1.5 V on the
    // cathode and about 170 V on the plate, with a gain of 50 to 65, inverting.
    CHECK(curve.quiescentCathode > 0.8);
    CHECK(curve.quiescentCathode < 1.8);
    CHECK(curve.quiescentPlate > 140.0);
    CHECK(curve.quiescentPlate < 210.0);
    CHECK(curve.gain < -45.0);
    CHECK(curve.gain > -70.0);
    // Unbypassed, the cathode resistor's feedback lowers the gain.
    plugins::TriodeCircuit unbypassed;
    unbypassed.bypassed = false;
    const auto lower = plugins::solveTriode(unbypassed);
    CHECK(lower.gain > curve.gain);
    CHECK(lower.gain < -25.0);
}

TEST_CASE("A triode stage's curve falls steadily and flattens at both ends", "[plugins][amp]")
{
    const auto curve = plugins::solveTriode({});
    const auto& t = curve.table;
    for (double x = -15.0; x < 15.0; x += 0.25)
        CHECK(t.value(x + 0.25) <= t.value(x) + 1.0e-9);
    const auto slope = [&](double x) { return (t.value(x + 0.01) - t.value(x - 0.01)) / 0.02; };
    // Cutoff below, grid conduction above (the 68k source resistance against the
    // grid current leaves a little slope): both ends a small part of the gain.
    CHECK(std::abs(slope(-6.0)) < 0.05 * std::abs(curve.gain));
    CHECK(std::abs(slope(6.0)) < 0.1 * std::abs(curve.gain));
    // A small signal swings about the same both ways; a big one does not.
    CHECK(std::abs(t.value(0.05) + t.value(-0.05)) < 0.05 * std::abs(t.value(0.05)));
}

TEST_CASE("A triode stage amplifies a quiet tone cleanly and a loud one with harmonics", "[plugins][amp]")
{
    constexpr double rate = 192000.0, hz = 500.0;
    const auto curve = plugins::solveTriode({});
    const auto distortion = [&](float amplitude)
    {
        plugins::TriodeStage stage;
        stage.prepare(curve, rate, 10.0, 40000.0);
        auto x = sine(hz, rate, 96000, amplitude);
        for (auto& v : x)
            v = stage.process(v);
        const double fundamental = amplitudeAt(x, hz, rate, 48000, 96000);
        double harmonics = 0.0;
        for (int h = 2; h <= 8; ++h)
            harmonics += std::pow(amplitudeAt(x, hz * h, rate, 48000, 96000), 2.0);
        return std::make_pair(fundamental / amplitude, std::sqrt(harmonics) / fundamental);
    };
    const auto [quietGain, quietThd] = distortion(0.02f);
    CHECK(quietGain == Approx(std::abs(curve.gain)).epsilon(0.05));
    CHECK(quietThd < 0.01);
    const auto [loudGain, loudThd] = distortion(4.0f);
    CHECK(loudGain < 0.5 * quietGain);
    CHECK(loudThd > 0.1);
}

namespace
{
// The tone stack's output by nodal analysis of the circuit itself, for 1 V in:
// C1 from the input to the treble pot's top (T); the treble pot from T to its
// bottom (B) with the output on its wiper (W); the slope resistor from the input
// to node N; C2 from N to B; C3 from N to M; the bass pot from B to M; the
// middle pot from M to ground.
std::complex<double>
nodalResponse(const plugins::tonestack::Components& c, double t, double m, double l, double hz)
{
    using C = std::complex<double>;
    const C s(0.0, 2.0 * std::numbers::pi * hz);
    const C y1 = s * c.trebleCap, y2 = s * c.bassCap, y3 = s * c.middleCap;
    const C yu = 1.0 / ((1.0 - t) * c.treblePot), yl = 1.0 / (t * c.treblePot);
    const C y4 = 1.0 / c.slope, yb = 1.0 / (l * c.bassPot), ym = 1.0 / (m * c.middlePot);
    enum
    {
        T,
        W,
        B,
        M,
        N
    };
    std::array<std::array<C, 6>, 5> a{}; // [row][col], col 5 = right-hand side
    a[T][T] = y1 + yu;
    a[T][W] = -yu;
    a[T][5] = y1;
    a[W][W] = yu + yl;
    a[W][T] = -yu;
    a[W][B] = -yl;
    a[B][B] = yl + y2 + yb;
    a[B][W] = -yl;
    a[B][N] = -y2;
    a[B][M] = -yb;
    a[M][M] = yb + y3 + ym;
    a[M][B] = -yb;
    a[M][N] = -y3;
    a[N][N] = y4 + y2 + y3;
    a[N][B] = -y2;
    a[N][M] = -y3;
    a[N][5] = y4;
    for (int col = 0; col < 5; ++col)
    {
        int pivot = col;
        for (int r = col + 1; r < 5; ++r)
            if (std::abs(a[static_cast<size_t>(r)][static_cast<size_t>(col)]) >
                std::abs(a[static_cast<size_t>(pivot)][static_cast<size_t>(col)]))
                pivot = r;
        std::swap(a[static_cast<size_t>(col)], a[static_cast<size_t>(pivot)]);
        for (int r = 0; r < 5; ++r)
        {
            if (r == col)
                continue;
            const C f = a[static_cast<size_t>(r)][static_cast<size_t>(col)] /
                        a[static_cast<size_t>(col)][static_cast<size_t>(col)];
            for (int k = col; k < 6; ++k)
                a[static_cast<size_t>(r)][static_cast<size_t>(k)] -=
                    f * a[static_cast<size_t>(col)][static_cast<size_t>(k)];
        }
    }
    return a[W][5] / a[W][W];
}
} // namespace

TEST_CASE("The tone stack's transfer function is the circuit's, at every knob setting", "[plugins][amp]")
{
    const plugins::tonestack::Components c;
    for (double t : {0.05, 0.5, 0.95})
        for (double m : {0.05, 0.5, 0.95})
            for (double l : {0.05, 0.5, 0.95})
            {
                const auto h = plugins::tonestack::analog(c, t, m, l);
                for (double hz : {40.0, 150.0, 500.0, 1500.0, 5000.0, 15000.0})
                {
                    const auto expected = nodalResponse(c, t, m, l, hz);
                    const auto got = plugins::tonestack::response(h, hz);
                    INFO("t " << t << " m " << m << " l " << l << " at " << hz << " Hz");
                    CHECK(std::abs(got - expected) < 1.0e-6 + 1.0e-4 * std::abs(expected));
                }
            }
}

TEST_CASE("The tone stack scoops the mids, and its knobs move the curve as an amp's do", "[plugins][amp]")
{
    const plugins::tonestack::Components c;
    const auto at = [&](double t, double m, double l, double hz)
    { return db(std::abs(plugins::tonestack::response(plugins::tonestack::analog(c, t, m, l), hz))); };
    // Noon on every knob: the mids sit under the lows and highs.
    const double t = plugins::tonestack::audioTaper(0.5), l = plugins::tonestack::audioTaper(0.5);
    CHECK(at(t, 0.5, l, 500.0) < at(t, 0.5, l, 80.0) - 3.0);
    CHECK(at(t, 0.5, l, 500.0) < at(t, 0.5, l, 5000.0) - 3.0);
    // Treble up brightens, bass up fattens, middle up fills the scoop.
    CHECK(at(0.9, 0.5, l, 5000.0) > at(0.1, 0.5, l, 5000.0) + 6.0);
    CHECK(at(t, 0.5, 0.9, 80.0) > at(t, 0.5, 0.1, 80.0) + 6.0);
    CHECK(at(t, 1.0, l, 500.0) > at(t, 0.1, l, 500.0) + 3.0);
    // It is passive: it never gains.
    for (double hz = 20.0; hz < 20000.0; hz *= 1.2)
        CHECK(at(1.0, 1.0, 1.0, hz) < 0.01);
}

TEST_CASE("The digital tone stack matches the analog one across the audio band", "[plugins][amp]")
{
    const plugins::tonestack::Components c;
    const auto h = plugins::tonestack::analog(c, 0.4, 0.6, 0.2);
    plugins::tonestack::Filter f;
    for (double rate : {44100.0, 192000.0})
    {
        f.design(h, rate);
        for (double hz : {50.0, 300.0, 1000.0, 3000.0, 8000.0})
        {
            INFO(rate << " Hz rate, " << hz << " Hz");
            // The bilinear transform warps the top end at the base rate.
            const double margin = rate > 50000.0 ? 0.05 : hz > 5000.0 ? 1.0 : 0.1;
            CHECK(db(f.magnitude(hz, rate)) ==
                  Approx(db(std::abs(plugins::tonestack::response(h, hz)))).margin(margin));
        }
    }
}

TEST_CASE("The convolver equals direct convolution at any block size, with no latency", "[plugins][amp]")
{
    constexpr int length = 1000, inputLength = 6000;
    const auto h = noise(length, 7u);
    const auto hr = noise(length, 99u);
    const auto x = noise(inputLength, 1234u);
    std::vector<double> expectL(inputLength, 0.0), expectR(inputLength, 0.0);
    for (int n = 0; n < inputLength; ++n)
        for (int k = 0; k < length && k <= n; ++k)
        {
            expectL[static_cast<size_t>(n)] +=
                static_cast<double>(h[static_cast<size_t>(k)]) * x[static_cast<size_t>(n - k)];
            expectR[static_cast<size_t>(n)] +=
                static_cast<double>(hr[static_cast<size_t>(k)]) * x[static_cast<size_t>(n - k)];
        }
    const auto kernel = plugins::makeConvolverKernel(h.data(), hr.data(), length, 64);
    REQUIRE(kernel->channels == 2);
    REQUIRE(kernel->partitions == 16);
    for (int blockSize : {1, 17, 64, 256, 1000})
    {
        plugins::Convolver c;
        c.prepare(64, 32);
        std::vector<float> l(inputLength), r(inputLength);
        for (int done = 0; done < inputLength; done += blockSize)
        {
            const int n = std::min(blockSize, inputLength - done);
            c.process(kernel.get(), nullptr, x.data() + done, l.data() + done, r.data() + done, n);
        }
        double worst = 0.0;
        for (int n = 0; n < inputLength; ++n)
        {
            worst = std::max(worst, std::abs(l[static_cast<size_t>(n)] - expectL[static_cast<size_t>(n)]));
            worst = std::max(worst, std::abs(r[static_cast<size_t>(n)] - expectR[static_cast<size_t>(n)]));
        }
        INFO("block " << blockSize);
        CHECK(worst < 2.0e-3);
        // The first output sample already carries the response's first tap.
        CHECK(l[0] == Approx(h[0] * x[0]).margin(1.0e-6));
    }
}

TEST_CASE("A mono kernel feeds both outputs alike", "[plugins][amp]")
{
    const auto h = noise(300, 5u);
    const auto kernel = plugins::makeConvolverKernel(h.data(), h.data(), 300, 64);
    CHECK(kernel->channels == 1);
    plugins::Convolver c;
    c.prepare(64, 8);
    const auto x = noise(2000, 77u);
    std::vector<float> l(2000), r(2000);
    c.process(kernel.get(), nullptr, x.data(), l.data(), r.data(), 2000);
    CHECK(l == r);
}

TEST_CASE("Changing the convolver's kernel crossfades to the new response", "[plugins][amp]")
{
    std::vector<float> a(256, 0.0f), b(256, 0.0f);
    a[0] = 1.0f;  // passes the input
    b[0] = -1.0f; // inverts it
    const auto ka = plugins::makeConvolverKernel(a.data(), nullptr, 256, 64);
    const auto kb = plugins::makeConvolverKernel(b.data(), nullptr, 256, 64);
    plugins::Convolver c;
    c.prepare(64, 8);
    c.setCrossfadeLength(512);
    std::vector<float> x(4096, 0.5f), l(4096), r(4096);
    c.process(ka.get(), nullptr, x.data(), l.data(), r.data(), 1000);
    CHECK(l[999] == Approx(0.5f));
    c.process(kb.get(), ka.get(), x.data() + 1000, l.data() + 1000, r.data() + 1000, 3096);
    // No jump at the change, a steady ramp through zero, then the new response.
    CHECK(std::abs(l[1000] - l[999]) < 0.01f);
    CHECK(std::abs(l[1256]) < 0.01f);
    CHECK(l[1600] == Approx(-0.5f));
    for (int i = 1000; i < 1512; ++i)
        CHECK(l[static_cast<size_t>(i + 1)] <= l[static_cast<size_t>(i)] + 1.0e-6f);
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> partsViolations{0};
void countPartsViolation(const rt::Violation&) noexcept
{
    partsViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("The oversampler, triode stage and convolver run without allocating on the real-time thread",
          "[plugins][amp][rt]")
{
    const auto curve = plugins::solveTriode({});
    plugins::TriodeStage stage;
    stage.prepare(curve, 192000.0, 10.0, 40000.0);
    plugins::Oversampler os;
    os.setFactor(4);
    const auto h = noise(2048, 3u), h2 = noise(2048, 4u);
    const auto ka = plugins::makeConvolverKernel(h.data(), nullptr, 2048, 64);
    const auto kb = plugins::makeConvolverKernel(h.data(), h2.data(), 2048, 64);
    plugins::Convolver c;
    c.prepare(64, 32);
    std::vector<float> x(512, 0.3f), l(512), r(512);
    const auto previous = rt::setViolationHandler(&countPartsViolation);
    partsViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        float raised[plugins::Oversampler::maxFactor];
        for (int block = 0; block < 20; ++block)
        {
            for (auto& v : x)
            {
                os.up(v, raised);
                for (int i = 0; i < 4; ++i)
                    raised[i] = stage.process(raised[i]);
                v = os.down(raised) * 0.01f;
            }
            c.process(block < 10 ? ka.get() : kb.get(), ka.get(), x.data(), l.data(), r.data(), 512);
        }
    }
    rt::setViolationHandler(previous);
    CHECK(partsViolations == 0);
}
#endif
