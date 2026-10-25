#include "SyntheticSinger.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/BuiltIns.h"
#include "plugins/EffectPresets.h"
#include "plugins/Vesper.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
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
// The level of one frequency in `x` between two times (a Hann-windowed DFT bin).
double levelAt(const std::vector<float>& x, double rate, double hz, double from, double to)
{
    const auto a = std::min(x.size(), static_cast<size_t>(from * rate));
    const auto b = std::min(x.size(), static_cast<size_t>(to * rate));
    double re = 0.0, im = 0.0, weights = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        const double t = static_cast<double>(i - a) / static_cast<double>(b - a);
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * t);
        const double phase = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate;
        re += w * x[i] * std::cos(phase);
        im += w * x[i] * std::sin(phase);
        weights += w;
    }
    return weights > 0.0 ? 2.0 * std::sqrt(re * re + im * im) / weights : 0.0;
}

// The tail after a phrase, at Halo off and at `halo`: how much Halo lifts each
// frequency in it, in dB.
std::vector<double> haloLift(const Singer& singer, float halo, const std::vector<double>& hz)
{
    const auto tailOf = [&](float amount)
    {
        auto p = makeVesper();
        p->setParameter(vesper::halo, amount);
        return play(*p, singer.audio(), 48000.0, 1.5).left;
    };
    const auto off = tailOf(0.0f), on = tailOf(halo);
    const double end = singer.now();
    std::vector<double> lift;
    for (const double f : hz)
        lift.push_back(db(levelAt(on, 48000.0, f, end + 0.3, end + 1.2)) -
                       db(levelAt(off, 48000.0, f, end + 0.3, end + 1.2)));
    return lift;
}
} // namespace

TEST_CASE("Halo rings the note sung in the tail, and not the notes beside it", "[plugins][vesper]")
{
    Singer singer(48000.0);
    singer.rest(0.1).vowel(1.0, 220.0f);
    // 220 Hz and its octave, then a semitone above and a tritone between.
    const auto lift = haloLift(singer, 100.0f, {220.0, 440.0, 233.1, 311.1});
    CHECK(lift[0] > 6.0);
    CHECK(lift[1] > 6.0);
    CHECK(lift[0] > lift[2] + 6.0);
    CHECK(lift[0] > lift[3] + 6.0);
}

TEST_CASE("Halo holds two notes sung one after the other", "[plugins][vesper]")
{
    Singer singer(48000.0);
    singer.rest(0.1).vowel(0.6, 220.0f).rest(0.05).vowel(0.6, 330.0f);
    // The second note has a fresh tail of its own, so Halo lifts it less.
    const auto lift = haloLift(singer, 100.0f, {220.0, 330.0, 277.2});
    CHECK(lift[0] > 6.0);
    CHECK(lift[1] > 3.0);
    CHECK(lift[0] > lift[2] + 3.0);
    CHECK(lift[1] > lift[2] + 3.0);
}

TEST_CASE("Halo tunes a note sung with a wide vibrato to its centre", "[plugins][vesper]")
{
    // An "ah" at 220 Hz with a 5.5 Hz vibrato of 60 cents either way.
    constexpr double rate = 48000.0;
    Singer singer(rate);
    singer.rest(0.1);
    std::vector<float> in = singer.audio();
    const auto length = static_cast<size_t>(1.2 * rate);
    double phase = 0.0;
    for (size_t n = 0; n < length; ++n)
    {
        const double t = static_cast<double>(n) / rate;
        phase += 2.0 * std::numbers::pi * 220.0 *
                 std::exp2(0.6 / 12.0 * std::sin(2.0 * std::numbers::pi * 5.5 * t)) / rate;
        double x = 0.0;
        for (int k = 1; k <= 10; ++k)
            x += std::sin(k * phase) / k;
        const double fade = std::min({1.0, t / 0.02, (1.2 - t) / 0.02});
        in.push_back(static_cast<float>(0.08 * x * fade));
    }
    const auto tailOf = [&](float halo)
    {
        auto p = makeVesper();
        p->setParameter(vesper::halo, halo);
        return play(*p, in, rate, 1.5).left;
    };
    const auto off = tailOf(0.0f), on = tailOf(100.0f);
    const double end = static_cast<double>(in.size()) / rate;
    const auto lift = [&](double hz)
    {
        return db(levelAt(on, rate, hz, end + 0.3, end + 1.2)) -
               db(levelAt(off, rate, hz, end + 0.3, end + 1.2));
    };
    CHECK(lift(220.0) > 6.0);
    CHECK(lift(220.0) > lift(233.1) + 6.0);
}

TEST_CASE("Halo stays stable at its longest, on loud singing and noise", "[plugins][vesper]")
{
    Singer singer(48000.0);
    const float notes[] = {110.0f, 147.0f, 196.0f, 262.0f, 349.0f, 466.0f, 622.0f, 831.0f};
    for (const float hz : notes)
        singer.vowel(0.5, hz, -3.0f).s(0.1, -6.0f).breath(0.1, -12.0f);
    const auto peakOf = [&](float halo)
    {
        auto p = makeVesper();
        p->setParameter(vesper::halo, halo);
        p->setParameter(vesper::decay, vesper::maxDecaySeconds);
        const auto out = play(*p, singer.audio(), 48000.0, 4.0);
        float peak = 0.0f;
        for (const float x : out.left)
            peak = std::isfinite(x) ? std::max(peak, std::abs(x)) : 1.0e9f;
        return peak;
    };
    // No louder than the reverb alone by more than a little, and never unbounded.
    CHECK(peakOf(100.0f) < 1.25f * peakOf(0.0f));
}

TEST_CASE("Halo turned off stops its strings and Vesper falls silent", "[plugins][vesper]")
{
    auto p = makeVesper();
    p->setParameter(vesper::halo, 100.0f);
    p->setParameter(vesper::decay, 1.0f);
    Singer singer(48000.0);
    singer.rest(0.1).vowel(0.8, 220.0f);
    play(*p, singer.audio(), 48000.0);
    p->setParameter(vesper::halo, 0.0f);
    const auto after = play(*p, {}, 48000.0, 6.0);
    CHECK(std::all_of(after.left.end() - 24000, after.left.end(), [](float x) { return x == 0.0f; }));
}

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

TEST_CASE("Vesper reports its lookahead as latency", "[plugins][vesper]")
{
    for (const double rate : {48000.0, 96000.0})
    {
        auto p = makeVesper(rate);
        CHECK(p->latencySamples() == 0);
        p->setParameter(vesper::lookahead, 1.0f);
        CHECK(p->latencySamples() == static_cast<int>(std::lround(0.03 * rate)));
        p->setParameter(vesper::lookahead, 2.0f);
        CHECK(p->latencySamples() == static_cast<int>(std::lround(0.5 * rate)));
    }
}

TEST_CASE("Vesper's lookahead delays the dry signal by its latency, bypassed too", "[plugins][vesper]")
{
    for (const float choice : {1.0f, 2.0f})
    {
        INFO("lookahead " << choice);
        auto p = makeVesper();
        p->setParameter(vesper::lookahead, choice);
        p->setParameter(vesper::mix, 0.0f);
        const int latency = p->latencySamples();
        Singer singer(48000.0);
        singer.vowel(0.4, 220.0f).s(0.1).vowel(0.6, 247.0f);
        const auto& in = singer.audio();
        const auto out = play(*p, in, 48000.0);
        for (size_t i = static_cast<size_t>(latency); i < in.size(); ++i)
            REQUIRE(out.left[i] == in[i - static_cast<size_t>(latency)]);

        auto q = makeVesper();
        q->setParameter(vesper::lookahead, choice);
        q->setBypassed(true);
        play(*q, std::vector<float>(4800, 0.0f), 48000.0); // the bypass ramp settles
        const auto bypassed = play(*q, in, 48000.0);
        for (size_t i = static_cast<size_t>(latency); i < in.size(); ++i)
            REQUIRE(bypassed.left[i] == in[i - static_cast<size_t>(latency)]);
    }
}

TEST_CASE("With lookahead, Clarity has the send down before an s starts", "[plugins][vesper]")
{
    const auto tailOfS = [](float clarity)
    {
        auto p = makeVesper();
        p->setParameter(vesper::lookahead, 1.0f);
        p->setParameter(vesper::clarity, clarity);
        p->setParameter(vesper::damping, 20000.0f);
        p->setParameter(vesper::preDelayAuto, 0.0f);
        p->setParameter(vesper::preDelay, 0.0f);
        Singer singer(48000.0);
        singer.rest(0.1).s(0.15);
        const auto out = play(*p, singer.audio(), 48000.0, 0.4);
        return rmsOf(out.left, 48000.0, 0.1, 0.6);
    };
    CHECK(db(tailOfS(100.0f)) < db(tailOfS(0.0f)) - 20.0);
}

TEST_CASE("With Long lookahead a line's last word is thrown and Ends starts as it finishes",
          "[plugins][vesper]")
{
    const auto afterWord = [](float choice)
    {
        auto p = makeVesper();
        p->setParameter(vesper::lookahead, choice);
        p->setParameter(vesper::ends, 100.0f);
        p->setParameter(vesper::decay, 1.5f);
        Singer singer(48000.0);
        singer.rest(0.1).vowel(0.8, 220.0f); // the word ends at 0.9 s
        const auto out = play(*p, singer.audio(), 48000.0, 2.0);
        // Read the same moment of the song: just after the word, before the gap.
        const double shift = vesper::lookaheadSeconds(choice);
        return rmsOf(out.left, 48000.0, 0.95 + shift, 1.1 + shift);
    };
    CHECK(db(afterWord(2.0f)) > db(afterWord(0.0f)) + 3.0);
}

TEST_CASE("Swell lets the tail rise into a line before it is sung, with Long lookahead only",
          "[plugins][vesper]")
{
    const auto beforeLine = [](float choice, float swell)
    {
        auto p = makeVesper();
        p->setParameter(vesper::lookahead, choice);
        p->setParameter(vesper::swell, swell);
        Singer singer(48000.0);
        singer.rest(1.0).vowel(0.6, 220.0f); // the line starts at 1.0 s
        const auto out = play(*p, singer.audio(), 48000.0, 0.5);
        const double start = 1.0 + vesper::lookaheadSeconds(choice);
        return std::pair{rmsOf(out.left, 48000.0, start - 0.3, start - 0.02),
                         rmsOf(out.left, 48000.0, start - 0.3, start - 0.15)};
    };
    const auto swelled = beforeLine(2.0f, 100.0f);
    CHECK(db(swelled.first) > -50.0);
    CHECK(swelled.first > swelled.second); // rising toward the line
    CHECK(beforeLine(2.0f, 0.0f).first == 0.0);
    CHECK(beforeLine(1.0f, 100.0f).first == 0.0); // Short cannot see far enough
}

TEST_CASE("Catch holds the tail as a pad, keeps the input out and lets it go", "[plugins][vesper]")
{
    auto p = makeVesper();
    p->setParameter(vesper::decay, 1.5f);
    Singer singer(48000.0);
    singer.rest(0.1).vowel(0.6, 220.0f);
    play(*p, singer.audio(), 48000.0);
    p->setParameter(vesper::catchTail, 1.0f);
    const auto held = play(*p, {}, 48000.0, 3.0);
    const double start = db(rmsOf(held.left, 48000.0, 0.3, 0.5));
    CHECK(start > -50.0);
    CHECK(std::abs(db(rmsOf(held.left, 48000.0, 2.6, 3.0)) - start) < 3.0);
    // Singing over the pad does not feed it.
    Singer over(48000.0);
    over.vowel(1.0, 330.0f, -12.0f);
    const auto sungOver = play(*p, over.audio(), 48000.0);
    CHECK(std::abs(db(rmsOf(sungOver.left, 48000.0, 0.5, 1.0)) - start) < 3.0);
    // Let go: it decays again.
    p->setParameter(vesper::catchTail, 0.0f);
    const auto released = play(*p, {}, 48000.0, 3.0);
    CHECK(db(rmsOf(released.left, 48000.0, 2.5, 3.0)) < start - 40.0);
}

TEST_CASE("Vesper takes a change of lookahead while it plays", "[plugins][vesper]")
{
    auto p = makeVesper();
    p->setParameter(vesper::ends, 60.0f);
    p->setParameter(vesper::swell, 50.0f);
    Singer singer(48000.0);
    singer.vowel(0.4, 220.0f).rest(0.3).vowel(0.4, 247.0f).rest(0.3);
    for (const float choice : {2.0f, 1.0f, 0.0f, 2.0f})
    {
        p->setParameter(vesper::lookahead, choice);
        CHECK(p->latencySamples() == vesper::lookaheadSamples(choice, 48000.0));
        const auto out = play(*p, singer.audio(), 48000.0);
        for (const float x : out.left)
            REQUIRE(std::isfinite(x));
    }
}

namespace
{
// How a preset sounds on three sung lines (vowels with an "s" and a "t" in each,
// 0.7 s between lines, a second of silence before and 2.5 s after), as the
// reverb's level against the dry voice in the mix.
struct PresetSound
{
    double underDb = 0.0; // the reverb while the singer sings
    double gapDb = 0.0;   // the reverb in the gaps between lines
    double swellDb = 0.0; // the reverb in the 0.3 s before the first line
    double rt60 = 0.0;    // the decay after the last line
    double peakDb = 0.0;  // the output's peak (the voice at -18 dB RMS)
};

PresetSound measurePreset(const std::string& name)
{
    const auto& presets = plugins::factoryPresets(vesper::type);
    const auto it = std::find_if(
        presets.begin(), presets.end(), [&](const plugins::EffectPreset& p) { return p.name == name; });
    REQUIRE(it != presets.end());
    const auto& values = it->values;

    constexpr double rate = 48000.0;
    Singer singer(rate);
    singer.rest(1.0);
    std::vector<std::pair<double, double>> lines;
    for (int line = 0; line < 3; ++line)
    {
        const double start = singer.now();
        singer.vowel(0.35, 220.0f).s(0.08).vowel(0.3, 247.0f).t().vowel(0.45, 262.0f);
        lines.emplace_back(start, singer.now());
        if (line < 2)
            singer.rest(0.7);
    }
    const auto& dry = singer.audio();

    const auto run = [&](bool tailOnly)
    {
        auto p = plugins::BuiltInFactory().create(vesper::type);
        p->prepare(rate, block);
        p->setBypassed(false);
        for (size_t i = 0; i < values.size(); ++i)
            p->setParameter(static_cast<int>(i), values[i]);
        if (tailOnly)
            p->setParameter(vesper::mix, 100.0f);
        return std::pair{play(*p, dry, rate, 2.5), p->latencySamples()};
    };
    const auto [mixed, latency] = run(false);
    const auto [tail, unused] = run(true);
    const double shift = static_cast<double>(latency) / rate;
    const double wet = values[vesper::mix] * 0.01;

    double singing = 0.0, under = 0.0, gaps = 0.0;
    for (const auto& [start, end] : lines)
    {
        singing += rmsOf(dry, rate, start, end) / 3.0;
        under += rmsOf(tail.left, rate, start + shift, end + shift) / 3.0;
    }
    for (size_t i = 0; i + 1 < lines.size(); ++i)
        gaps += rmsOf(tail.left, rate, lines[i].second + 0.1 + shift, lines[i].second + 0.6 + shift) / 2.0;
    const double dryInMix = singing * (1.0 - wet);
    const double last = lines.back().second + shift;
    const double fall = db(rmsOf(tail.left, rate, last + 0.5, last + 0.7)) -
                        db(rmsOf(tail.left, rate, last + 1.2, last + 1.4));
    float peak = 0.0f;
    for (const float x : mixed.left)
        peak = std::max(peak, std::abs(x));

    PresetSound s;
    s.underDb = db(under * wet) - db(dryInMix);
    s.gapDb = db(gaps * wet) - db(dryInMix);
    s.swellDb =
        db(rmsOf(tail.left, rate, lines[0].first + shift - 0.3, lines[0].first + shift - 0.02) * wet) -
        db(dryInMix);
    s.rt60 = fall > 0.1 ? 0.7 * 60.0 / fall : 99.0;
    s.peakDb = db(peak);
    return s;
}
} // namespace

// Each preset does what its name says, measured on sung lines (the reverb's level
// against the voice in the mix), so a change to the DSP cannot quietly turn one
// into something else. Every one keeps the reverb under the voice and headroom.
TEST_CASE("Vesper's presets each do what their names say", "[plugins][vesper][presets]")
{
    for (const auto& preset : plugins::factoryPresets(vesper::type))
    {
        INFO(preset.name);
        const auto s = measurePreset(preset.name);
        CHECK(s.gapDb < -3.0);
        CHECK(s.underDb < -8.0);
        CHECK(s.peakDb < -3.0);
        if (preset.name == "Vocal Plate") // a moderate plate under the voice
        {
            CHECK(s.underDb > -18.0);
            CHECK(s.rt60 < 3.0);
        }
        if (preset.name == "Pop Lead") // out of the way of the words, blooming between lines
            CHECK(s.gapDb > s.underDb + 3.0);
        if (preset.name == "Ballad Hall" || preset.name == "Swell In") // long and lush
        {
            CHECK(s.rt60 > 3.0);
            CHECK(s.gapDb > -12.0);
        }
        if (preset.name == "Swell In") // the tail rises into a line
            CHECK(s.swellDb > -35.0);
        if (preset.name == "Intimate Chamber" || preset.name == "Rap Clarity") // close and dry
        {
            CHECK(s.underDb < -18.0);
            CHECK(s.rt60 < 1.6);
        }
        if (preset.name == "Throws Only") // almost nothing under the singing, throws at line ends
        {
            CHECK(s.underDb < -22.0);
            CHECK(s.gapDb > s.underDb + 15.0);
        }
    }
}

// A report, hidden from the default run (northlight_unit_tests "[.presets]"): each
// preset's sound, for reviewing them against what their names promise.
TEST_CASE("Vesper's presets, measured", "[.presets][vesper]")
{
    std::printf("%-18s %8s %8s %8s %7s %8s\n", "preset", "under", "gaps", "swell", "rt60", "peak");
    for (const auto& preset : plugins::factoryPresets(vesper::type))
    {
        const auto s = measurePreset(preset.name);
        std::printf("%-18s %7.1f %8.1f %8.1f %7.2f %8.1f\n",
                    preset.name.c_str(),
                    s.underDb,
                    s.gapDb,
                    s.swellDb,
                    s.rt60,
                    s.peakDb);
    }
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
    p->setParameter(vesper::halo, 50.0f);
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

// Renders for listening, hidden from the default run: a sung phrase through
// presets with Halo off, at 50% and at 100%, as 32-bit float WAV files in the
// folder named by NORTHLIGHT_RENDER_DIR (northlight_unit_tests "[.render]").
// NORTHLIGHT_RENDER_INPUT names a recording to use instead of the synthetic
// singer: raw 32-bit float samples, mono, 48 kHz.
namespace
{
void writeWav(const std::string& path, const Played& audio, int rate)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    const auto frames = static_cast<std::uint32_t>(audio.left.size());
    const std::uint32_t bytes = frames * 2u * 4u;
    const auto u32 = [f](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const auto u16 = [f](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36u + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16u);
    u16(3u); // IEEE float
    u16(2u);
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate) * 8u);
    u16(8u);
    u16(32u);
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    for (size_t i = 0; i < audio.left.size(); ++i)
    {
        std::fwrite(&audio.left[i], 4, 1, f);
        std::fwrite(&audio.right[i], 4, 1, f);
    }
    std::fclose(f);
}
} // namespace

TEST_CASE("Vesper renders for listening", "[.render][vesper]")
{
    const char* folder = std::getenv("NORTHLIGHT_RENDER_DIR");
    if (folder == nullptr)
        SKIP("NORTHLIGHT_RENDER_DIR is not set");
    constexpr double rate = 48000.0;
    // Two sung lines, A minor: a rising line and an answer, with consonants and a breath.
    Singer singer(rate);
    singer.rest(0.5);
    singer.vowel(0.4, 220.0f).t().vowel(0.35, 262.0f).s(0.08).vowel(0.35, 294.0f).vowel(0.9, 330.0f, -18.0f);
    singer.rest(0.6).breath(0.3, -38.0f).rest(0.1);
    singer.vowel(0.35, 294.0f).t().vowel(0.35, 262.0f).s(0.08).vowel(0.4, 247.0f).vowel(1.2, 220.0f, -18.0f);
    std::vector<float> phrase = singer.audio();
    if (const char* input = std::getenv("NORTHLIGHT_RENDER_INPUT"))
    {
        std::FILE* f = std::fopen(input, "rb");
        REQUIRE(f != nullptr);
        phrase.clear();
        float x = 0.0f;
        while (std::fread(&x, 4, 1, f) == 1)
            phrase.push_back(x);
        std::fclose(f);
    }
    for (const std::string preset : {"Vocal Plate", "Ballad Hall"})
    {
        const auto& presets = plugins::factoryPresets(vesper::type);
        const auto it = std::find_if(
            presets.begin(), presets.end(), [&](const plugins::EffectPreset& p) { return p.name == preset; });
        REQUIRE(it != presets.end());
        for (const float halo : {0.0f, 50.0f, 100.0f})
        {
            auto p = plugins::BuiltInFactory().create(vesper::type);
            p->prepare(rate, block);
            p->setBypassed(false);
            for (size_t i = 0; i < it->values.size(); ++i)
                p->setParameter(static_cast<int>(i), it->values[i]);
            p->setParameter(vesper::halo, halo);
            auto out = play(*p, phrase, rate, 4.0);
            // The lookahead's delay taken off, so every file lines up.
            const auto shift = static_cast<std::ptrdiff_t>(p->latencySamples());
            out.left.erase(out.left.begin(), out.left.begin() + shift);
            out.right.erase(out.right.begin(), out.right.begin() + shift);
            std::string name = preset;
            std::replace(name.begin(), name.end(), ' ', '-');
            writeWav(std::string(folder) + "/" + name + "-halo-" + std::to_string(static_cast<int>(halo)) +
                         ".wav",
                     out,
                     static_cast<int>(rate));
        }
    }
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
    p->setParameter(vesper::lookahead, 2.0f);
    p->setParameter(vesper::swell, 60.0f);
    p->setParameter(vesper::halo, 70.0f);
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
            if (n == 80)
                p->setParameter(vesper::catchTail, 1.0f);
            if (n == 100)
                p->setParameter(vesper::catchTail, 0.0f);
            if (n == 110)
                p->setParameter(vesper::halo, 0.0f);
            if (n == 120)
                p->setBypassed(true);
            p->process(l.data(), r.data(), block);
        }
    }
    rt::setViolationHandler(previous);
    CHECK(vesperViolations == 0);
}
#endif
