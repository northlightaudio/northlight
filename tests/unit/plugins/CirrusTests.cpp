// Cirrus, the FM synth: its waves and algorithms, and how the instrument
// follows its operators' frequencies, levels, velocity and key scaling.
#include "engine/Midi.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/Cirrus.h"
#include "plugins/fm/Fm.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
namespace cr = plugins::cirrus;
namespace fm = plugins::fm;

namespace
{
constexpr int block = 256;
constexpr double twoPi = 2.0 * std::numbers::pi;

struct Stereo
{
    std::vector<float> left, right;
};

// A Cirrus with every operator silent, at ratio 1, sustaining and unscaled, and nothing
// after it: a test turns on only the operators it hears.
std::unique_ptr<engine::InsertProcessor> quiet(double rate)
{
    auto p = cr::create();
    p->prepare(rate, block);
    for (int op = 0; op < fm::operatorCount; ++op)
    {
        p->setParameter(cr::opParam(op, cr::ratio), 1.0f);
        p->setParameter(cr::opParam(op, cr::level), 0.0f);
        p->setParameter(cr::opParam(op, cr::attack), 1.0f);
        p->setParameter(cr::opParam(op, cr::sustain), 1.0f);
        p->setParameter(cr::opParam(op, cr::velocity), 0.0f);
        p->setParameter(cr::opParam(op, cr::keyScale), 0.0f);
    }
    p->setParameter(cr::width, 0.0f);
    return p;
}

// Plays `note` from the first sample for `blocks` blocks; `events` adds more.
Stereo play(engine::InsertProcessor& p,
            int note,
            int velocity,
            int blocks,
            const std::function<void(int, engine::MidiEventList&)>& events = {})
{
    Stereo out;
    std::vector<float> l(block), r(block);
    for (int b = 0; b < blocks; ++b)
    {
        engine::MidiEventList list(64);
        if (b == 0 && note >= 0)
            list.noteOn(0, static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity));
        if (events)
            events(b, list);
        p.processMidi(list, l.data(), r.data(), block);
        out.left.insert(out.left.end(), l.begin(), l.end());
        out.right.insert(out.right.end(), r.begin(), r.end());
    }
    return out;
}

// The amplitude of the component at `hz` over [from, from + length), through
// a Hann window.
double amplitude(const std::vector<float>& x, double rate, double hz, size_t from, size_t length)
{
    double re = 0.0, im = 0.0, windowSum = 0.0;
    for (size_t i = 0; i < length; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(twoPi * static_cast<double>(i) / static_cast<double>(length));
        const double phase = twoPi * hz * static_cast<double>(i) / rate;
        re += x[from + i] * w * std::cos(phase);
        im -= x[from + i] * w * std::sin(phase);
        windowSum += w;
    }
    return 2.0 * std::sqrt(re * re + im * im) / windowSum;
}

// The frequency of a near-sine over [from, from + length), from its rising
// zero crossings (interpolated between samples).
double frequency(const std::vector<float>& x, double rate, size_t from, size_t length)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (size_t i = from + 1; i < from + length; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double at = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0.0)
                first = at;
            last = at;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0.0;
}

// How bright a note is: the share of its power outside the fundamental.
double brightnessOf(const std::vector<float>& x, double rate, double hz, size_t from, size_t length)
{
    double total = 0.0;
    for (size_t i = from; i < from + length; ++i)
        total += static_cast<double>(x[i]) * x[i];
    total /= static_cast<double>(length);
    const double a = amplitude(x, rate, hz, from, length);
    return std::max(0.0, 1.0 - 0.5 * a * a / total);
}

double noteHz(int note)
{
    return 440.0 * std::exp2((note - 69) / 12.0);
}

// The level whose modulation index (in radians, at full envelope and with
// nothing scaling it) is `index`.
float levelForIndex(double index)
{
    const double gain = index / (twoPi * fm::maxIndexCycles);
    return static_cast<float>(99.0 + 8.0 * std::log2(gain));
}
} // namespace

TEST_CASE("Every Cirrus wave has no offset and peaks at one", "[plugins][cirrus]")
{
    const auto& tables = fm::waveTables();
    for (int w = 0; w < fm::waveCount; ++w)
    {
        INFO(w);
        const auto& t = tables[static_cast<size_t>(w)];
        double mean = 0.0, peak = 0.0;
        for (int i = 0; i < fm::tableSize; ++i)
        {
            mean += t[static_cast<size_t>(i)];
            peak = std::max(peak, static_cast<double>(std::abs(t[static_cast<size_t>(i)])));
        }
        CHECK(std::abs(mean / fm::tableSize) < 1.0e-6);
        CHECK(peak == Catch::Approx(1.0).margin(1.0e-6));
        CHECK(t[fm::tableSize] == t[0]);
    }
    for (int i = 0; i < 64; ++i)
    {
        const float phase = static_cast<float>(i) / 64.0f;
        CHECK(fm::waveAt(fm::Wave::sine, phase) == Catch::Approx(std::sin(twoPi * phase)).margin(1.0e-5));
    }
}

TEST_CASE("Every Cirrus algorithm is heard and runs from operator 4 down", "[plugins][cirrus]")
{
    for (int a = 0; a < fm::algorithmCount; ++a)
    {
        INFO("algorithm " << a + 1);
        const auto& alg = fm::algorithms[static_cast<size_t>(a)];
        CHECK(alg.carrierCount() >= 1);
        CHECK(alg.isCarrier(0)); // operator 1 is heard in every algorithm
        for (int op = 0; op < fm::operatorCount; ++op)
        {
            const auto mask = alg.modulators[static_cast<size_t>(op)];
            // Only higher-numbered operators modulate it.
            CHECK((mask & ((1u << (op + 1)) - 1u)) == 0u);
            // A carrier modulates nothing, and every other operator modulates something.
            bool modulates = false;
            for (int t = 0; t < fm::operatorCount; ++t)
                modulates = modulates || ((alg.modulators[static_cast<size_t>(t)] >> op) & 1u) != 0;
            CHECK(modulates != alg.isCarrier(op));
        }
    }
}

TEST_CASE("Cirrus's level steps are 0.75 dB, 99 at unity, 0 silent", "[plugins][cirrus]")
{
    CHECK(fm::levelGain(99.0f) == Catch::Approx(1.0f));
    CHECK(fm::levelGain(91.0f) == Catch::Approx(0.5f));
    CHECK(fm::levelGain(0.0f) == 0.0f);
    CHECK(20.0 * std::log10(fm::levelGain(98.0f)) == Catch::Approx(-0.7526).margin(1.0e-3));
}

TEST_CASE("A Cirrus carrier alone plays a sine at its ratio or its fixed frequency", "[plugins][cirrus]")
{
    constexpr double rate = 48000.0;
    const auto measure = [&](int note, const std::function<void(engine::InsertProcessor&)>& setUp)
    {
        auto p = quiet(rate);
        p->setParameter(cr::algorithm, 7.0f); // all four heard
        p->setParameter(cr::opParam(0, cr::level), 99.0f);
        setUp(*p);
        const auto out = play(*p, note, 100, 40);
        return frequency(out.left, rate, 4096, 4096);
    };
    CHECK(measure(69, [](auto&) {}) == Catch::Approx(440.0).epsilon(0.001));
    // Ratio 2: an octave up.
    CHECK(measure(69, [](auto& p) { p.setParameter(cr::opParam(0, cr::ratio), 2.0f); }) ==
          Catch::Approx(880.0).epsilon(0.001));
    // Ratio 0.5: an octave down.
    CHECK(measure(69, [](auto& p) { p.setParameter(cr::opParam(0, cr::ratio), 0.0f); }) ==
          Catch::Approx(220.0).epsilon(0.001));
    // Ratio 3 with 50% fine: 4.5 times.
    CHECK(measure(57,
                  [](auto& p)
                  {
                      p.setParameter(cr::opParam(0, cr::ratio), 3.0f);
                      p.setParameter(cr::opParam(0, cr::fine), 50.0f);
                  }) == Catch::Approx(990.0).epsilon(0.001));
    // Fixed at 300 Hz, whatever the note.
    for (const int note : {40, 69, 90})
    {
        INFO(note);
        CHECK(measure(note,
                      [](auto& p)
                      {
                          p.setParameter(cr::opParam(0, cr::fixed), 1.0f);
                          p.setParameter(cr::opParam(0, cr::fixedHz), 300.0f);
                      }) == Catch::Approx(300.0).epsilon(0.001));
    }

    // And it is a sine: nothing at the second harmonic.
    auto p = quiet(rate);
    p->setParameter(cr::algorithm, 0.0f); // 4 > 3 > 2 > 1, the modulators silent
    p->setParameter(cr::opParam(0, cr::level), 99.0f);
    const auto out = play(*p, 57, 100, 40);
    const double f = noteHz(57);
    const double fundamental = amplitude(out.left, rate, f, 4096, 4096);
    CHECK(fundamental > 0.05);
    CHECK(amplitude(out.left, rate, 2.0 * f, 4096, 4096) < fundamental * 1.0e-3);
}

TEST_CASE("A Cirrus modulator makes sidebands as deep as its index says", "[plugins][cirrus]")
{
    // Carrier at four times the note, modulator at the note, index 1 radian: the
    // carrier at J0(1), the first sidebands at J1(1), the second at J2(1).
    constexpr double rate = 48000.0;
    auto p = quiet(rate);
    p->setParameter(cr::algorithm, 0.0f); // 4 > 3 > 2 > 1
    p->setParameter(cr::opParam(0, cr::level), 99.0f);
    p->setParameter(cr::opParam(0, cr::ratio), 4.0f);
    p->setParameter(cr::opParam(1, cr::level), levelForIndex(1.0));
    const auto out = play(*p, 48, 100, 60);
    const double f = noteHz(48);
    const auto at = [&](double multiple) { return amplitude(out.left, rate, f * multiple, 6000, 8192); };
    const double carrier = at(4.0);
    CHECK(at(5.0) / carrier == Catch::Approx(0.4401 / 0.7652).epsilon(0.03));
    CHECK(at(3.0) / carrier == Catch::Approx(0.4401 / 0.7652).epsilon(0.03));
    CHECK(at(6.0) / carrier == Catch::Approx(0.1149 / 0.7652).epsilon(0.05));
}

TEST_CASE("Cirrus routes each operator as its algorithm says", "[plugins][cirrus]")
{
    // Algorithm 5 (2 > 1, 4 > 3) at full width: operator 1 is heard on the
    // left, operator 3 on the right, so silencing operator 4 changes only the
    // right and silencing operator 2 only the left.
    constexpr double rate = 48000.0;
    const auto render = [&](int silenced)
    {
        auto p = quiet(rate);
        p->setParameter(cr::algorithm, 4.0f);
        p->setParameter(cr::width, 100.0f);
        for (int op = 0; op < fm::operatorCount; ++op)
            if (op != silenced)
                p->setParameter(cr::opParam(op, cr::level), op % 2 == 0 ? 99.0f : 80.0f);
        p->setParameter(cr::opParam(2, cr::ratio), 3.0f);
        return play(*p, 60, 100, 20);
    };
    const auto all = render(-1);
    const auto differs = [](const std::vector<float>& a, const std::vector<float>& b)
    {
        double d = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
            d = std::max(d, static_cast<double>(std::abs(a[i] - b[i])));
        return d > 1.0e-4;
    };
    const auto no4 = render(3);
    CHECK_FALSE(differs(all.left, no4.left));
    CHECK(differs(all.right, no4.right));
    const auto no2 = render(1);
    CHECK(differs(all.left, no2.left));
    CHECK_FALSE(differs(all.right, no2.right));

    // In every algorithm, every operator changes the sound.
    for (int a = 0; a < fm::algorithmCount; ++a)
    {
        const auto renderAlgorithm = [&](int silenced)
        {
            auto p = quiet(rate);
            p->setParameter(cr::algorithm, static_cast<float>(a));
            for (int op = 0; op < fm::operatorCount; ++op)
                if (op != silenced)
                    p->setParameter(cr::opParam(op, cr::level), 85.0f);
            return play(*p, 60, 100, 8).left;
        };
        const auto full = renderAlgorithm(-1);
        for (int op = 0; op < fm::operatorCount; ++op)
        {
            INFO("algorithm " << a + 1 << ", operator " << op + 1);
            CHECK(differs(full, renderAlgorithm(op)));
        }
    }
}

TEST_CASE("Cirrus's feedback brightens operator 4 and stays bounded", "[plugins][cirrus]")
{
    constexpr double rate = 48000.0;
    const auto render = [&](float feedbackAmount)
    {
        auto p = quiet(rate);
        p->setParameter(cr::algorithm, 7.0f); // all heard: operator 4 alone
        p->setParameter(cr::opParam(3, cr::level), 99.0f);
        p->setParameter(cr::feedback, feedbackAmount);
        return play(*p, 57, 127, 40).left;
    };
    const double f = noteHz(57);
    const auto plain = render(0.0f);
    const auto fed = render(0.6f);
    CHECK(amplitude(plain, rate, 2.0 * f, 4096, 4096) < 1.0e-4);
    CHECK(amplitude(fed, rate, 2.0 * f, 4096, 4096) > 0.2 * amplitude(fed, rate, f, 4096, 4096));
    for (const float x : render(1.0f))
    {
        REQUIRE(std::isfinite(x));
        REQUIRE(std::abs(x) <= 1.0f);
    }
}

TEST_CASE("Velocity on a Cirrus modulator brightens a note; key scale darkens high notes",
          "[plugins][cirrus]")
{
    constexpr double rate = 48000.0;
    // The second harmonic against the fundamental: how bright a 1:1 pair is.
    const auto brightness =
        [&](int note, int velocity, const std::function<void(engine::InsertProcessor&)>& setUp)
    {
        auto p = quiet(rate);
        p->setParameter(cr::algorithm, 0.0f);
        p->setParameter(cr::opParam(0, cr::level), 99.0f);
        p->setParameter(cr::opParam(1, cr::level), 70.0f);
        setUp(*p);
        const auto out = play(*p, note, velocity, 40);
        return brightnessOf(out.left, rate, noteHz(note), 4096, 4096);
    };
    const auto velocityOnModulator = [](engine::InsertProcessor& p)
    { p.setParameter(cr::opParam(1, cr::velocity), 1.0f); };
    CHECK(brightness(60, 127, velocityOnModulator) > 2.0 * brightness(60, 40, velocityOnModulator));
    // Without it, velocity changes nothing about the colour.
    CHECK(brightness(60, 127, [](auto&) {}) == Catch::Approx(brightness(60, 40, [](auto&) {})).epsilon(0.01));

    const auto keyScaled = [](engine::InsertProcessor& p)
    { p.setParameter(cr::opParam(1, cr::keyScale), -1.0f); };
    CHECK(brightness(96, 100, keyScaled) < 0.5 * brightness(96, 100, [](auto&) {}));
    // Below middle C it does nothing.
    CHECK(brightness(48, 100, keyScaled) == Catch::Approx(brightness(48, 100, [](auto&) {})).epsilon(0.01));
}

TEST_CASE("Cirrus's Tone macro moves every modulator at once", "[plugins][cirrus]")
{
    constexpr double rate = 48000.0;
    const auto brightness = [&](float toneValue)
    {
        auto p = quiet(rate);
        p->setParameter(cr::algorithm, 0.0f);
        p->setParameter(cr::opParam(0, cr::level), 99.0f);
        p->setParameter(cr::opParam(1, cr::level), 62.0f);
        p->setParameter(cr::tone, toneValue);
        const auto out = play(*p, 60, 100, 40);
        return brightnessOf(out.left, rate, noteHz(60), 4096, 4096);
    };
    CHECK(brightness(100.0f) > 2.0 * brightness(0.0f));
    CHECK(brightness(-100.0f) < 0.5 * brightness(0.0f));
}

TEST_CASE("Cirrus on High keeps a bright high note's fold-back far under Eco's", "[plugins][cirrus]")
{
    // A 2900 Hz pair at 1:1, modulated hard: its tenth harmonic (29 kHz) is past
    // the base rate's Nyquist and would fold back to 19 kHz, which is not a
    // harmonic. On High the down-sampler removes it before it can fold.
    constexpr double rate = 48000.0;
    const auto foldBack = [&](float qualityValue)
    {
        auto p = quiet(rate);
        p->setParameter(cr::quality, qualityValue);
        p->setParameter(cr::algorithm, 0.0f);
        for (int op = 0; op < 2; ++op)
        {
            p->setParameter(cr::opParam(op, cr::fixed), 1.0f);
            p->setParameter(cr::opParam(op, cr::fixedHz), 2900.0f);
        }
        p->setParameter(cr::opParam(0, cr::level), 99.0f);
        p->setParameter(cr::opParam(1, cr::level), 96.0f);
        const auto out = play(*p, 60, 100, 80).left;
        const double fundamental = amplitude(out, rate, 2900.0, 8192, 8192);
        const double alias = amplitude(out, rate, 19000.0, 8192, 8192);
        return 20.0 * std::log10(alias / fundamental + 1.0e-12);
    };
    const double high = foldBack(1.0f);
    const double eco = foldBack(0.0f);
    INFO("High " << high << " dB, Eco " << eco << " dB");
    CHECK(high < -80.0);
    CHECK(high < eco - 40.0);
}

TEST_CASE("Cirrus sounds the same at 44.1 kHz and 96 kHz", "[plugins][cirrus]")
{
    const auto spectrum = [](double rate)
    {
        auto p = quiet(rate);
        p->setParameter(cr::algorithm, 0.0f);
        p->setParameter(cr::opParam(0, cr::level), 99.0f);
        p->setParameter(cr::opParam(1, cr::level), 82.0f);
        p->setParameter(cr::opParam(1, cr::ratio), 2.0f);
        p->setParameter(cr::opParam(1, cr::decay), 400.0f);
        p->setParameter(cr::opParam(1, cr::sustain), 0.3f);
        const auto blocks = static_cast<int>(rate * 1.0 / block);
        const auto out = play(*p, 57, 100, blocks);
        const double f = noteHz(57);
        const auto from = static_cast<size_t>(rate * 0.6);
        const auto length = static_cast<size_t>(rate * 0.2);
        std::vector<double> h;
        for (int k = 1; k <= 6; ++k)
            h.push_back(amplitude(out.left, rate, k * f, from, length));
        return h;
    };
    const auto a = spectrum(44100.0);
    const auto b = spectrum(96000.0);
    for (size_t k = 0; k < a.size(); ++k)
    {
        INFO("harmonic " << k + 1);
        CHECK(b[k] == Catch::Approx(a[k]).margin(0.002 + 0.03 * a[k]));
    }
}

TEST_CASE("Cirrus's voices end after their release and leave silence", "[plugins][cirrus]")
{
    auto p = cr::create();
    p->prepare(48000.0, block);
    const auto out = play(*p,
                          -1,
                          0,
                          320,
                          [](int b, engine::MidiEventList& list)
                          {
                              if (b == 0)
                                  for (const int n : {60, 64, 67})
                                      list.noteOn(0, static_cast<std::uint8_t>(n), 100);
                              if (b == 40)
                                  for (const int n : {60, 64, 67})
                                      list.noteOff(0, static_cast<std::uint8_t>(n));
                          });
    double early = 0.0;
    for (size_t i = 1000; i < 4000; ++i)
        early = std::max(early, static_cast<double>(std::abs(out.left[i])));
    CHECK(early > 0.01);
    for (size_t i = out.left.size() - block; i < out.left.size(); ++i)
    {
        REQUIRE(out.left[i] == 0.0f);
        REQUIRE(out.right[i] == 0.0f);
    }
}

TEST_CASE("Cirrus in Mono glides between legato notes and not between detached ones", "[plugins][cirrus]")
{
    constexpr double rate = 48000.0;
    const auto setUp = [&](float glideMs)
    {
        auto p = quiet(rate);
        p->setParameter(cr::algorithm, 7.0f);
        p->setParameter(cr::opParam(0, cr::level), 99.0f);
        p->setParameter(cr::voiceMode, 1.0f);
        p->setParameter(cr::glide, glideMs);
        return p;
    };
    // A3 held, then A4 (legato): with glide, 20 ms later the pitch is between.
    const auto legato = [&](float glideMs)
    {
        auto p = setUp(glideMs);
        const auto out = play(*p,
                              57,
                              100,
                              30,
                              [](int b, engine::MidiEventList& list)
                              {
                                  if (b == 10)
                                      list.noteOn(0, 69, 100);
                              });
        return frequency(out.left, rate, 10 * block + 960, 512);
    };
    CHECK(legato(0.0f) == Catch::Approx(440.0).epsilon(0.005));
    const double gliding = legato(100.0f);
    CHECK(gliding > 240.0);
    CHECK(gliding < 400.0);

    // Released first: the next note starts on its own pitch.
    auto p = setUp(100.0f);
    p->setParameter(cr::opParam(0, cr::release), 5.0f);
    const auto out = play(*p,
                          57,
                          100,
                          30,
                          [](int b, engine::MidiEventList& list)
                          {
                              if (b == 8)
                                  list.noteOff(0, 57);
                              if (b == 10)
                                  list.noteOn(0, 69, 100);
                          });
    CHECK(frequency(out.left, rate, 10 * block + 960, 512) == Catch::Approx(440.0).epsilon(0.005));
}

TEST_CASE("Cirrus's pitch envelope starts a note away from its pitch and settles", "[plugins][cirrus]")
{
    constexpr double rate = 48000.0;
    auto p = quiet(rate);
    p->setParameter(cr::algorithm, 7.0f);
    p->setParameter(cr::opParam(0, cr::level), 99.0f);
    p->setParameter(cr::pitchEnvelope, 12.0f);
    p->setParameter(cr::pitchTime, 300.0f);
    const auto out = play(*p, 57, 100, 200);
    // At the start, about an octave up; well after the time, on the note.
    CHECK(frequency(out.left, rate, 0, 256) > 1.6 * 220.0);
    CHECK(frequency(out.left, rate, 40000, 4096) == Catch::Approx(220.0).epsilon(0.002));
}

#ifdef NORTHLIGHT_RT_CHECKS
namespace
{
std::atomic<int> cirrusViolations{0};
void countCirrusViolation(const rt::Violation&) noexcept
{
    cirrusViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Cirrus plays and changes its settings without allocating", "[plugins][cirrus][rt]")
{
    auto p = cr::create();
    p->prepare(48000.0, block);
    engine::MidiEventList events(64);
    std::vector<float> l(block), r(block);
    const auto previous = rt::setViolationHandler(&countCirrusViolation);
    cirrusViolations = 0;
    for (int b = 0; b < 200; ++b)
    {
        events.clear();
        if (b % 6 == 0)
            events.noteOn(b % block, static_cast<std::uint8_t>(40 + b % 40), 90);
        if (b % 6 == 3)
            events.noteOff(5, static_cast<std::uint8_t>(40 + (b - 3) % 40));
        p->setParameter(cr::algorithm, static_cast<float>(b % fm::algorithmCount));
        p->setParameter(cr::quality, static_cast<float>((b / 50) % 2));
        p->setParameter(cr::voiceMode, static_cast<float>((b / 100) % 2));
        p->setParameter(cr::lfoSync, b > 150 ? 8.0f : 0.0f);
        rt::ScopedRealtimeSection section;
        p->processMidi(events, l.data(), r.data(), block);
    }
    rt::setViolationHandler(previous);
    CHECK(cirrusViolations == 0);
}
#endif

TEST_CASE("Every Cirrus preset sounds and stays below full scale across the keyboard", "[plugins][cirrus]")
{
    constexpr double rate = 48000.0;
    const auto& list = cr::presets();
    REQUIRE(list.size() == 10);
    for (const auto& preset : list)
    {
        INFO(preset.name);
        for (const int note : {24, 48, 60, 72, 96})
        {
            INFO("note " << note);
            auto p = cr::create();
            p->prepare(rate, block);
            for (size_t i = 0; i < preset.values.size(); ++i)
                p->setParameter(static_cast<int>(i), preset.values[i]);
            const auto out = play(*p, note, 127, 60);
            float peak = 0.0f;
            for (size_t i = 0; i < out.left.size(); ++i)
                peak = std::max({peak, std::abs(out.left[i]), std::abs(out.right[i])});
            CHECK(peak > 0.01f);
            CHECK(peak < 1.0f);
        }
    }
}

TEST_CASE("A watched Cirrus publishes its newest voice's operator levels and modulation depth",
          "[plugins][cirrus]")
{
    auto p = quiet(48000.0);
    p->setParameter(cr::algorithm, 4.0f); // 2 into 1, 4 into 3
    p->setParameter(cr::opParam(0, cr::level), 99.0f);
    p->setParameter(cr::opParam(1, cr::level), 80.0f);
    const auto* activity = cr::activity(*p);
    REQUIRE(activity != nullptr);
    REQUIRE(p->feedback() != nullptr);

    // Unwatched, nothing is written.
    play(*p, 60, 127, 8);
    CHECK(activity->level[0].load() == 0.0f);

    p->feedback()->setWatched(true);
    play(*p, -1, 0, 8);
    CHECK(activity->level[0].load() == Catch::Approx(1.0f).margin(0.02));
    CHECK(activity->level[1].load() == Catch::Approx(fm::levelGain(80.0f)).margin(0.02));
    CHECK(activity->level[2].load() == 0.0f); // silent operators read 0
    const float depth = activity->depth.load();
    CHECK(depth ==
          Catch::Approx(2.0 * std::numbers::pi * fm::maxIndexCycles * fm::levelGain(80.0f)).epsilon(0.05));

    // Deeper modulation reads deeper; silence reads nothing.
    p->setParameter(cr::opParam(1, cr::level), 90.0f);
    play(*p, -1, 0, 4);
    CHECK(activity->depth.load() > depth * 1.5f);
    for (int op = 0; op < fm::operatorCount; ++op)
        p->setParameter(cr::opParam(op, cr::release), 1.0f);
    play(*p,
         -1,
         0,
         40,
         [](int b, engine::MidiEventList& e)
         {
             if (b == 0)
                 e.noteOff(0, 60);
         });
    CHECK(activity->level[0].load() == 0.0f);
    CHECK(activity->depth.load() == 0.0f);
}

TEST_CASE("Bessel functions give FM's sideband amplitudes", "[plugins][cirrus]")
{
    // Reference values (Abramowitz and Stegun).
    CHECK(fm::besselJ(0, 0.0) == 1.0);
    CHECK(fm::besselJ(3, 0.0) == 0.0);
    CHECK(fm::besselJ(0, 1.0) == Catch::Approx(0.7651976866).epsilon(1e-8));
    CHECK(fm::besselJ(1, 1.0) == Catch::Approx(0.4400505857).epsilon(1e-8));
    CHECK(fm::besselJ(2, 1.0) == Catch::Approx(0.1149034849).epsilon(1e-8));
    CHECK(fm::besselJ(5, 10.0) == Catch::Approx(-0.2340615282).epsilon(1e-7));
    CHECK(fm::besselJ(0, 40.0) == Catch::Approx(0.0073668905).epsilon(1e-6));
    CHECK(std::abs(fm::besselJ(0, 2.404825557695773)) < 1.0e-9); // its first zero
    // J_-n = (-1)^n J_n, and J_n(-x) = (-1)^n J_n(x).
    CHECK(fm::besselJ(-1, 1.0) == Catch::Approx(-0.4400505857).epsilon(1e-8));
    CHECK(fm::besselJ(-2, 1.0) == Catch::Approx(0.1149034849).epsilon(1e-8));
    CHECK(fm::besselJ(1, -1.0) == Catch::Approx(-0.4400505857).epsilon(1e-8));
    // The sidebands' power adds up to the carrier's: sum of J_n^2 is 1.
    double power = 0.0;
    for (int n = -60; n <= 60; ++n)
        power += fm::besselJ(n, 12.6) * fm::besselJ(n, 12.6);
    CHECK(power == Catch::Approx(1.0).epsilon(1e-9));
}
