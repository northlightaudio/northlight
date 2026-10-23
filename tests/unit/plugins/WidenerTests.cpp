#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/Widener.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;
namespace wd = plugins::widener;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

std::unique_ptr<engine::InsertProcessor> makeWidener()
{
    auto p = plugins::BuiltInFactory().create(wd::type);
    REQUIRE(p != nullptr);
    p->prepare(rate, block);
    p->setBypassed(false);
    return p;
}

// A repeatable white noise in -1..1.
struct Noise
{
    std::uint32_t state;
    explicit Noise(std::uint32_t seed) : state(seed) {}
    float next()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 23) - 1.0f;
    }
};

// What came out over the last `measureSeconds` of `seconds` of a source.
struct Result
{
    double midSquare = 0.0, sideSquare = 0.0, leftSquare = 0.0, rightSquare = 0.0, product = 0.0;
    std::vector<float> left, right;
    double midRms() const { return std::sqrt(midSquare); }
    double sideRms() const { return std::sqrt(sideSquare); }
    double power() const { return leftSquare + rightSquare; }
    double correlation() const { return product / std::sqrt(leftSquare * rightSquare); }
};

using Source = std::function<void(int index, float& left, float& right)>;

Result play(engine::InsertProcessor& p, const Source& source, double seconds, double measureSeconds = 0.5)
{
    std::vector<float> l(block), r(block);
    Result result;
    const int total = static_cast<int>(seconds * rate);
    const int from = total - static_cast<int>(measureSeconds * rate);
    int n = 0;
    int index = 0;
    for (int done = 0; done < total; done += block)
    {
        for (int i = 0; i < block; ++i)
            source(index++, l[static_cast<size_t>(i)], r[static_cast<size_t>(i)]);
        p.process(l.data(), r.data(), block);
        for (int i = 0; i < block; ++i)
        {
            if (done + i < from)
                continue;
            const double a = l[static_cast<size_t>(i)], b = r[static_cast<size_t>(i)];
            result.midSquare += 0.25 * (a + b) * (a + b);
            result.sideSquare += 0.25 * (a - b) * (a - b);
            result.leftSquare += a * a;
            result.rightSquare += b * b;
            result.product += a * b;
            result.left.push_back(static_cast<float>(a));
            result.right.push_back(static_cast<float>(b));
            ++n;
        }
    }
    for (auto* v :
         {&result.midSquare, &result.sideSquare, &result.leftSquare, &result.rightSquare, &result.product})
        *v /= std::max(1, n);
    return result;
}

Source sine(double hz, float left, float right)
{
    return [hz, left, right](int index, float& l, float& r)
    {
        const auto v = static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * index / rate));
        l = left * v;
        r = right * v;
    };
}

// Independent noise on each side: a wide, uncorrelated stereo source.
Source stereoNoise(float level = 0.25f)
{
    auto a = std::make_shared<Noise>(11u), b = std::make_shared<Noise>(29u);
    return [a, b, level](int, float& l, float& r)
    {
        l = level * a->next();
        r = level * b->next();
    };
}

Source monoNoise(float level = 0.25f)
{
    auto a = std::make_shared<Noise>(7u);
    return [a, level](int, float& l, float& r) { l = r = level * a->next(); };
}

double correlationOf(const std::vector<float>& a, const std::vector<float>& b)
{
    double ab = 0.0, aa = 0.0, bb = 0.0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
    {
        ab += static_cast<double>(a[i]) * b[i];
        aa += static_cast<double>(a[i]) * a[i];
        bb += static_cast<double>(b[i]) * b[i];
    }
    return ab / std::sqrt(aa * bb);
}

std::vector<float> monoSum(const Result& r)
{
    std::vector<float> sum(r.left.size());
    for (size_t i = 0; i < sum.size(); ++i)
        sum[i] = r.left[i] + r.right[i];
    return sum;
}

double db(double ratio)
{
    return 20.0 * std::log10(ratio);
}
} // namespace

TEST_CASE("The widener at its defaults leaves the level, the image and the mono sum as they were",
          "[plugins][widener]")
{
    for (const double hz : {60.0, 400.0, 2500.0, 9000.0})
    {
        auto p = makeWidener();
        const auto in = sine(hz, 0.5f, 0.2f); // a source panned left of centre
        const auto out = play(*p, in, 0.6, 0.3);
        // The input's mid is 0.35 and its side 0.15 (as sines: RMS / sqrt 2).
        CHECK(out.midRms() == Approx(0.35 / std::sqrt(2.0)).epsilon(0.01));
        CHECK(out.sideRms() == Approx(0.15 / std::sqrt(2.0)).epsilon(0.01));
        CHECK(out.correlation() == Approx(1.0).margin(1e-3)); // still one panned source
    }
    // Noise: the same power in and out, the same (no) correlation.
    auto p = makeWidener();
    const auto out = play(*p, stereoNoise(), 2.0, 1.0);
    CHECK(db(out.power() / (2.0 * 0.25 * 0.25 / 3.0)) == Approx(0.0).margin(0.2));
    CHECK(std::abs(out.correlation()) < 0.05);
}

TEST_CASE("Each band's width scales its own side: lows to mono while highs open up", "[plugins][widener]")
{
    const auto ratioAt = [](double hz, float low, float mid, float high)
    {
        auto p = makeWidener();
        p->setParameter(wd::lowWidth, low);
        p->setParameter(wd::midWidth, mid);
        p->setParameter(wd::highWidth, high);
        // A hard-left sine: mid and side equal going in.
        const auto out = play(*p, sine(hz, 0.5f, 0.0f), 0.8, 0.3);
        return out.sideRms() / out.midRms();
    };
    CHECK(ratioAt(60.0, 0.0f, 100.0f, 200.0f) < 0.03);
    CHECK(ratioAt(1000.0, 0.0f, 100.0f, 200.0f) == Approx(1.0).epsilon(0.03));
    CHECK(ratioAt(12000.0, 0.0f, 100.0f, 200.0f) == Approx(2.0).epsilon(0.03));
    // Across a crossover the bands sum back: equal widths are flat there too.
    CHECK(ratioAt(250.0, 150.0f, 150.0f, 150.0f) == Approx(1.5).epsilon(0.02));
    CHECK(ratioAt(4000.0, 50.0f, 50.0f, 50.0f) == Approx(0.5).epsilon(0.02));
}

TEST_CASE("Mono below takes the side out of the bass only", "[plugins][widener]")
{
    const auto ratioAt = [](double hz)
    {
        auto p = makeWidener();
        p->setParameter(wd::monoBelow, 300.0f);
        const auto out = play(*p, sine(hz, 0.5f, 0.0f), 0.8, 0.3);
        return out.sideRms() / out.midRms();
    };
    CHECK(ratioAt(50.0) < 0.02);
    CHECK(ratioAt(3000.0) == Approx(1.0).epsilon(0.02));
    CHECK(wd::monoOffHz < wd::maxMonoBelowHz);
}

TEST_CASE("Spread widens a mono source and cancels in the mono sum, in both modes", "[plugins][widener]")
{
    auto reference = makeWidener();
    const auto plain = play(*reference, monoNoise(), 2.0, 1.0);
    CHECK(plain.correlation() == Approx(1.0).margin(1e-4));
    for (const int mode : {wd::diffuse, wd::haas})
    {
        auto p = makeWidener();
        p->setParameter(wd::spread, 100.0f);
        p->setParameter(wd::spreadMode, static_cast<float>(mode));
        const auto out = play(*p, monoNoise(), 2.0, 1.0);
        // Wider: the two sides no longer move together.
        CHECK(out.correlation() < 0.75);
        CHECK(out.correlation() > 0.2); // not out of phase
        // The mono sum is the mid as it was, only scaled by the level compensation.
        CHECK(correlationOf(monoSum(out), monoSum(plain)) > 0.999);
        // And the loudness holds.
        CHECK(db(out.power() / plain.power()) == Approx(0.0).margin(0.5));
    }
}

TEST_CASE("The level holds as the width changes", "[plugins][widener]")
{
    auto reference = makeWidener();
    const double neutral = play(*reference, stereoNoise(), 2.0, 1.0).power();
    for (const float width : {0.0f, 50.0f, 150.0f, 200.0f})
    {
        auto p = makeWidener();
        for (int band : {wd::lowWidth, wd::midWidth, wd::highWidth})
            p->setParameter(band, width);
        const auto out = play(*p, stereoNoise(), 2.0, 1.0);
        CHECK(db(out.power() / neutral) == Approx(0.0).margin(0.5));
        const double expected = width / 100.0;
        CHECK(out.sideRms() / out.midRms() == Approx(expected).margin(0.05));
    }
}

TEST_CASE("The guard narrows an image that goes out of phase and reports how far", "[plugins][widener]")
{
    const auto run = [](bool guard)
    {
        auto p = makeWidener();
        for (int band : {wd::lowWidth, wd::midWidth, wd::highWidth})
            p->setParameter(band, 200.0f);
        p->setParameter(wd::guard, guard ? 1.0f : 0.0f);
        p->setParameter(wd::guardAt, 0.0f);
        const auto out = play(*p, stereoNoise(), 2.0, 1.0);
        return std::pair{out.correlation(), p->feedback()->latest()};
    };
    const auto [open, openFrame] = run(false);
    const auto [guarded, guardedFrame] = run(true);
    CHECK(open < -0.45); // (1 - 4) / (1 + 4)
    CHECK(guarded > -0.08);
    CHECK(openFrame.reduction == Approx(0.0f).margin(1e-3f));
    CHECK(guardedFrame.reduction > 30.0f); // to about half the side: 50%
    // The frame's correlation is the output's.
    const auto shown = wd::decodeCorrelation(guardedFrame.extra);
    REQUIRE(shown.has_value());
    CHECK(*shown == Approx(guarded).margin(0.12));

    CHECK(wd::guardGainFor(1.0f, 1.0f, 0.0f) == Approx(1.0f));
    CHECK(wd::guardGainFor(1.0f, 4.0f, 0.0f) == Approx(0.5f));
    CHECK(wd::guardGainFor(1.0f, 0.0f, 0.0f) == 1.0f);
    CHECK(wd::guardGainFor(0.0f, 1.0f, 0.0f) == Approx(0.25f)); // its floor: never mute
}

TEST_CASE("Rotation turns the image and balance tilts it", "[plugins][widener]")
{
    auto p = makeWidener();
    p->setParameter(wd::rotation, 45.0f);
    auto out = play(*p, sine(1000.0, 0.5f, 0.5f), 0.5, 0.2);
    CHECK(std::sqrt(out.leftSquare) < 0.005);
    CHECK(std::sqrt(out.rightSquare) == Approx(0.5).epsilon(0.02)); // sqrt 2 * 0.5 / sqrt 2

    auto q = makeWidener();
    q->setParameter(wd::balance, -100.0f);
    out = play(*q, sine(1000.0, 0.5f, 0.5f), 0.5, 0.2);
    CHECK(std::sqrt(out.rightSquare) < 1e-4);
    CHECK(std::sqrt(out.leftSquare) == Approx(0.5 / std::sqrt(2.0)).epsilon(0.02));

    auto r = makeWidener();
    r->setParameter(wd::output, -6.0f);
    out = play(*r, sine(1000.0, 0.5f, 0.5f), 0.5, 0.2);
    CHECK(db(std::sqrt(out.leftSquare) / (0.5 / std::sqrt(2.0))) == Approx(-6.0).margin(0.1));
}

TEST_CASE("The widener publishes the correlation and its output's two channels", "[plugins][widener]")
{
    auto p = makeWidener();
    p->feedback()->setTapsEnabled(true);
    play(*p, sine(500.0, 0.5f, 0.5f), 0.5);
    auto shown = wd::decodeCorrelation(p->feedback()->latest().extra);
    REQUIRE(shown.has_value());
    CHECK(*shown == Approx(1.0f).margin(0.01f));
    play(*p, sine(500.0, 0.5f, -0.5f), 3.0);
    shown = wd::decodeCorrelation(p->feedback()->latest().extra);
    REQUIRE(shown.has_value());
    CHECK(*shown == Approx(-1.0f).margin(0.01f));
    const auto input = wd::decodeCorrelation(p->feedback()->latest().extra2);
    REQUIRE(input.has_value());
    CHECK(*input == Approx(-1.0f).margin(0.01f));

    // The taps: tap 0 is the left channel, tap 1 the right, sample for sample.
    std::vector<float> l(block), r(block);
    for (int i = 0; i < block; ++i)
    {
        l[static_cast<size_t>(i)] = 0.3f;
        r[static_cast<size_t>(i)] = -0.1f;
    }
    for (int b = 0; b < 40; ++b)
    {
        std::vector<float> a = l, c = r;
        p->process(a.data(), c.data(), block);
        if (b == 39)
        {
            std::vector<float> tapL(block), tapR(block);
            REQUIRE(p->feedback()->readTap(0, tapL.data(), block));
            REQUIRE(p->feedback()->readTap(1, tapR.data(), block));
            CHECK(tapL == a);
            CHECK(tapR == c);
        }
    }

    // Silence reads as no correlation at all.
    play(*p, sine(500.0, 0.0f, 0.0f), 3.0);
    CHECK_FALSE(wd::decodeCorrelation(p->feedback()->latest().extra).has_value());
}

TEST_CASE("The widener's controls move without steps", "[plugins][widener]")
{
    // A width jump on a steady side: no sample-to-sample step bigger than the sine's own.
    auto p = makeWidener();
    const auto source = sine(200.0, 0.5f, 0.0f);
    play(*p, source, 0.3, 0.0); // 57 blocks
    p->setParameter(wd::midWidth, 0.0f);
    p->setParameter(wd::lowWidth, 200.0f);
    p->setParameter(wd::lowSplit, 1000.0f);
    p->setParameter(wd::rotation, -30.0f);
    p->setParameter(wd::spreadMode, static_cast<float>(wd::haas));
    p->setParameter(wd::spread, 80.0f);
    const auto out = play(*p, [&](int i, float& l, float& r) { source(i + 57 * block, l, r); }, 0.1, 0.1);
    const double sineStep = 0.5 * 2.0 * std::numbers::pi * 200.0 / rate * 2.5; // with headroom
    double largest = 0.0;
    for (size_t i = 1; i < out.left.size(); ++i)
        largest = std::max({largest,
                            std::abs(static_cast<double>(out.left[i] - out.left[i - 1])),
                            std::abs(static_cast<double>(out.right[i] - out.right[i - 1]))});
    CHECK(largest < sineStep);
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> widenerViolations{0};
void countWidenerViolation(const rt::Violation&) noexcept
{
    widenerViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("The widener processes without allocating on the real-time thread", "[plugins][widener][rt]")
{
    auto p = makeWidener();
    p->feedback()->setTapsEnabled(true);
    p->feedback()->setWatched(true);
    p->setParameter(wd::spread, 60.0f);
    p->setParameter(wd::guard, 1.0f);
    p->setParameter(wd::monoBelow, 120.0f);
    p->setParameter(wd::rotation, 10.0f);
    std::vector<float> l(block, 0.4f), r(block, -0.3f);
    const auto previous = rt::setViolationHandler(&countWidenerViolation);
    widenerViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int i = 0; i < 100; ++i)
        {
            if (i == 50)
            {
                p->setParameter(wd::spreadMode, static_cast<float>(wd::haas));
                p->setParameter(wd::lowSplit, 600.0f);
                p->setBypassed(true);
            }
            p->process(l.data(), r.data(), block);
        }
    }
    rt::setViolationHandler(previous);
    CHECK(widenerViolations == 0);
}
#endif
