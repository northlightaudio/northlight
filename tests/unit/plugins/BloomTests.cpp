#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/Bloom.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;
namespace bloom = plugins::bloom;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

std::unique_ptr<engine::InsertProcessor> makeBloom()
{
    auto p = plugins::BuiltInFactory().create(bloom::type);
    REQUIRE(p != nullptr);
    p->prepare(rate, block);
    p->setBypassed(false);
    p->setParameter(bloom::mix, 100.0f); // the tail alone unless a test says otherwise
    return p;
}

// A repeatable noise source (uniform, -1..1).
struct Noise
{
    std::uint32_t state = 12345u;
    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
};

enum class Source
{
    silence,
    noise,
    sine,
    impulse
};

// `seconds` through the processor; the output (left channel, and right).
struct Played
{
    std::vector<float> left, right;
};
Played play(engine::InsertProcessor& p,
            Source source,
            double seconds,
            float amplitude = 0.5f,
            double hz = 440.0,
            Noise* noise = nullptr)
{
    Played out;
    Noise local;
    Noise& n = noise != nullptr ? *noise : local;
    std::vector<float> l(block), r(block);
    double phase = 0.0;
    const int total = static_cast<int>(seconds * rate);
    for (int done = 0; done < total; done += block)
    {
        for (int i = 0; i < block; ++i)
        {
            float v = 0.0f;
            switch (source)
            {
                case Source::silence:
                    break;
                case Source::noise:
                    v = amplitude * n.next();
                    break;
                case Source::sine:
                    v = amplitude * static_cast<float>(std::sin(phase));
                    phase += 2.0 * std::numbers::pi * hz / rate;
                    break;
                case Source::impulse:
                    v = done == 0 && i == 0 ? amplitude : 0.0f;
                    break;
            }
            l[static_cast<size_t>(i)] = v;
            r[static_cast<size_t>(i)] = source == Source::noise ? amplitude * n.next() : v;
        }
        p.process(l.data(), r.data(), block);
        out.left.insert(out.left.end(), l.begin(), l.end());
        out.right.insert(out.right.end(), r.begin(), r.end());
    }
    return out;
}

// RMS (both channels) over [from, to) seconds of a played buffer.
double rmsOf(const Played& p, double from, double to)
{
    const auto a = static_cast<size_t>(std::max(0.0, from * rate));
    const auto b = std::min(p.left.size(), static_cast<size_t>(to * rate));
    double sum = 0.0;
    for (size_t i = a; i < b; ++i)
        sum +=
            0.5 * (static_cast<double>(p.left[i]) * p.left[i] + static_cast<double>(p.right[i]) * p.right[i]);
    return b > a ? std::sqrt(sum / static_cast<double>(b - a)) : 0.0;
}

double db(double linear)
{
    return 20.0 * std::log10(std::max(1.0e-12, linear));
}

float peakOf(const Played& p)
{
    float peak = 0.0f;
    for (size_t i = 0; i < p.left.size(); ++i)
        peak = std::max(peak, std::max(std::abs(p.left[i]), std::abs(p.right[i])));
    return peak;
}

bool finite(const Played& p)
{
    for (size_t i = 0; i < p.left.size(); ++i)
        if (!std::isfinite(p.left[i]) || !std::isfinite(p.right[i]))
            return false;
    return true;
}

// The level of one frequency in [from, to) seconds of the left channel (Goertzel), linear.
double toneLevel(const Played& p, double hz, double from, double to)
{
    const auto a = static_cast<size_t>(from * rate);
    const auto b = std::min(p.left.size(), static_cast<size_t>(to * rate));
    const double w = 2.0 * std::numbers::pi * hz / rate;
    const double coefficient = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = a; i < b; ++i)
    {
        const double s = p.left[i] + coefficient * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
    return std::sqrt(std::max(0.0, power)) / static_cast<double>(b - a);
}
} // namespace

TEST_CASE("Bloom's helpers: size, pitch, synced pre-delay and the decay across the spectrum",
          "[plugins][bloom]")
{
    CHECK(bloom::sizeScale(0.0f) == Approx(bloom::minScale));
    CHECK(bloom::sizeScale(100.0f) == Approx(bloom::maxScale));
    CHECK(bloom::sizeMetres(100.0f) > 4.0f * bloom::sizeMetres(30.0f));

    CHECK(bloom::pitchRatio(3.0f, 0) == Approx(2.0f));
    CHECK(bloom::pitchRatio(0.0f, 1) == Approx(0.5f));
    CHECK(bloom::pitchRatio(2.0f, 0) == Approx(std::exp2(7.0f / 12.0f)));
    // Both octaves: even lines up, odd lines down.
    CHECK(bloom::pitchRatio(static_cast<float>(bloom::bothOctaves), 2) == Approx(2.0f));
    CHECK(bloom::pitchRatio(static_cast<float>(bloom::bothOctaves), 3) == Approx(0.5f));

    CHECK(bloom::preDelaySeconds(120.0f, 0.0f, 120.0) == Approx(0.12));
    CHECK(bloom::preDelaySeconds(120.0f, 8.0f, 120.0) == Approx(0.25)); // an eighth at 120 bpm
    // A whole note at 40 bpm is six seconds: held to the line's two.
    CHECK(bloom::preDelaySeconds(0.0f, 16.0f, 40.0) == Approx(bloom::maxSyncedPreDelaySeconds));

    // Where the filters leave the signal alone the tail decays at the decay
    // time; above the damping it is shorter, and longer decays keep the ratio.
    CHECK(bloom::decayAt(1000.0f, 6.0f, 60.0f, 20000.0f, 20.0f) == Approx(6.0f).epsilon(0.05));
    const float highs = bloom::decayAt(10000.0f, 6.0f, 60.0f, 2000.0f, 20.0f);
    CHECK(highs < 3.0f);
    CHECK(highs > 1.0f);
    CHECK(bloom::decayAt(60.0f, 6.0f, 60.0f, 20000.0f, 400.0f) < 3.0f);
}

TEST_CASE("Bloom's tail falls 60 dB over the decay time", "[plugins][bloom]")
{
    const auto measured = [](float decay)
    {
        auto p = makeBloom();
        p->setParameter(bloom::decay, decay);
        p->setParameter(bloom::damping, 20000.0f);
        p->setParameter(bloom::lowCut, 20.0f);
        p->setParameter(bloom::modulation, 0.0f);
        p->setParameter(bloom::preDelay, 0.0f);
        play(*p, Source::noise, 0.3, 0.5f);
        const auto tail = play(*p, Source::silence, 2.0);
        // The slope between two windows a second apart.
        const double fall = db(rmsOf(tail, 0.3, 0.5)) - db(rmsOf(tail, 1.3, 1.5));
        return 60.0 / fall;
    };
    CHECK(measured(2.0f) == Approx(2.0).epsilon(0.25));
    CHECK(measured(4.0f) == Approx(4.0).epsilon(0.25));
}

TEST_CASE("Bloom's levels stay sane at the defaults and at the extremes", "[plugins][bloom]")
{
    // Defaults: noise in, a tail of a similar level out.
    {
        auto p = makeBloom();
        const auto out = play(*p, Source::noise, 4.0, 0.25f);
        const double in = 0.25 / std::sqrt(3.0);
        const double level = db(rmsOf(out, 2.0, 4.0)) - db(in);
        CHECK(level > -10.0);
        CHECK(level < 6.0);
    }
    // Everything at its most: the longest decay, the largest size, full
    // shimmer up a twelfth, full diffusion and modulation, a loud input for
    // seconds, then frozen with the input still coming. It never runs away.
    auto p = makeBloom();
    p->setParameter(bloom::decay, bloom::maxDecaySeconds);
    p->setParameter(bloom::size, 100.0f);
    p->setParameter(bloom::shimmer, 100.0f);
    p->setParameter(bloom::pitch, 4.0f); // +19
    p->setParameter(bloom::diffusion, 100.0f);
    p->setParameter(bloom::modulation, 100.0f);
    p->setParameter(bloom::modRate, 5.0f);
    p->setParameter(bloom::damping, 20000.0f);
    p->setParameter(bloom::lowCut, 20.0f);
    p->setParameter(bloom::swell, 2000.0f);
    Noise noise;
    const auto loud = play(*p, Source::noise, 12.0, 1.0f, 0.0, &noise);
    CHECK(finite(loud));
    CHECK(peakOf(loud) < 4.0f);
    CHECK(rmsOf(loud, 8.0, 12.0) < 1.0);
    p->setParameter(bloom::freeze, 1.0f);
    const auto held = play(*p, Source::noise, 6.0, 1.0f, 0.0, &noise);
    CHECK(finite(held));
    CHECK(peakOf(held) < 4.0f);
    // Both octaves at once, too.
    p->setParameter(bloom::freeze, 0.0f);
    p->setParameter(bloom::pitch, static_cast<float>(bloom::bothOctaves));
    const auto both = play(*p, Source::sine, 6.0, 1.0f, 220.0);
    CHECK(finite(both));
    CHECK(peakOf(both) < 4.0f);
}

TEST_CASE("Bloom's freeze holds the tail, keeps the input out and lets go", "[plugins][bloom]")
{
    auto p = makeBloom();
    p->setParameter(bloom::decay, 2.0f);
    play(*p, Source::noise, 1.0, 0.4f);
    p->setParameter(bloom::freeze, 1.0f);
    // A little time for the freeze to fade in, then seconds of silence.
    const auto settle = play(*p, Source::silence, 0.4);
    const double start = rmsOf(settle, 0.3, 0.4);
    const auto held = play(*p, Source::silence, 6.0);
    CHECK(start > 0.01);
    CHECK(std::abs(db(rmsOf(held, 5.0, 6.0)) - db(start)) < 3.0);
    // Input while frozen does not reach the tail.
    const auto ignored = play(*p, Source::noise, 2.0, 0.9f);
    CHECK(std::abs(db(rmsOf(ignored, 1.0, 2.0)) - db(start)) < 3.0);
    // Let go: it decays again.
    p->setParameter(bloom::freeze, 0.0f);
    const auto released = play(*p, Source::silence, 4.0);
    CHECK(db(rmsOf(released, 3.5, 4.0)) < db(start) - 60.0);

    // Frozen, the held tail keeps moving: windows a second apart are different
    // sound, not one loop repeating.
    auto q = makeBloom();
    q->setParameter(bloom::modulation, 0.0f); // even with no modulation asked for
    play(*q, Source::noise, 1.0, 0.4f);
    q->setParameter(bloom::freeze, 1.0f);
    const auto frozen = play(*q, Source::silence, 3.0);
    const auto window = static_cast<size_t>(0.1 * rate);
    double best = 0.0;
    // The best match of a window against any lag over the next second.
    const size_t a = static_cast<size_t>(1.0 * rate);
    double energyA = 0.0;
    for (size_t i = 0; i < window; ++i)
        energyA += static_cast<double>(frozen.left[a + i]) * frozen.left[a + i];
    for (size_t lag = static_cast<size_t>(0.02 * rate); lag < static_cast<size_t>(1.0 * rate); lag += 7)
    {
        double cross = 0.0, energyB = 0.0;
        for (size_t i = 0; i < window; ++i)
        {
            cross += static_cast<double>(frozen.left[a + i]) * frozen.left[a + lag + i];
            energyB += static_cast<double>(frozen.left[a + lag + i]) * frozen.left[a + lag + i];
        }
        best = std::max(best, cross / std::sqrt(energyA * energyB + 1.0e-20));
    }
    CHECK(best < 0.9);
}

TEST_CASE("Bloom's shimmer moves the tail up by its interval", "[plugins][bloom]")
{
    const auto octaveAbove = [](float shimmer)
    {
        auto p = makeBloom();
        p->setParameter(bloom::decay, 4.0f);
        p->setParameter(bloom::shimmer, shimmer);
        p->setParameter(bloom::pitch, 3.0f); // +12
        p->setParameter(bloom::modulation, 0.0f);
        play(*p, Source::sine, 0.5, 0.5f, 440.0);
        const auto tail = play(*p, Source::silence, 2.0);
        return db(toneLevel(tail, 880.0, 0.5, 1.5)) - db(toneLevel(tail, 440.0, 0.5, 1.5));
    };
    const double dry = octaveAbove(0.0f);
    const double shimmering = octaveAbove(60.0f);
    CHECK(shimmering > dry + 12.0);
}

TEST_CASE("Bloom's swell builds the tail slowly instead of at once", "[plugins][bloom]")
{
    // When the tail of a single hit is loudest.
    const auto peakTime = [](float swellMs)
    {
        auto p = makeBloom();
        p->setParameter(bloom::decay, 3.0f);
        p->setParameter(bloom::preDelay, 0.0f);
        p->setParameter(bloom::swell, swellMs);
        const auto out = play(*p, Source::impulse, 3.0, 1.0f);
        double best = 0.0, at = 0.0;
        for (double t = 0.0; t < 2.9; t += 0.05)
            if (const double r = rmsOf(out, t, t + 0.05); r > best)
            {
                best = r;
                at = t;
            }
        return at;
    };
    CHECK(peakTime(bloom::swellOffMs) < 0.2);
    CHECK(peakTime(1500.0f) > 0.9);
}

TEST_CASE("Bloom's gate cuts the tail after the input stops, and frozen it never does", "[plugins][bloom]")
{
    const auto afterBurst = [](float gateMs, bool freeze)
    {
        auto p = makeBloom();
        p->setParameter(bloom::decay, 8.0f);
        p->setParameter(bloom::gate, gateMs);
        const auto during = play(*p, Source::noise, 0.5, 0.5f);
        if (freeze)
            p->setParameter(bloom::freeze, 1.0f);
        const auto after = play(*p, Source::silence, 1.0);
        return db(rmsOf(after, 0.6, 1.0)) - db(rmsOf(during, 0.3, 0.5));
    };
    CHECK(afterBurst(bloom::gateOffMs, false) > -12.0);
    CHECK(afterBurst(250.0f, false) < -80.0);
    CHECK(afterBurst(250.0f, true) > -12.0);
}

TEST_CASE("Bloom's ducking lowers the tail under the input and lets it back", "[plugins][bloom]")
{
    const auto levels = [](float duck)
    {
        auto p = makeBloom();
        p->setParameter(bloom::decay, 10.0f);
        p->setParameter(bloom::duck, duck);
        p->setParameter(bloom::duckRelease, 200.0f);
        const auto during = play(*p, Source::noise, 1.5, 0.5f);
        const auto after = play(*p, Source::silence, 1.5);
        return std::pair{db(rmsOf(during, 1.0, 1.5)), db(rmsOf(after, 1.0, 1.5))};
    };
    const auto [openDuring, openAfter] = levels(0.0f);
    const auto [duckDuring, duckAfter] = levels(100.0f);
    CHECK(duckDuring < openDuring - 15.0);
    CHECK(std::abs(duckAfter - openAfter) < 2.0);
    // The card reads the ducking from the reduction.
    auto p = makeBloom();
    p->feedback()->setWatched(true);
    p->setParameter(bloom::duck, 50.0f);
    play(*p, Source::noise, 0.3, 0.5f);
    CHECK(p->feedback()->latest().reduction > 6.0f);
}

TEST_CASE("Bloom's pre-delay holds the tail back, by a note value when synced", "[plugins][bloom]")
{
    const auto onset = [](float ms, float syncChoice)
    {
        auto p = makeBloom();
        p->setParameter(bloom::preDelay, ms);
        p->setParameter(bloom::sync, syncChoice);
        engine::PlayHead head;
        head.tempoBpm = 120.0;
        p->setPlayHead(head);
        const auto out = play(*p, Source::impulse, 1.0, 1.0f);
        for (size_t i = 0; i < out.left.size(); ++i)
            if (std::abs(out.left[i]) > 1.0e-4f || std::abs(out.right[i]) > 1.0e-4f)
                return static_cast<double>(i) / rate;
        return 10.0;
    };
    CHECK(onset(100.0f, 0.0f) == Approx(0.1).margin(0.005));
    CHECK(onset(100.0f, 8.0f) == Approx(0.25).margin(0.005)); // an eighth at 120 bpm
}

TEST_CASE("Bloom passes the dry signal at no mix and when bypassed, its tail kept running",
          "[plugins][bloom]")
{
    auto p = makeBloom();
    p->setParameter(bloom::mix, 0.0f);
    std::vector<float> l(block, 0.3f), r(block, -0.2f);
    for (int i = 0; i < 20; ++i)
    {
        std::fill(l.begin(), l.end(), 0.3f);
        std::fill(r.begin(), r.end(), -0.2f);
        p->process(l.data(), r.data(), block);
    }
    CHECK(l.back() == Approx(0.3f).margin(1.0e-6f));
    CHECK(r.back() == Approx(-0.2f).margin(1.0e-6f));

    // Bypassed: the dry signal out, but the tail goes on building, so it is
    // there at once when it comes back.
    auto q = makeBloom();
    q->setParameter(bloom::decay, 6.0f);
    q->setBypassed(true);
    const auto bypassed = play(*q, Source::noise, 1.0, 0.4f);
    // Past the bypass's 10 ms crossfade, the input exactly (the source draws left, then right).
    Noise again;
    constexpr size_t at = 2000;
    float expected = 0.0f;
    for (size_t i = 0; i <= at; ++i)
    {
        expected = 0.4f * again.next();
        again.next();
    }
    CHECK(bypassed.left[at] == Approx(expected).margin(1.0e-6f));
    q->setBypassed(false);
    const auto back = play(*q, Source::silence, 0.1);
    CHECK(rmsOf(back, 0.05, 0.1) > 0.01);
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> bloomViolations{0};
void countBloomViolation(const rt::Violation&) noexcept
{
    bloomViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Bloom processes without allocating on the real-time thread", "[plugins][bloom][rt]")
{
    auto p = makeBloom();
    p->feedback()->setWatched(true);
    p->feedback()->setTapsEnabled(true);
    p->setParameter(bloom::shimmer, 50.0f);
    p->setParameter(bloom::swell, 800.0f);
    p->setParameter(bloom::gate, 300.0f);
    p->setParameter(bloom::duck, 40.0f);
    p->setParameter(bloom::sync, 8.0f);
    std::vector<float> l(block, 0.4f), r(block, -0.3f);
    const auto previous = rt::setViolationHandler(&countBloomViolation);
    bloomViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int i = 0; i < 100; ++i)
        {
            if (i == 50)
                p->setParameter(bloom::freeze, 1.0f);
            p->process(l.data(), r.data(), block);
        }
        p->setBypassed(true);
        for (int i = 0; i < 20; ++i)
            p->process(l.data(), r.data(), block);
    }
    rt::setViolationHandler(previous);
    CHECK(bloomViolations == 0);
}
#endif
