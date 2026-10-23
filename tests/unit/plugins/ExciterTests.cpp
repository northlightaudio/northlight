#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/Exciter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <functional>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

using namespace northlight;
using Catch::Approx;
namespace ex = plugins::exciter;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

std::unique_ptr<engine::InsertProcessor> makeExciter()
{
    auto p = plugins::BuiltInFactory().create(ex::type);
    REQUIRE(p != nullptr);
    p->prepare(rate, block);
    p->setBypassed(false);
    p->setParameter(ex::autoGain, 0.0f); // most tests read what is added: out - in
    return p;
}

// A run's input and output, both sides.
struct Run
{
    std::vector<float> inL, inR, outL, outR;
    std::vector<float> added(bool left = true) const
    {
        std::vector<float> a(inL.size());
        for (size_t i = 0; i < a.size(); ++i)
            a[i] = left ? outL[i] - inL[i] : outR[i] - inR[i];
        return a;
    }
};

using Source = std::function<std::pair<float, float>(double seconds)>;

Run play(engine::InsertProcessor& p, double seconds, const Source& source, double from = 0.0)
{
    Run run;
    const int total = static_cast<int>(seconds * rate);
    std::vector<float> l(block), r(block);
    for (int done = 0; done < total; done += block)
    {
        const int n = std::min(block, total - done);
        for (int i = 0; i < n; ++i)
        {
            const auto [a, b] = source(from + (done + i) / rate);
            l[static_cast<size_t>(i)] = a;
            r[static_cast<size_t>(i)] = b;
        }
        run.inL.insert(run.inL.end(), l.begin(), l.begin() + n);
        run.inR.insert(run.inR.end(), r.begin(), r.begin() + n);
        p.process(l.data(), r.data(), n);
        run.outL.insert(run.outL.end(), l.begin(), l.begin() + n);
        run.outR.insert(run.outR.end(), r.begin(), r.begin() + n);
    }
    return run;
}

Source sine(double hz, float amplitude)
{
    return [hz, amplitude](double t)
    {
        const float v = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * t));
        return std::pair{v, v};
    };
}

// The level (dBFS of a sine's amplitude) at `hz` over samples [from, to): a
// Hann-windowed DFT bin.
double levelAt(const std::vector<float>& x, double hz, size_t from, size_t to)
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
    return 20.0 * std::log10(std::max(1.0e-12, 2.0 * std::abs(sum) / weight));
}

double rmsDb(const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return 10.0 * std::log10(std::max(1.0e-24, sum / static_cast<double>(to - from)));
}

size_t at(double seconds)
{
    return static_cast<size_t>(seconds * rate);
}
} // namespace

TEST_CASE("The exciter adds nothing at zero amount and keeps silence silent", "[plugins][exciter]")
{
    auto p = makeExciter();
    p->setParameter(ex::autoGain, 1.0f);
    p->setParameter(ex::amount, 0.0f);
    p->setParameter(ex::bass, 0.0f);
    p->prepare(rate, block); // starts at these settings, with no glide from the defaults
    const auto run = play(*p, 0.5, sine(3000.0, 0.4f));
    float worst = 0.0f;
    for (size_t i = 0; i < run.inL.size(); ++i)
        worst = std::max({worst, std::abs(run.outL[i] - run.inL[i]), std::abs(run.outR[i] - run.inR[i])});
    CHECK(worst == 0.0f);

    // Everything on, then silence: nothing comes out once the tails are gone.
    auto q = makeExciter();
    q->setParameter(ex::amount, 100.0f);
    q->setParameter(ex::bass, 100.0f);
    q->setParameter(ex::transients, 100.0f);
    q->setParameter(ex::stereo, 100.0f);
    const auto quiet = play(*q, 1.0, [](double) { return std::pair{0.0f, 0.0f}; });
    float peak = 0.0f;
    for (float v : quiet.outL)
        peak = std::max(peak, std::abs(v));
    CHECK(peak == 0.0f);
}

TEST_CASE("The exciter drives only the band above its crossover", "[plugins][exciter]")
{
    auto p = makeExciter();
    p->setParameter(ex::frequency, 2000.0f);
    p->setParameter(ex::amount, 60.0f);
    const auto run = play(*p,
                          1.0,
                          [](double t)
                          {
                              const float v =
                                  0.3f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 300.0 * t)) +
                                  0.1f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 5000.0 * t));
                              return std::pair{v, v};
                          });
    const auto added = run.added();
    const double octaveOfHigh = levelAt(added, 10000.0, at(0.5), at(1.0));
    const double octaveOfLow = levelAt(added, 600.0, at(0.5), at(1.0));
    CHECK(octaveOfHigh > -45.0);
    CHECK(octaveOfLow < octaveOfHigh - 40.0);
}

TEST_CASE("Each character adds its own series of harmonics", "[plugins][exciter]")
{
    struct Series
    {
        double second, third, fourth;
    };
    const auto series = [](int character)
    {
        auto p = makeExciter();
        p->setParameter(ex::frequency, 1000.0f);
        p->setParameter(ex::character, static_cast<float>(character));
        p->setParameter(ex::drive, 10.0f);
        p->setParameter(ex::amount, 60.0f);
        const auto added = play(*p, 1.0, sine(3000.0, 0.2f)).added();
        return Series{levelAt(added, 6000.0, at(0.5), at(1.0)),
                      levelAt(added, 9000.0, at(0.5), at(1.0)),
                      levelAt(added, 12000.0, at(0.5), at(1.0))};
    };
    const auto tube = series(ex::tube);
    CHECK(tube.second > -50.0);
    CHECK(tube.third > -60.0); // warm: the octave and the twelfth
    const auto tape = series(ex::tape);
    CHECK(tape.third > tape.second + 30.0); // odd only
    const auto crisp = series(ex::crisp);
    CHECK(crisp.second > crisp.third + 30.0); // even only
    CHECK(crisp.fourth > -60.0);              // and a dense series
    const auto air = series(ex::air);
    CHECK(air.second > air.third + 30.0);
    CHECK(air.second > air.fourth + 6.0); // mostly the octave
}

TEST_CASE("The exciter's harmonics sit at the Amount's level under the band, for every character and level",
          "[plugins][exciter]")
{
    const auto ratio = [](int character, float amplitude, float drive)
    {
        auto p = makeExciter();
        p->setParameter(ex::frequency, 1000.0f);
        p->setParameter(ex::character, static_cast<float>(character));
        p->setParameter(ex::drive, drive);
        p->setParameter(ex::amount, 50.0f); // 6 dB under the band
        const auto run = play(*p, 1.0, sine(3000.0, amplitude));
        return rmsDb(run.added(), at(0.5), at(1.0)) - rmsDb(run.inL, at(0.5), at(1.0));
    };
    for (int c = 0; c < ex::characterCount; ++c)
        for (float drive : {0.0f, 8.0f, 20.0f})
        {
            INFO("character " << c << ", drive " << drive);
            const double loud = ratio(c, 0.3f, drive);
            CHECK(loud == Approx(-6.0).margin(2.5));
            // 30 dB quieter, the same distance under the band.
            CHECK(ratio(c, 0.3f * 0.0316f, drive) == Approx(loud).margin(1.0));
        }
}

TEST_CASE("Focus tilts the harmonics without making them louder overall", "[plugins][exciter]")
{
    // The octave sits under the tilt's corner (three times the crossover), the
    // fourth harmonic over it.
    struct Result
    {
        double tilt, total;
    };
    const auto measure = [](float focus)
    {
        auto p = makeExciter();
        p->setParameter(ex::frequency, 2000.0f);
        p->setParameter(ex::drive, 14.0f);
        p->setParameter(ex::amount, 60.0f);
        p->setParameter(ex::focus, focus);
        const auto run = play(*p, 1.0, sine(2500.0, 0.2f));
        const auto added = run.added();
        return Result{levelAt(added, 10000.0, at(0.5), at(1.0)) - levelAt(added, 5000.0, at(0.5), at(1.0)),
                      rmsDb(added, at(0.5), at(1.0))};
    };
    const auto dark = measure(-100.0f), edge = measure(100.0f);
    CHECK(edge.tilt > dark.tilt + 8.0);
    CHECK(std::abs(edge.total - dark.total) < 6.0);
}

TEST_CASE("The exciter's curves are anti-aliased", "[plugins][exciter]")
{
    auto p = makeExciter();
    p->setParameter(ex::frequency, 2000.0f);
    p->setParameter(ex::character, static_cast<float>(ex::tape));
    p->setParameter(ex::drive, 12.0f);
    p->setParameter(ex::amount, 100.0f);
    const auto added = play(*p, 1.0, sine(7000.0, 0.3f)).added();
    const double third = levelAt(added, 21000.0, at(0.5), at(1.0));
    CHECK(third > -40.0);
    // Where the fifth, seventh and ninth harmonics would fold back to.
    for (double alias : {13000.0, 1000.0, 15000.0})
    {
        INFO("alias at " << alias);
        CHECK(levelAt(added, alias, at(0.5), at(1.0)) < third - 60.0);
    }
}

TEST_CASE("Dynamics push the loud moments or fill the quiet ones", "[plugins][exciter]")
{
    // Half a second loud, half a second 26 dB down, repeating.
    const Source phrase = [](double t)
    {
        const float level = std::fmod(t, 1.0) < 0.5 ? 0.3f : 0.015f;
        const float v = level * static_cast<float>(std::sin(2.0 * std::numbers::pi * 4000.0 * t));
        return std::pair{v, v};
    };
    // Added under the input (dB) at the end of a loud and of a quiet half.
    const auto spread = [&](float dynamics)
    {
        auto p = makeExciter();
        p->setParameter(ex::frequency, 1500.0f);
        p->setParameter(ex::amount, 40.0f);
        p->setParameter(ex::dynamics, dynamics);
        const auto run = play(*p, 4.0, phrase);
        const auto added = run.added();
        const double loud = rmsDb(added, at(3.3), at(3.5)) - rmsDb(run.inL, at(3.3), at(3.5));
        const double quiet = rmsDb(added, at(3.8), at(4.0)) - rmsDb(run.inL, at(3.8), at(4.0));
        return loud - quiet;
    };
    CHECK(std::abs(spread(0.0f)) < 1.5);
    CHECK(spread(100.0f) > 6.0);
    CHECK(spread(-100.0f) < -6.0);
}

TEST_CASE("Transients accent the onsets", "[plugins][exciter]")
{
    // A tone that starts after silence: the added level in its first 15 ms against later.
    const auto accent = [](float transients)
    {
        auto p = makeExciter();
        p->setParameter(ex::frequency, 1500.0f);
        p->setParameter(ex::amount, 30.0f);
        p->setParameter(ex::transients, transients);
        const auto run = play(
            *p,
            0.8,
            [](double t)
            {
                const float v =
                    t < 0.4 ? 0.0f : 0.2f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 4000.0 * t));
                return std::pair{v, v};
            });
        const auto added = run.added();
        return rmsDb(added, at(0.405), at(0.42)) - rmsDb(added, at(0.6), at(0.8));
    };
    CHECK(accent(100.0f) > accent(0.0f) + 4.0);
}

TEST_CASE("The low end gains harmonics of the bass", "[plugins][exciter]")
{
    auto p = makeExciter();
    p->setParameter(ex::amount, 0.0f);
    p->setParameter(ex::bass, 100.0f);
    p->setParameter(ex::bassFrequency, 100.0f);
    const auto added = play(*p, 1.5, sine(50.0, 0.3f)).added();
    const double second = levelAt(added, 100.0, at(0.5), at(1.5));
    const double third = levelAt(added, 150.0, at(0.5), at(1.5));
    CHECK(second > -30.0);
    CHECK(third > -40.0);
    CHECK(levelAt(added, 50.0, at(0.5), at(1.5)) < second - 12.0);
    // Off, nothing.
    p->setParameter(ex::bass, 0.0f);
    const auto off = play(*p, 0.6, sine(50.0, 0.3f)).added();
    CHECK(rmsDb(off, at(0.4), at(0.6)) < -120.0);
}

TEST_CASE("Stereo spreads the harmonics without changing the mono sum", "[plugins][exciter]")
{
    const auto runWith = [](float stereo)
    {
        auto p = makeExciter();
        p->setParameter(ex::amount, 60.0f);
        p->setParameter(ex::stereo, stereo);
        return play(*p, 0.6, sine(5000.0, 0.2f));
    };
    const auto narrow = runWith(0.0f), wide = runWith(100.0f);
    double side = 0.0, monoError = 0.0;
    for (size_t i = at(0.3); i < at(0.6); ++i)
    {
        side = std::max(side, static_cast<double>(std::abs(wide.outL[i] - wide.outR[i])));
        monoError = std::max(
            monoError,
            static_cast<double>(std::abs((wide.outL[i] + wide.outR[i]) - (narrow.outL[i] + narrow.outR[i]))));
        CHECK(narrow.outL[i] == narrow.outR[i]);
    }
    CHECK(side > 0.01);
    CHECK(monoError < 1.0e-5);
}

TEST_CASE("Auto gain matches the output's loudness to the input's", "[plugins][exciter]")
{
    const Source chord = [](double t)
    {
        const float v = 0.15f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 3000.0 * t) +
                                                   std::sin(2.0 * std::numbers::pi * 4700.0 * t));
        return std::pair{v, v};
    };
    const auto louder = [&](bool autoGain, float* reduction)
    {
        auto p = makeExciter();
        p->setParameter(ex::amount, 100.0f);
        p->setParameter(ex::autoGain, autoGain ? 1.0f : 0.0f);
        const auto run = play(*p, 5.0, chord);
        if (reduction != nullptr)
            *reduction = p->feedback()->latest().reduction;
        return rmsDb(run.outL, at(4.0), at(5.0)) - rmsDb(run.inL, at(4.0), at(5.0));
    };
    float reduction = 0.0f;
    CHECK(louder(false, nullptr) > 1.5);
    CHECK(std::abs(louder(true, &reduction)) < 0.5);
    CHECK(reduction > 1.0f);
}

TEST_CASE("The exciter's feedback reads the input and what is added", "[plugins][exciter]")
{
    auto p = makeExciter();
    p->setParameter(ex::frequency, 1000.0f);
    p->setParameter(ex::amount, 50.0f);
    play(*p, 1.0, sine(3000.0, 0.3f));
    const auto f = p->feedback()->latest();
    CHECK(f.extra == Approx(0.3f / std::numbers::sqrt2_v<float>).margin(0.01f));
    CHECK(20.0f * std::log10(f.extra2 / f.extra) == Approx(-6.0f).margin(2.5f));
    CHECK(f.reduction == 0.0f); // auto gain off
}

TEST_CASE("The exciter stays in bounds at its extremes", "[plugins][exciter]")
{
    // Every control at its maximum, on full-band noise with kick hits.
    const auto extreme = [](bool autoGain)
    {
        auto p = makeExciter();
        for (int i : {ex::drive, ex::amount, ex::transients, ex::dynamics, ex::stereo, ex::bass, ex::focus})
            p->setParameter(i, 1000.0f); // clamped to each maximum
        p->setParameter(ex::frequency, ex::minFrequency);
        p->setParameter(ex::autoGain, autoGain ? 1.0f : 0.0f);
        std::uint32_t seed = 1;
        return play(*p,
                    4.0,
                    [&](double t)
                    {
                        seed = seed * 1664525u + 1013904223u;
                        const float noise = static_cast<float>(seed >> 8) / 16777216.0f - 0.5f;
                        const float kick = std::fmod(t, 0.5) < 0.05 ? 0.9f : 0.0f;
                        const float v =
                            0.3f * noise +
                            kick * static_cast<float>(std::sin(2.0 * std::numbers::pi * 55.0 * t));
                        return std::pair{v, -v};
                    });
    };
    const auto raw = extreme(false);
    float in = 0.0f, out = 0.0f;
    for (size_t i = 0; i < raw.outL.size(); ++i)
    {
        REQUIRE(std::isfinite(raw.outL[i]));
        REQUIRE(std::isfinite(raw.outR[i]));
        in = std::max(in, std::abs(raw.inL[i]));
        out = std::max({out, std::abs(raw.outL[i]), std::abs(raw.outR[i])});
    }
    CHECK(out < 4.2f * in); // about +12 dB at most, with nothing held back
    // With auto gain (the default) the loudness comes back down.
    const auto matched = extreme(true);
    const double rawLouder = rmsDb(raw.outL, at(3.0), at(4.0)) - rmsDb(raw.inL, at(3.0), at(4.0));
    const double matchedLouder = rmsDb(matched.outL, at(3.0), at(4.0)) - rmsDb(matched.inL, at(3.0), at(4.0));
    CHECK(rawLouder > 3.0);
    // Matched on K-weighted loudness, which counts the added highs more than plain RMS does.
    CHECK(matchedLouder < rawLouder - 8.0);
    CHECK(matchedLouder < 0.5);
}

TEST_CASE("Bypassed, the exciter passes the audio and still measures for a watching editor",
          "[plugins][exciter]")
{
    auto p = makeExciter();
    p->setParameter(ex::frequency, 1000.0f);
    p->setBypassed(true);
    play(*p, 0.05, sine(3000.0, 0.3f)); // the bypass ramp settles
    p->feedback()->setWatched(true);
    const auto run = play(*p, 0.5, sine(3000.0, 0.3f), 0.05);
    for (size_t i = 0; i < run.inL.size(); ++i)
        REQUIRE(run.outL[i] == run.inL[i]);
    CHECK(p->feedback()->latest().extra2 > 0.01f);
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> exciterViolations{0};
void countExciterViolation(const rt::Violation&) noexcept
{
    exciterViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("The exciter processes without allocating on the real-time thread", "[plugins][exciter][rt]")
{
    auto p = makeExciter();
    p->setParameter(ex::bass, 60.0f);
    p->setParameter(ex::stereo, 50.0f);
    p->setParameter(ex::transients, 50.0f);
    p->setParameter(ex::autoGain, 1.0f);
    p->feedback()->setWatched(true);
    p->feedback()->setTapsEnabled(true);
    std::vector<float> l(block), r(block);
    for (int i = 0; i < block; ++i)
    {
        l[static_cast<size_t>(i)] = 0.3f * std::sin(0.4f * static_cast<float>(i));
        r[static_cast<size_t>(i)] = 0.2f * std::sin(0.3f * static_cast<float>(i));
    }
    const auto previous = rt::setViolationHandler(&countExciterViolation);
    exciterViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int i = 0; i < 100; ++i)
        {
            if (i == 20)
                p->setParameter(ex::character, static_cast<float>(ex::crisp));
            if (i == 50)
                p->setBypassed(true);
            p->process(l.data(), r.data(), block);
        }
    }
    rt::setViolationHandler(previous);
    CHECK(exciterViolations == 0);
}
#endif
