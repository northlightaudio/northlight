#include "SyntheticSinger.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/BuiltIns.h"
#include "plugins/Vesper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;
using testing::Singer;
namespace vesper = plugins::vesper;

namespace
{
constexpr int block = 256;

// Vesper as the tests want it unless they say otherwise: the tail alone, and
// none of the singing-driven shaping, so each test turns on what it measures.
std::unique_ptr<engine::InsertProcessor> makeVesper(double rate = 48000.0)
{
    auto p = plugins::BuiltInFactory().create(vesper::type);
    REQUIRE(p != nullptr);
    p->prepare(rate, block);
    p->setBypassed(false);
    p->setParameter(vesper::mix, 100.0f);
    p->setParameter(vesper::duck, 0.0f);
    p->setParameter(vesper::ends, 0.0f);
    p->setParameter(vesper::clarity, 0.0f);
    p->setParameter(vesper::breaths, 0.0f);
    p->setParameter(vesper::mud, 0.0f);
    p->setParameter(vesper::follow, vesper::minFollowOctaves); // off
    p->setParameter(vesper::releaseSync, 0.0f);
    p->setParameter(vesper::release, 100.0f);
    return p;
}

struct Played
{
    std::vector<float> left, right;
};

// A mono signal through the processor (the same on both sides), then `tail`
// seconds of silence.
Played play(engine::InsertProcessor& p, const std::vector<float>& mono, double rate, double tail = 0.0)
{
    std::vector<float> in = mono;
    in.resize(in.size() + static_cast<size_t>(tail * rate), 0.0f);
    in.resize((in.size() + block - 1) / block * block, 0.0f);
    Played out;
    out.left.reserve(in.size());
    out.right.reserve(in.size());
    std::vector<float> l(block), r(block);
    for (size_t at = 0; at < in.size(); at += block)
    {
        std::copy(in.begin() + static_cast<std::ptrdiff_t>(at),
                  in.begin() + static_cast<std::ptrdiff_t>(at + block),
                  l.begin());
        r = l;
        p.process(l.data(), r.data(), block);
        out.left.insert(out.left.end(), l.begin(), l.end());
        out.right.insert(out.right.end(), r.begin(), r.end());
    }
    return out;
}

double rmsOf(const std::vector<float>& x, double rate, double from, double to)
{
    const auto a = std::min(x.size(), static_cast<size_t>(from * rate));
    const auto b = std::min(x.size(), static_cast<size_t>(to * rate));
    double sum = 0.0;
    for (size_t i = a; i < b; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return b > a ? std::sqrt(sum / static_cast<double>(b - a)) : 0.0;
}

double db(double x)
{
    return 20.0 * std::log10(std::max(1.0e-12, x));
}

std::vector<float> noise(double seconds, double rate, float amplitude, std::uint32_t seed = 5u)
{
    std::vector<float> v(static_cast<size_t>(seconds * rate));
    for (auto& x : v)
    {
        seed = seed * 1664525u + 1013904223u;
        x = amplitude * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
    }
    return v;
}
} // namespace

TEST_CASE("Vesper's helpers: Auto pre-delay, the pre-delay's sources and Follow's cutoff",
          "[plugins][vesper]")
{
    CHECK(vesper::autoPreDelayMs(0.0f) == 60.0f);
    CHECK(vesper::autoPreDelayMs(1.0f) == Approx(89.0f));
    CHECK(vesper::autoPreDelayMs(5.0f) == Approx(25.0f));
    CHECK(vesper::autoPreDelayMs(2.0f) > vesper::autoPreDelayMs(3.0f));
    // Auto wins, then a note value, then the time.
    CHECK(vesper::preDelaySeconds(100.0f, 1.0f, 11.0f, 5.0f, 120.0) == Approx(0.025));
    CHECK(vesper::preDelaySeconds(100.0f, 0.0f, 11.0f, 5.0f, 120.0) == Approx(0.5)); // a quarter at 120
    CHECK(vesper::preDelaySeconds(100.0f, 0.0f, 0.0f, 5.0f, 120.0) == Approx(0.1));
    CHECK(vesper::preDelaySeconds(100.0f, 0.0f, 16.0f, 5.0f, 30.0) == Approx(2.0)); // held
    CHECK(vesper::followCutoffHz(440.0f, 1.0f) == Approx(220.0f));
    CHECK(vesper::followCutoffHz(440.0f, 2.0f) == Approx(110.0f));
    CHECK(vesper::followCutoffHz(440.0f, vesper::followOffOctaves) == 0.0f);
    CHECK(vesper::followCutoffHz(0.0f, 1.0f) == 0.0f);
}

TEST_CASE("Vesper's spaces each decay over the decay time at a sane level, at any rate", "[plugins][vesper]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
    {
        double plateLevel = 0.0;
        for (int space = 0; space < 3; ++space)
        {
            INFO("rate " << rate << ", space " << space);
            auto p = makeVesper(rate);
            p->setParameter(vesper::space, static_cast<float>(space));
            p->setParameter(vesper::decay, 2.0f);
            p->setParameter(vesper::damping, 20000.0f);
            p->setParameter(vesper::lowCut, 20.0f);
            const auto out = play(*p, noise(0.5, rate, 0.3f), rate, 2.0);
            const double fall = db(rmsOf(out.left, rate, 0.9, 1.1)) - db(rmsOf(out.left, rate, 1.6, 1.8));
            CHECK(0.7 * 60.0 / fall == Approx(2.0).epsilon(0.25));
            const double level = rmsOf(out.left, rate, 0.2, 0.5);
            CHECK(db(level) > -40.0);
            CHECK(db(level) < 0.0);
            if (space == 0)
                plateLevel = level;
            else
                CHECK(std::abs(db(level) - db(plateLevel)) < 6.0);
        }
    }
}

TEST_CASE("Vesper's dry signal is untouched at no mix", "[plugins][vesper]")
{
    auto p = makeVesper();
    p->setParameter(vesper::mix, 0.0f);
    p->setParameter(vesper::clarity, 100.0f);
    p->setParameter(vesper::duck, 18.0f);
    Singer singer(48000.0);
    singer.vowel(0.4, 220.0f).s(0.15).vowel(0.3, 247.0f);
    const auto out = play(*p, singer.audio(), 48000.0);
    for (size_t i = 0; i < singer.audio().size(); ++i)
        REQUIRE(out.left[i] == singer.audio()[i]);
}

TEST_CASE("Vesper's Clarity keeps an s out of the reverb", "[plugins][vesper]")
{
    const auto tailOfS = [](float clarity)
    {
        auto p = makeVesper();
        p->setParameter(vesper::clarity, clarity);
        p->setParameter(vesper::damping, 20000.0f);
        p->setParameter(vesper::preDelayAuto, 0.0f);
        p->setParameter(vesper::preDelay, 0.0f);
        Singer singer(48000.0);
        singer.rest(0.1).s(0.15);
        const auto out = play(*p, singer.audio(), 48000.0, 0.3);
        return rmsOf(out.left, 48000.0, 0.1, 0.45);
    };
    // The first few milliseconds of an s reach the reverb before the Listener
    // has heard it (lookahead, in a later phase, closes that).
    CHECK(db(tailOfS(100.0f)) < db(tailOfS(0.0f)) - 12.0);
}

TEST_CASE("Vesper's Duck lowers the tail while the singer sings and lets it back", "[plugins][vesper]")
{
    const auto run = [](float duck)
    {
        auto p = makeVesper();
        p->setParameter(vesper::duck, duck);
        p->setParameter(vesper::decay, 3.0f);
        Singer singer(48000.0);
        singer.rest(0.1).vowel(1.5, 220.0f);
        return play(*p, singer.audio(), 48000.0, 1.0);
    };
    const auto plain = run(0.0f), ducked = run(6.0f);
    const double held = db(rmsOf(plain.left, 48000.0, 1.0, 1.5)) - db(rmsOf(ducked.left, 48000.0, 1.0, 1.5));
    CHECK(held == Approx(6.0).margin(1.0));
    // Released over 100 ms once the voice stops.
    const double after = db(rmsOf(plain.left, 48000.0, 2.0, 2.3)) - db(rmsOf(ducked.left, 48000.0, 2.0, 2.3));
    CHECK(std::abs(after) < 1.0);
}

TEST_CASE("Vesper's Ends lifts and lengthens the tail after a line", "[plugins][vesper]")
{
    const auto run = [](float ends)
    {
        auto p = makeVesper();
        p->setParameter(vesper::ends, ends);
        p->setParameter(vesper::decay, 1.5f);
        Singer singer(48000.0);
        singer.rest(0.1).vowel(0.8, 220.0f);
        return play(*p, singer.audio(), 48000.0, 2.0);
    };
    const auto plain = run(0.0f), lifted = run(100.0f);
    // The line ends 250 ms (the gap) after the voice stops.
    CHECK(db(rmsOf(lifted.left, 48000.0, 1.3, 1.6)) > db(rmsOf(plain.left, 48000.0, 1.3, 1.6)) + 4.0);
    CHECK(db(rmsOf(lifted.left, 48000.0, 2.0, 2.4)) > db(rmsOf(plain.left, 48000.0, 2.0, 2.4)) + 8.0);
}

TEST_CASE("Vesper's Follow clears the tail under the sung note", "[plugins][vesper]")
{
    const auto lowsOfTail = [](float follow)
    {
        auto p = makeVesper();
        p->setParameter(vesper::follow, follow);
        p->setParameter(vesper::lowCut, 20.0f);
        Singer singer(48000.0);
        singer.rest(0.1).vowel(1.5, 440.0f);
        auto in = singer.audio();
        // A 60 Hz hum under the voice.
        for (size_t i = 0; i < in.size(); ++i)
            in[i] += 0.03f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 60.0 * i / 48000.0));
        const auto out = play(*p, in, 48000.0);
        plugins::Biquad a, b;
        a.setLowPass(48000.0, 90.0);
        b.setLowPass(48000.0, 90.0);
        std::vector<float> lows(out.left.size());
        for (size_t i = 0; i < lows.size(); ++i)
            lows[i] = b.process(a.process(out.left[i]));
        return rmsOf(lows, 48000.0, 0.8, 1.5);
    };
    CHECK(db(lowsOfTail(1.0f)) < db(lowsOfTail(vesper::minFollowOctaves)) - 12.0);
}

TEST_CASE("Vesper's Centre takes the tail's middle down and leaves its sides", "[plugins][vesper]")
{
    const auto midAndSide = [](float centre)
    {
        auto p = makeVesper();
        p->setParameter(vesper::centre, centre);
        const auto out = play(*p, noise(0.6, 48000.0, 0.3f), 48000.0, 0.5);
        std::vector<float> mid(out.left.size()), side(out.left.size());
        for (size_t i = 0; i < mid.size(); ++i)
        {
            mid[i] = 0.5f * (out.left[i] + out.right[i]);
            side[i] = 0.5f * (out.left[i] - out.right[i]);
        }
        return std::pair{rmsOf(mid, 48000.0, 0.3, 0.9), rmsOf(side, 48000.0, 0.3, 0.9)};
    };
    const auto open = midAndSide(0.0f), cleared = midAndSide(100.0f);
    CHECK(db(cleared.first) < db(open.first) - 12.0);
    CHECK(db(cleared.second) == Approx(db(open.second)).margin(0.5));
}

TEST_CASE("Vesper's Mud cuts a build-up at 250-500 Hz in the tail", "[plugins][vesper]")
{
    const auto bandOfTail = [](float mud)
    {
        auto p = makeVesper();
        p->setParameter(vesper::mud, mud);
        p->setParameter(vesper::decay, 3.0f);
        auto in = noise(1.5, 48000.0, 0.5f);
        plugins::Biquad lp;
        lp.setLowPass(48000.0, 450.0);
        for (auto& x : in)
            x = lp.process(x);
        const auto out = play(*p, in, 48000.0);
        plugins::Biquad band;
        band.setBandPass(48000.0, 350.0, 1.0);
        std::vector<float> b(out.left.size());
        for (size_t i = 0; i < b.size(); ++i)
            b[i] = band.process(out.left[i]);
        return rmsOf(b, 48000.0, 0.8, 1.5);
    };
    CHECK(db(bandOfTail(100.0f)) < db(bandOfTail(0.0f)) - 2.0);
}

TEST_CASE("Vesper's pre-delay holds the tail back", "[plugins][vesper]")
{
    auto p = makeVesper();
    p->setParameter(vesper::preDelayAuto, 0.0f);
    p->setParameter(vesper::preDelay, 100.0f);
    std::vector<float> click(4800, 0.0f);
    click[0] = 0.8f;
    const auto out = play(*p, click, 48000.0, 0.2);
    size_t first = 0;
    while (first < out.left.size() && std::abs(out.left[first]) < 1.0e-6f)
        ++first;
    CHECK(static_cast<double>(first) / 48000.0 == Approx(0.1).margin(0.003));
}

TEST_CASE("Vesper changes space without a gap: the old tail rings on as the new builds", "[plugins][vesper]")
{
    auto p = makeVesper();
    p->setParameter(vesper::decay, 3.0f);
    const auto before = play(*p, noise(0.5, 48000.0, 0.3f), 48000.0);
    const double level = rmsOf(before.left, 48000.0, 0.4, 0.5);
    p->setParameter(vesper::space, 2.0f);
    const auto after = play(*p, {}, 48000.0, 0.3);
    for (const float x : after.left)
        REQUIRE(std::isfinite(x));
    CHECK(db(rmsOf(after.left, 48000.0, 0.0, 0.05)) > db(level) - 6.0);
}

TEST_CASE("Vesper falls silent after its tail and stays clean", "[plugins][vesper]")
{
    auto p = makeVesper();
    p->setParameter(vesper::decay, 0.5f);
    Singer singer(48000.0);
    singer.vowel(0.5, 220.0f);
    const auto out = play(*p, singer.audio(), 48000.0, 6.0);
    for (const float x : out.left)
        REQUIRE(std::isfinite(x));
    CHECK(rmsOf(out.left, 48000.0, 5.5, 6.4) == 0.0);
}

// A benchmark, hidden from the default run (northlight_unit_tests "[.bench]"): what
// Vesper costs in percent of one core at 48 kHz with 256-sample blocks, on singing,
// with everything the Listener drives turned on.
TEST_CASE("Vesper cost", "[.bench][vesper]")
{
    auto p = makeVesper();
    p->setParameter(vesper::clarity, 60.0f);
    p->setParameter(vesper::duck, 6.0f);
    p->setParameter(vesper::ends, 40.0f);
    p->setParameter(vesper::mud, 30.0f);
    p->setParameter(vesper::follow, 1.0f);
    p->setParameter(vesper::space, 2.0f);
    Singer singer(48000.0);
    while (singer.now() < 10.0)
        singer.vowel(0.4, 220.0f).s(0.1).vowel(0.3, 262.0f).rest(0.3).breath(0.2, -38.0f);
    const auto start = std::chrono::steady_clock::now();
    play(*p, singer.audio(), 48000.0);
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double percent = 100.0 * elapsed / singer.now();
    std::printf("Vesper: %.3f%% of a core\n", percent);
    CHECK(percent < 10.0); // generous: a debug build is slow
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> vesperViolations{0};
void countVesperViolation(const rt::Violation&) noexcept
{
    vesperViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Vesper processes without allocating on the real-time thread", "[plugins][vesper][rt]")
{
    auto p = makeVesper();
    p->feedback()->setWatched(true);
    p->feedback()->setTapsEnabled(true);
    p->setParameter(vesper::clarity, 80.0f);
    p->setParameter(vesper::duck, 9.0f);
    p->setParameter(vesper::ends, 60.0f);
    p->setParameter(vesper::mud, 50.0f);
    p->setParameter(vesper::follow, 1.0f);
    p->setParameter(vesper::releaseSync, 8.0f);
    Singer singer(48000.0);
    singer.vowel(0.4, 220.0f).s(0.1).rest(0.4).breath(0.2).vowel(0.3, 330.0f).rest(0.5);
    const auto& in = singer.audio();
    std::vector<float> l(block), r(block);
    const auto previous = rt::setViolationHandler(&countVesperViolation);
    vesperViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        int n = 0;
        for (size_t at = 0; at + block <= in.size(); at += block, ++n)
        {
            std::copy(in.begin() + static_cast<std::ptrdiff_t>(at),
                      in.begin() + static_cast<std::ptrdiff_t>(at + block),
                      l.begin());
            std::copy(l.begin(), l.end(), r.begin());
            if (n == 60)
                p->setParameter(vesper::space, 1.0f);
            if (n == 120)
                p->setBypassed(true);
            p->process(l.data(), r.data(), block);
        }
    }
    rt::setViolationHandler(previous);
    CHECK(vesperViolations == 0);
}
#endif
