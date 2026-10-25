#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/Patina.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <numbers>
#include <set>
#include <string>
#include <vector>

using namespace northlight;
using Catch::Approx;
namespace pa = plugins::patina;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

// A Patina with every stage off and the output section neutral, so a test
// switches on only what it is about.
std::unique_ptr<engine::InsertProcessor> makePatina(bool allOff = true)
{
    auto p = plugins::BuiltInFactory().create(pa::type);
    REQUIRE(p != nullptr);
    p->prepare(rate, block);
    p->setBypassed(false);
    if (allOff)
        for (int s = 0; s < pa::stageCount; ++s)
            p->setParameter(pa::onParam(s), 0.0f);
    return p;
}

struct Stereo
{
    std::vector<float> l, r;
};

// `seconds` of a sine (hz > 0) or silence (hz == 0) through the processor.
Stereo play(engine::InsertProcessor& p, double hz, float amplitude, double seconds, double rightHz = -1.0)
{
    const int total = static_cast<int>(seconds * rate);
    Stereo out;
    out.l.resize(static_cast<size_t>(total));
    out.r.resize(static_cast<size_t>(total));
    std::vector<float> l(block), r(block);
    if (rightHz < 0.0)
        rightHz = hz;
    for (int done = 0; done < total; done += block)
    {
        const int n = std::min(block, total - done);
        for (int i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(done + i) / rate;
            l[static_cast<size_t>(i)] =
                hz > 0.0 ? amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * t)) : 0.0f;
            r[static_cast<size_t>(i)] =
                rightHz > 0.0 ? amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * rightHz * t))
                              : 0.0f;
        }
        p.process(l.data(), r.data(), n);
        std::copy(l.begin(), l.begin() + n, out.l.begin() + done);
        std::copy(r.begin(), r.begin() + n, out.r.begin() + done);
    }
    return out;
}

double rms(const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    to = to == 0 ? x.size() : std::min(to, x.size());
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return to > from ? std::sqrt(sum / static_cast<double>(to - from)) : 0.0;
}

double db(double linear)
{
    return 20.0 * std::log10(std::max(1.0e-12, linear));
}

// The amplitude of one frequency in a signal (a Goertzel over [from, to)).
double amplitudeAt(const std::vector<float>& x, double hz, size_t from, size_t to)
{
    const double w = 2.0 * std::numbers::pi * hz / rate;
    const double c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double s = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 2.0 * std::sqrt(std::max(0.0, power)) / static_cast<double>(to - from);
}

// The periods between rising zero crossings, in samples (interpolated).
std::vector<double> periods(const std::vector<float>& x, size_t from)
{
    std::vector<double> crossings;
    for (size_t i = from + 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            crossings.push_back(static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]));
    std::vector<double> out;
    for (size_t i = 1; i < crossings.size(); ++i)
        out.push_back(crossings[i] - crossings[i - 1]);
    return out;
}

// The spread of a pitch over its periods, in cents.
double pitchSpreadCents(const std::vector<float>& x, size_t from)
{
    const auto p = periods(x, from);
    REQUIRE(p.size() > 10);
    const auto [lo, hi] = std::minmax_element(p.begin(), p.end());
    return 1200.0 * std::log2(*hi / *lo);
}
} // namespace

TEST_CASE("Patina's Age scales every amount: nothing, as set, then toward full", "[plugins][patina]")
{
    CHECK(pa::aged(0.6f, 0.0f) == 0.0f);
    CHECK(pa::aged(0.6f, 0.25f) == Approx(0.3f));
    CHECK(pa::aged(0.6f, 0.5f) == Approx(0.6f));
    CHECK(pa::aged(1.0f, 1.0f) == Approx(1.0f));
    CHECK(pa::aged(0.0f, 1.0f) == 0.0f);
    // A light touch stays light; a heavy one heads for ruin.
    CHECK(pa::aged(0.1f, 1.0f) == Approx(0.19f));
    CHECK(pa::aged(0.8f, 1.0f) == Approx(0.96f));
    for (float a = 0.0f; a <= 1.0f; a += 0.1f)
        CHECK(pa::aged(0.5f, a + 0.05f) >= pa::aged(0.5f, a));

    // The crush mappings round-trip.
    CHECK(pa::crushBitsFor(pa::crushAmountOfBits(8.0f)) == Approx(8.0f));
    CHECK(pa::crushRateFor(pa::crushAmountOfRate(11025.0f)) == Approx(11025.0f).epsilon(1e-4));
    CHECK(pa::crushRateFor(0.0f) == Approx(48000.0f));
    CHECK(pa::wearCutoffHz(0.0f) == Approx(20000.0f));
    CHECK(pa::wearCutoffHz(1.0f) < 3000.0f);
}

TEST_CASE("Patina with every stage off passes the sound untouched", "[plugins][patina]")
{
    auto p = makePatina();
    const auto out = play(*p, 997.0, 0.5f, 0.5);
    double worst = 0.0;
    for (size_t i = 0; i < out.l.size(); ++i)
    {
        const double t = static_cast<double>(i) / rate;
        const double in = 0.5 * std::sin(2.0 * std::numbers::pi * 997.0 * t);
        worst = std::max(worst, std::abs(out.l[i] - in));
    }
    CHECK(worst < 1.0e-3);
}

TEST_CASE("Patina's defaults age a sound without changing its level much", "[plugins][patina]")
{
    auto p = makePatina(false);
    const auto out = play(*p, 440.0, 0.25f, 2.0);
    const double inRms = 0.25 / std::numbers::sqrt2;
    const double outRms = rms(out.l, static_cast<size_t>(rate));
    CHECK(std::abs(db(outRms / inRms)) < 2.0);
    // No surprise level anywhere in Age's travel.
    for (const float a : {0.0f, 25.0f, 75.0f, 100.0f})
    {
        auto q = makePatina(false);
        q->setParameter(pa::age, a);
        const auto o = play(*q, 440.0, 0.25f, 1.5);
        INFO("Age " << a);
        CHECK(std::abs(db(rms(o.l, static_cast<size_t>(rate)) / inRms)) < 4.0);
    }
}

TEST_CASE("Heat holds its level across the drive and adds the character's harmonics", "[plugins][patina]")
{
    const auto run = [](int character, float drive)
    {
        auto p = makePatina();
        p->setParameter(pa::heatOn, 1.0f);
        p->setParameter(pa::heatCharacter, static_cast<float>(character));
        p->setParameter(pa::heatDrive, drive);
        return play(*p, 500.0, 0.25f, 0.6);
    };
    const size_t from = static_cast<size_t>(0.2 * rate), to = static_cast<size_t>(0.6 * rate);
    const double inRms = 0.25 / std::numbers::sqrt2;
    for (int character = 0; character < 4; ++character)
        for (const float drive : {0.0f, 50.0f, 100.0f})
        {
            INFO("character " << character << " drive " << drive);
            const auto out = run(character, drive);
            CHECK(std::abs(db(rms(out.l, from, to) / inRms)) < 4.0);
        }
    // Clean at no drive, harmonics with it; the tube's are even, tape's odd.
    const auto harmonics = [&](const Stereo& s, double h)
    { return amplitudeAt(s.l, 500.0 * h, from, to) / amplitudeAt(s.l, 500.0, from, to); };
    const auto cleanTube = run(pa::tube, 0.0f);
    const auto hotTube = run(pa::tube, 90.0f);
    const auto hotTape = run(pa::tape, 90.0f);
    CHECK(harmonics(cleanTube, 2.0) < 0.01);
    CHECK(harmonics(hotTube, 2.0) > 0.03);
    CHECK(harmonics(hotTape, 3.0) > 0.03);
    CHECK(harmonics(hotTape, 2.0) < 0.3 * harmonics(hotTape, 3.0));
    // The static curve the card draws is the one that runs: unity-ish slope
    // through zero at no drive, flattening at full.
    CHECK(pa::heatTransfer(pa::tape, 0.0f, 0.1f) == Approx(0.1f).margin(0.005f));
    CHECK(pa::heatTransfer(pa::tape, 1.0f, 1.0f) < 1.5f * pa::heatTransfer(pa::tape, 1.0f, 0.25f));
    // The fuzz is starved: quiet signals sputter out.
    CHECK(std::abs(pa::heatTransfer(pa::fuzz, 1.0f, 0.001f)) < 1.0e-6f);
}

TEST_CASE("Heat's antiderivative anti-aliasing keeps aliases low", "[plugins][patina]")
{
    // A 7 kHz tone hard into the tape curve: its 7th harmonic (49 kHz)
    // would fold to 1 kHz. ADAA holds it well under the harmonics that remain.
    auto p = makePatina();
    p->setParameter(pa::heatOn, 1.0f);
    p->setParameter(pa::heatCharacter, static_cast<float>(pa::iron));
    p->setParameter(pa::heatDrive, 100.0f);
    const auto out = play(*p, 7000.0, 0.5f, 0.5);
    const size_t from = static_cast<size_t>(0.1 * rate), to = out.l.size();
    const double fundamental = amplitudeAt(out.l, 7000.0, from, to);
    const double alias = amplitudeAt(out.l, 1000.0, from, to);
    CHECK(db(alias / fundamental) < -40.0);
}

TEST_CASE("Grain adds its noise at its level, riding or ducking under the music", "[plugins][patina]")
{
    const auto noiseIn = [](int source, float follow, float toneAmplitude)
    {
        auto p = makePatina();
        p->setParameter(pa::grainOn, 1.0f);
        p->setParameter(pa::grainSource, static_cast<float>(source));
        p->setParameter(pa::grainAmount, 60.0f);
        p->setParameter(pa::grainFollow, follow);
        auto out = play(*p, toneAmplitude > 0.0f ? 300.0 : 0.0, toneAmplitude, 2.0);
        // Take the tone out: what is left is the noise.
        for (size_t i = 0; i < out.l.size(); ++i)
            out.l[i] -=
                toneAmplitude *
                static_cast<float>(std::sin(2.0 * std::numbers::pi * 300.0 * static_cast<double>(i) / rate));
        return out;
    };
    const size_t from = static_cast<size_t>(0.5 * rate);
    for (int source = 0; source <= pa::air; ++source)
    {
        INFO("source " << source);
        const auto quiet = noiseIn(source, 0.0f, 0.0f);
        const double level = db(rms(quiet.l, from));
        // -60 + 44 * 0.6 = -33.6 dB, give or take each source's character.
        CHECK(level > -46.0);
        CHECK(level < -24.0);
        // Riding: gone in silence.
        const auto ride = noiseIn(source, 100.0f, 0.0f);
        CHECK(db(rms(ride.l, from)) < level - 40.0);
    }
    // Ducking: quieter under a loud tone than in silence.
    const auto steady = noiseIn(pa::hiss, 0.0f, 0.0f);
    const auto ducked = noiseIn(pa::hiss, -100.0f, 0.5f);
    CHECK(db(rms(ducked.l, from)) < db(rms(steady.l, from)) - 10.0);
    // Hum sits on its mains frequency; vinyl crackles (peaks far above its body).
    const auto hum = noiseIn(pa::hum60, 0.0f, 0.0f);
    CHECK(amplitudeAt(hum.l, 60.0, from, hum.l.size()) > 3.0 * amplitudeAt(hum.l, 50.0, from, hum.l.size()));
    const auto vinyl = noiseIn(pa::vinyl, 0.0f, 0.0f);
    float peak = 0.0f;
    for (size_t i = from; i < vinyl.l.size(); ++i)
        peak = std::max(peak, std::abs(vinyl.l[i]));
    CHECK(db(peak / rms(vinyl.l, from)) > 15.0);
    // No noise at all at no amount.
    auto none = makePatina();
    none->setParameter(pa::grainOn, 1.0f);
    none->setParameter(pa::grainAmount, 0.0f);
    CHECK(rms(play(*none, 0.0, 0.0f, 0.5).l) == 0.0);
}

TEST_CASE("Drift bends the pitch, apart on the two sides with Stereo, and never when still",
          "[plugins][patina]")
{
    const auto spread = [](float wow, float flutter, float stereo, int machine)
    {
        auto p = makePatina();
        p->setParameter(pa::driftOn, 1.0f);
        p->setParameter(pa::driftMachine, static_cast<float>(machine));
        p->setParameter(pa::driftWow, wow);
        p->setParameter(pa::driftFlutter, flutter);
        p->setParameter(pa::driftStereo, stereo);
        p->setParameter(pa::driftBreath, 0.0f);
        const auto out = play(*p, 1000.0, 0.5f, 4.0);
        const size_t from = static_cast<size_t>(1.0 * rate);
        double difference = 0.0;
        for (size_t i = from; i < out.l.size(); ++i)
            difference = std::max(difference, static_cast<double>(std::abs(out.l[i] - out.r[i])));
        return std::pair{pitchSpreadCents(out.l, from), difference};
    };
    const auto [still, stillApart] = spread(0.0f, 0.0f, 0.0f, pa::reel);
    CHECK(still < 0.5);
    CHECK(stillApart < 1.0e-4);
    const auto [wow, wowApart] = spread(80.0f, 0.0f, 0.0f, pa::reel);
    CHECK(wow > 10.0);
    CHECK(wowApart < 1.0e-3); // mono: the sides move together
    const auto [flutter, flutterApart] = spread(0.0f, 80.0f, 0.0f, pa::deck);
    CHECK(flutter > 3.0);
    const auto [disc, discApart] = spread(60.0f, 0.0f, 100.0f, pa::disc);
    CHECK(disc > 8.0);
    const auto [wide, wideApart] = spread(80.0f, 40.0f, 100.0f, pa::reel);
    CHECK(wideApart > 0.05); // the sides wobble apart
}

TEST_CASE("Crush reduces bits and rate, with and without the converter's filters", "[plugins][patina]")
{
    const auto crushed = [](float bits, float hz, int flavour, double toneHz)
    {
        auto p = makePatina();
        p->setParameter(pa::crushOn, 1.0f);
        p->setParameter(pa::crushBits, bits);
        p->setParameter(pa::crushRate, hz);
        p->setParameter(pa::crushFlavour, static_cast<float>(flavour));
        return play(*p, toneHz, 0.5f, 0.5);
    };
    const size_t from = static_cast<size_t>(0.1 * rate);
    // Four bits, full rate, no filters: a handful of levels.
    const auto bits = crushed(4.0f, 48000.0f, pa::hard, 300.0);
    std::set<float> levels(bits.l.begin() + static_cast<long>(from), bits.l.end());
    CHECK(levels.size() <= 17);
    // 4 kHz, no filters: each value held for 12 samples.
    const auto held = crushed(16.0f, 4000.0f, pa::hard, 300.0);
    int changes = 0;
    for (size_t i = from + 1; i < held.l.size(); ++i)
        changes += held.l[i] != held.l[i - 1] ? 1 : 0;
    const double expected = static_cast<double>(held.l.size() - from) / 12.0;
    CHECK(changes == Approx(expected).epsilon(0.05));
    // A 10 kHz tone at a 16 kHz rate folds to 6 kHz unfiltered; the filtered
    // converter keeps it out.
    const auto hard = crushed(16.0f, 16000.0f, pa::hard, 10000.0);
    const auto soft = crushed(16.0f, 16000.0f, pa::soft, 10000.0);
    const double hardAlias = amplitudeAt(hard.l, 6000.0, from, hard.l.size());
    const double softAlias = amplitudeAt(soft.l, 6000.0, from, soft.l.size());
    CHECK(hardAlias > 0.1);
    CHECK(softAlias < 0.1 * hardAlias);
}

TEST_CASE("Chamber's tail lasts about its decay time", "[plugins][patina]")
{
    for (int space = 0; space <= pa::plate; ++space)
    {
        INFO("space " << space);
        auto p = makePatina();
        p->setParameter(pa::chamberOn, 1.0f);
        p->setParameter(pa::chamberType, static_cast<float>(space));
        p->setParameter(pa::chamberMix, 100.0f);
        p->setParameter(pa::chamberDecay, 50.0f);
        play(*p, 0.0, 0.0f, 0.05); // let the stage fade in
        // A burst of noise, then silence.
        std::vector<float> l(block), r(block);
        std::vector<float> tail;
        std::uint32_t seed = 1;
        const double seconds = pa::chamberDecaySeconds(space, 0.5f) * 1.2 + 0.2;
        for (int done = 0; done < static_cast<int>(seconds * rate); done += block)
        {
            for (int i = 0; i < block; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                const float noise = static_cast<float>(seed >> 8) / 16777216.0f - 0.5f;
                l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = done < 0.1 * rate ? noise : 0.0f;
            }
            p->process(l.data(), r.data(), block);
            tail.insert(tail.end(), l.begin(), l.end());
        }
        const double rt = pa::chamberDecaySeconds(space, 0.5f);
        const auto window = [&](double at) // 50 ms of RMS starting `at` seconds after the burst
        {
            const auto start = static_cast<size_t>((0.1 + at) * rate);
            return rms(tail, start, start + static_cast<size_t>(0.05 * rate));
        };
        const double early = window(0.02), half = window(rt * 0.5);
        // About 30 dB down half way through the decay; generous for the
        // spring's chirps and the plate's build-up.
        CHECK(db(half / early) < -15.0);
        CHECK(db(half / early) > -50.0);
    }
}

TEST_CASE("Wear dulls the highs, and its dropouts dip the level", "[plugins][patina]")
{
    const auto through = [](float loss, double hz)
    {
        auto p = makePatina();
        p->setParameter(pa::wearOn, 1.0f);
        p->setParameter(pa::wearLoss, loss);
        p->setParameter(pa::wearDropouts, 0.0f);
        p->setParameter(pa::wearSkew, 0.0f);
        const auto out = play(*p, hz, 0.5f, 0.5);
        return db(rms(out.l, static_cast<size_t>(0.2 * rate)) / (0.5 / std::numbers::sqrt2));
    };
    CHECK(through(100.0f, 10000.0) < -12.0);
    CHECK(std::abs(through(100.0f, 400.0)) < 2.0);
    CHECK(std::abs(through(0.0f, 10000.0)) < 0.5);

    auto p = makePatina();
    p->setParameter(pa::wearOn, 1.0f);
    p->setParameter(pa::wearLoss, 0.0f);
    p->setParameter(pa::wearDropouts, 100.0f);
    p->setParameter(pa::wearSkew, 0.0f);
    const auto* telemetry = pa::telemetryOf(p.get());
    REQUIRE(telemetry != nullptr);
    const auto out = play(*p, 400.0, 0.5f, 10.0);
    // The level over 10 ms windows: at least one dropout dips it by 3 dB.
    double deepest = 0.0;
    const size_t window = static_cast<size_t>(0.01 * rate);
    for (size_t at = static_cast<size_t>(0.5 * rate); at + window < out.l.size(); at += window)
        deepest = std::min(deepest, db(rms(out.l, at, at + window) / (0.5 / std::numbers::sqrt2)));
    CHECK(deepest < -3.0);
    // The card hears of them too.
    std::uint64_t cursor = 0;
    std::vector<pa::Telemetry::Frame> frames(1024);
    const int n = telemetry->read(cursor, frames.data(), 1024);
    REQUIRE(n > 100);
    float dip = 0.0f;
    for (int i = 0; i < n; ++i)
        dip = std::max(dip, frames[static_cast<size_t>(i)][pa::Telemetry::wearDipDb]);
    CHECK(dip > 3.0f);
}

TEST_CASE("Patina's telemetry belongs to Patina and reports each stage's amount", "[plugins][patina]")
{
    auto other = plugins::BuiltInFactory().create(plugins::types::gain);
    CHECK(pa::telemetryOf(other.get()) == nullptr);
    auto p = makePatina(false);
    const auto* telemetry = pa::telemetryOf(p.get());
    REQUIRE(telemetry != nullptr);
    play(*p, 440.0, 0.25f, 0.5);
    std::uint64_t cursor = 0;
    std::vector<pa::Telemetry::Frame> frames(256);
    const int n = telemetry->read(cursor, frames.data(), 256);
    REQUIRE(n > 50);
    const auto& last = frames[static_cast<size_t>(n - 1)];
    // Defaults at Age 50%: the amounts as set (Heat 35%), off stages at zero.
    CHECK(last[pa::Telemetry::amountHeat] == Approx(0.35f).margin(0.02f));
    CHECK(last[pa::Telemetry::amountChamber] == 0.0f);
    CHECK(last[pa::Telemetry::amountCrush] == 0.0f);
    CHECK(last[pa::Telemetry::heatLevel] == Approx(0.25f).margin(0.01f));
    CHECK(last[pa::Telemetry::wearCutoff] == Approx(pa::wearCutoffHz(0.3f)).epsilon(0.3));
    // Breath moves an amount over time, never above what is set.
    p->setParameter(pa::heatBreath, 100.0f);
    p->setParameter(pa::pace, 100.0f);
    play(*p, 440.0, 0.25f, 3.0);
    const int m = telemetry->read(cursor, frames.data(), 256);
    REQUIRE(m > 50);
    float lo = 1.0f, hi = 0.0f;
    for (int i = 0; i < m; ++i)
    {
        lo = std::min(lo, frames[static_cast<size_t>(i)][pa::Telemetry::amountHeat]);
        hi = std::max(hi, frames[static_cast<size_t>(i)][pa::Telemetry::amountHeat]);
    }
    CHECK(hi <= 0.351f);
    CHECK(hi - lo > 0.05f);
}

TEST_CASE("Switching a Patina stage or its character does not click", "[plugins][patina]")
{
    auto p = makePatina(false);
    p->setParameter(pa::chamberOn, 1.0f);
    std::vector<float> l(block), r(block);
    float previous = 0.0f, worst = 0.0f;
    double phase = 0.0;
    for (int b = 0; b < 400; ++b)
    {
        if (b == 100)
            p->setParameter(pa::heatCharacter, static_cast<float>(pa::fuzz));
        if (b == 150)
            p->setParameter(pa::heatOn, 0.0f);
        if (b == 200)
            p->setParameter(pa::chamberType, static_cast<float>(pa::plate));
        if (b == 250)
            p->setParameter(pa::driftMachine, static_cast<float>(pa::deck));
        if (b == 300)
            p->setParameter(pa::grainSource, static_cast<float>(pa::hum50));
        for (int i = 0; i < block; ++i)
        {
            l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
                0.3f * static_cast<float>(std::sin(phase));
            phase += 2.0 * std::numbers::pi * 110.0 / rate;
        }
        p->process(l.data(), r.data(), block);
        for (float v : l)
        {
            if (b > 20)
                worst = std::max(worst, std::abs(v - previous));
            previous = v;
        }
    }
    // A 110 Hz tone at 0.3 moves at most 0.0043 a sample; the noise and the
    // stages add some, but nothing like a step.
    CHECK(worst < 0.08f);
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> patinaViolations{0};
void countPatinaViolation(const rt::Violation&) noexcept
{
    patinaViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Patina processes without allocating on the real-time thread", "[plugins][patina][rt]")
{
    auto p = makePatina(false);
    for (int s = 0; s < pa::stageCount; ++s)
    {
        p->setParameter(pa::onParam(s), 1.0f);
        p->setParameter(pa::breathParam(s), 60.0f);
    }
    p->setParameter(pa::lowCut, 80.0f);
    p->setParameter(pa::highCut, 9000.0f);
    std::vector<float> l(block, 0.4f), r(block, -0.3f);
    const auto previous = rt::setViolationHandler(&countPatinaViolation);
    patinaViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int i = 0; i < 400; ++i)
        {
            if (i % 50 == 0)
            {
                const auto step = static_cast<float>((i / 50) % 4);
                p->setParameter(pa::heatCharacter, step);
                p->setParameter(pa::chamberType, std::fmod(step, 3.0f));
                p->setParameter(pa::grainSource, step);
                p->setParameter(pa::crushFlavour, step);
                p->setParameter(pa::driftMachine, std::fmod(step, 3.0f));
            }
            p->process(l.data(), r.data(), block);
        }
    }
    rt::setViolationHandler(previous);
    CHECK(patinaViolations == 0);
}
#endif

TEST_CASE("Patina's presets are full, named once, within range and play", "[plugins][patina]")
{
    const auto& infos = plugins::parameterInfo(pa::type);
    const auto& list = pa::presets();
    REQUIRE(list.size() >= 8);
    std::set<std::string> names;
    for (const auto& preset : list)
    {
        INFO(preset.name);
        CHECK(names.insert(preset.name).second);
        REQUIRE(preset.values.size() == infos.size());
        for (size_t i = 0; i < infos.size(); ++i)
        {
            CHECK(preset.values[i] >= infos[i].minimum);
            CHECK(preset.values[i] <= infos[i].maximum);
        }
        // Each one runs clean: no NaN or runaway level on a steady tone.
        auto p = makePatina(false);
        for (size_t i = 0; i < preset.values.size(); ++i)
            p->setParameter(static_cast<int>(i), preset.values[i]);
        std::vector<float> left(block), right(block);
        float peak = 0.0f;
        for (int b = 0; b < 40; ++b)
        {
            for (int n = 0; n < block; ++n)
            {
                const auto t = static_cast<double>(b * block + n) / rate;
                left[static_cast<size_t>(n)] = right[static_cast<size_t>(n)] =
                    0.3f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * t));
            }
            p->process(left.data(), right.data(), block);
            for (const float v : left)
            {
                REQUIRE(std::isfinite(v));
                peak = std::max(peak, std::abs(v));
            }
        }
        CHECK(peak > 0.01f);
        CHECK(peak < 4.0f);
    }
    // The first is the defaults, so a new Patina shows its name.
    for (size_t i = 0; i < infos.size(); ++i)
        CHECK(list.front().values[i] == infos[i].defaultValue);
}

TEST_CASE("Patina's pace, drift rate and crackle curves span their documented ranges", "[plugins][patina]")
{
    // The card prints these, so what it shows is what runs.
    CHECK(pa::paceHz(0.0f) == Approx(0.05f));
    CHECK(pa::paceHz(1.0f) == Approx(1.5f));
    CHECK(pa::driftRateScale(0.0f) == Approx(0.5f));
    CHECK(pa::driftRateScale(0.5f) == Approx(1.0f));
    CHECK(pa::driftRateScale(1.0f) == Approx(2.0f));
    CHECK(pa::crackleRate(0.0f) == Approx(2.0f));
    CHECK(pa::crackleRate(1.0f) == Approx(300.0f));
}
