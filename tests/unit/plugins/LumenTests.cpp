// Lumen, the modelled electric piano: its pitch, how velocity and the voicing
// change the timbre, the bell, the damper and the pedal, aliasing, and the
// modulation parts it shares (plugins/dsp/Modulation.h).
#include "engine/Midi.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/Lumen.h"
#include "plugins/TestPhrase.h"
#include "plugins/dsp/Modulation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <numbers>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace northlight;
namespace lumen = plugins::lumen;

namespace
{
constexpr int block = 256;

struct Timed
{
    double seconds;
    engine::MidiEvent event;
};

engine::MidiEvent noteOn(int note, int velocity)
{
    return {0,
            engine::MidiEvent::Type::noteOn,
            0,
            static_cast<std::uint8_t>(note),
            static_cast<std::uint8_t>(velocity)};
}
engine::MidiEvent noteOff(int note)
{
    return {0, engine::MidiEvent::Type::noteOff, 0, static_cast<std::uint8_t>(note), 0};
}
engine::MidiEvent controller(int number, int value)
{
    return {0,
            engine::MidiEvent::Type::controller,
            0,
            static_cast<std::uint8_t>(number),
            static_cast<std::uint8_t>(value)};
}

// The plain voice: no tremolo, no effects, no knocks, no age, so what is left is
// the hammer, the tine and the pickup.
std::map<int, float> plain()
{
    return {{lumen::tremoloMode, 0.0f},
            {lumen::noise, 0.0f},
            {lumen::age, 0.0f},
            {lumen::drive, 0.0f},
            {lumen::phaserMix, 0.0f},
            {lumen::chorusMix, 0.0f}};
}

struct Output
{
    std::vector<float> left, right;
};

Output render(double rate,
              double seconds,
              const std::vector<Timed>& events,
              const std::map<int, float>& settings = plain())
{
    auto synth = plugins::BuiltInFactory().create(lumen::type);
    REQUIRE(synth != nullptr);
    for (const auto& [index, value] : settings)
        synth->setParameter(index, value);
    synth->prepare(rate, block);
    const int total = static_cast<int>(rate * seconds);
    Output out;
    out.left.reserve(static_cast<size_t>(total));
    out.right.reserve(static_cast<size_t>(total));
    std::vector<float> l(block), r(block);
    for (int start = 0; start < total; start += block)
    {
        engine::MidiEventList list(64);
        for (const auto& t : events)
        {
            const int at = static_cast<int>(std::lround(t.seconds * rate));
            if (at >= start && at < start + block)
            {
                auto e = t.event;
                e.sampleOffset = at - start;
                list.push(e);
            }
        }
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        synth->processMidi(list, l.data(), r.data(), block);
        out.left.insert(out.left.end(), l.begin(), l.end());
        out.right.insert(out.right.end(), r.begin(), r.end());
    }
    return out;
}

Output strike(int note,
              int velocity,
              double rate = 48000.0,
              std::map<int, float> settings = plain(),
              double seconds = 1.0)
{
    return render(rate, seconds, {{0.0, noteOn(note, velocity)}}, settings);
}

double hz(int note)
{
    return 440.0 * std::exp2((note - 69) / 12.0);
}

double rms(const std::vector<float>& x, double rate, double from, double to)
{
    const auto a = static_cast<size_t>(from * rate), b = std::min(x.size(), static_cast<size_t>(to * rate));
    double sum = 0.0;
    for (size_t i = a; i < b; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(std::max<size_t>(1, b - a)));
}

double db(double x)
{
    return 20.0 * std::log10(std::max(x, 1.0e-30));
}

// A Hann-windowed DFT bin at any frequency, over `length` seconds from `from`.
std::complex<double>
bin(const std::vector<float>& x, double rate, double from, double length, double frequency)
{
    const auto a = static_cast<size_t>(from * rate);
    const auto n = static_cast<size_t>(length * rate);
    std::complex<double> sum = 0.0;
    for (size_t i = 0; i < n && a + i < x.size(); ++i)
    {
        const double w =
            0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n));
        const double phase = -2.0 * std::numbers::pi * frequency * static_cast<double>(a + i) / rate;
        sum += static_cast<double>(x[a + i]) * w * std::complex<double>(std::cos(phase), std::sin(phase));
    }
    return sum;
}

double level(const std::vector<float>& x, double rate, double from, double length, double frequency)
{
    return std::abs(bin(x, rate, from, length, frequency));
}

// The frequency near `expected` from the phase a bin turns through between two
// windows `gap` seconds apart.
double measuredFrequency(const std::vector<float>& x, double rate, double expected, double from)
{
    const double length = 0.25, gap = 0.05;
    const auto a = bin(x, rate, from, length, expected);
    const auto b = bin(x, rate, from + gap, length, expected);
    double turn = std::arg(b / a); // the phase beyond the expected frequency's own
    return expected + turn / (2.0 * std::numbers::pi * gap);
}

void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * std::numbers::pi / static_cast<double>(len);
        const std::complex<double> w(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> u(1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto p = a[i + k], q = a[i + k + len / 2] * u;
                a[i + k] = p + q;
                a[i + k + len / 2] = p - q;
                u *= w;
            }
        }
    }
}

// Magnitudes of a Hann-windowed power-of-two FFT from `from` seconds.
std::vector<double> spectrum(const std::vector<float>& x, double rate, double from, size_t n)
{
    std::vector<std::complex<double>> a(n);
    const auto start = static_cast<size_t>(from * rate);
    for (size_t i = 0; i < n; ++i)
    {
        const double w =
            0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(n));
        a[i] = start + i < x.size() ? static_cast<double>(x[start + i]) * w : 0.0;
    }
    fft(a);
    std::vector<double> m(n / 2);
    for (size_t i = 0; i < n / 2; ++i)
        m[i] = std::abs(a[i]);
    return m;
}

// The bell partial: the tine's first beam mode, against its fundamental.
constexpr double bellRatio = 6.4;

// The overtones (harmonics 2 to 8 and the bell) against the fundamental in the
// first 40 ms, in dB.
double brightness(const std::vector<float>& x, double rate, int note)
{
    double upper = 0.0;
    for (int k = 2; k <= 8; ++k)
        upper += std::pow(level(x, rate, 0.0, 0.04, hz(note) * k), 2.0);
    upper += std::pow(level(x, rate, 0.0, 0.04, hz(note) * bellRatio), 2.0);
    return db(std::sqrt(upper) / level(x, rate, 0.0, 0.04, hz(note)));
}

} // namespace

TEST_CASE("Lumen plays each note in tune at any sample rate", "[plugins][lumen]")
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
        for (const int note : {28, 40, 52, 60, 69, 81, 93, 100})
        {
            INFO(rate << " Hz, note " << note);
            const auto out = strike(note, 90, rate);
            const double f = measuredFrequency(out.left, rate, hz(note), 0.2);
            const double cents = 1200.0 * std::log2(f / hz(note));
            CHECK(std::abs(cents) < 1.0);
        }
}

TEST_CASE("Lumen's Tune moves A3 and Age detunes each note by a fixed amount", "[plugins][lumen]")
{
    auto settings = plain();
    settings[lumen::tune] = 442.0f;
    const auto tuned = strike(69, 90, 48000.0, settings);
    CHECK(measuredFrequency(tuned.left, 48000.0, 442.0, 0.2) == Catch::Approx(442.0).margin(0.05));

    settings[lumen::tune] = 440.0f;
    settings[lumen::age] = 100.0f;
    const auto aged = strike(60, 90, 48000.0, settings);
    const auto again = strike(60, 90, 48000.0, settings);
    const double f = measuredFrequency(aged.left, 48000.0, hz(60), 0.2);
    const double cents = 1200.0 * std::log2(f / hz(60));
    CHECK(std::abs(cents) <= 5.01);
    CHECK(measuredFrequency(again.left, 48000.0, hz(60), 0.2) == Catch::Approx(f).margin(1.0e-6));
}

TEST_CASE("A harder strike on Lumen is brighter, not only louder", "[plugins][lumen]")
{
    const double rate = 48000.0;
    for (const int note : {40, 64, 88})
    {
        INFO("note " << note);
        const auto soft = strike(note, 40);
        const auto hard = strike(note, 120);
        CHECK(rms(hard.left, rate, 0.0, 0.2) > rms(soft.left, rate, 0.0, 0.2) * 1.5);
        CHECK(brightness(hard.left, rate, note) > brightness(soft.left, rate, note) + 10.0);
    }
}

TEST_CASE("Lumen plays at one level across the keyboard", "[plugins][lumen]")
{
    // The tine's swing and the strike's roll-off are evened out per note, so only
    // the timbre follows the register (as the reference set's layers do).
    for (const int velocity : {40, 100})
    {
        double lowest = 1.0e9, highest = -1.0e9;
        for (int note = 29; note <= 96; note += 6)
        {
            const auto out = strike(note, velocity, 48000.0, plain(), 0.3);
            const double l = db(level(out.left, 48000.0, 0.0, std::max(0.05, 6.0 / hz(note)), hz(note)));
            lowest = std::min(lowest, l);
            highest = std::max(highest, l);
        }
        INFO("velocity " << velocity);
        CHECK(highest - lowest < 12.0);
    }
}

TEST_CASE("Lumen's Hardness rings the bell harder", "[plugins][lumen]")
{
    // Hardness shortens the hammer's contact: the tine's beam modes, not the
    // pickup's harmonics, take it up.
    auto settings = plain();
    settings[lumen::bark] = 0.0f;
    const auto bellOf = [](const Output& out)
    {
        return db(level(out.left, 48000.0, 0.0, 0.08, hz(60) * bellRatio) /
                  level(out.left, 48000.0, 0.0, 0.08, hz(60)));
    };
    settings[lumen::hardness] = 0.0f;
    const auto soft = strike(60, 100, 48000.0, settings);
    settings[lumen::hardness] = 100.0f;
    const auto hard = strike(60, 100, 48000.0, settings);
    CHECK(bellOf(hard) > bellOf(soft) + 6.0);
}

TEST_CASE("Lumen's Bark raises the harmonics and Symmetry trades the fundamental for the second",
          "[plugins][lumen]")
{
    const double rate = 48000.0;
    const int note = 52;
    const auto harmonics = [&](const Output& out)
    {
        double upper = 0.0;
        for (int k = 2; k <= 5; ++k)
            upper += level(out.left, rate, 0.1, 0.3, hz(note) * k);
        return upper / level(out.left, rate, 0.1, 0.3, hz(note));
    };
    auto settings = plain();
    settings[lumen::bark] = 10.0f;
    const auto far = strike(note, 100, rate, settings);
    settings[lumen::bark] = 90.0f;
    const auto close = strike(note, 100, rate, settings);
    CHECK(harmonics(close) > harmonics(far) * 2.0);

    const auto second = [&](const Output& out)
    { return level(out.left, rate, 0.1, 0.3, hz(note) * 2) / level(out.left, rate, 0.1, 0.3, hz(note)); };
    settings[lumen::bark] = 40.0f;
    settings[lumen::symmetry] = 0.0f;
    const auto centred = strike(note, 100, rate, settings);
    settings[lumen::symmetry] = 60.0f;
    const auto offset = strike(note, 100, rate, settings);
    CHECK(second(centred) > second(offset) * 2.0);
}

TEST_CASE("Lumen's bell sounds in the attack and is gone soon after", "[plugins][lumen]")
{
    const double rate = 48000.0;
    const int note = 60;
    const double bellHz = hz(note) * bellRatio;
    auto settings = plain();
    settings[lumen::bark] = 0.0f; // the pickup nearly linear, so the bell's neighbours are faint
    settings[lumen::bell] = 100.0f;
    const auto out = strike(note, 110, rate, settings, 1.2);
    const auto relative = [&](double from)
    { return db(level(out.left, rate, from, 0.08, bellHz) / level(out.left, rate, from, 0.08, hz(note))); };
    CHECK(relative(0.0) > -40.0);
    CHECK(relative(0.0) - relative(0.9) > 30.0);

    settings[lumen::bell] = 0.0f;
    const auto none = strike(note, 110, rate, settings, 0.2);
    CHECK(db(level(none.left, rate, 0.0, 0.08, bellHz) / level(none.left, rate, 0.0, 0.08, hz(note))) <
          relative(0.0) - 20.0);
}

TEST_CASE("Lumen's notes ring longer in the bass than at the top", "[plugins][lumen]")
{
    const double rate = 48000.0;
    double previous = 1.0e9;
    for (const int note : {33, 52, 72, 96})
    {
        INFO("note " << note);
        const auto out = strike(note, 90, rate, plain(), 1.2);
        // dB lost from the first tenth of a second to a second later.
        const double fall = db(rms(out.left, rate, 0.05, 0.15)) - db(rms(out.left, rate, 1.05, 1.15));
        CHECK(fall > 0.0);
        if (previous < 1.0e9)
            CHECK(fall > previous);
        previous = fall;
    }
}

TEST_CASE("A released note is damped; with the sustain pedal down it rings until the pedal lifts",
          "[plugins][lumen]")
{
    const double rate = 48000.0;
    const int note = 48;
    const auto held = render(rate, 1.5, {{0.0, noteOn(note, 100)}});
    const auto released = render(rate, 1.5, {{0.0, noteOn(note, 100)}, {0.3, noteOff(note)}});
    CHECK(db(rms(released.left, rate, 0.8, 0.9)) < db(rms(held.left, rate, 0.8, 0.9)) - 30.0);

    const auto pedalled = render(rate,
                                 1.5,
                                 {{0.0, controller(64, 127)},
                                  {0.01, noteOn(note, 100)},
                                  {0.3, noteOff(note)},
                                  {1.0, controller(64, 0)}});
    CHECK(db(rms(pedalled.left, rate, 0.8, 0.9)) > db(rms(held.left, rate, 0.8, 0.9)) - 1.0);
    CHECK(db(rms(pedalled.left, rate, 1.4, 1.5)) < db(rms(held.left, rate, 1.4, 1.5)) - 30.0);
}

TEST_CASE("Striking a ringing note again does not click", "[plugins][lumen]")
{
    const double rate = 48000.0;
    const auto out =
        render(rate, 0.8, {{0.0, noteOn(60, 90)}, {0.3, noteOn(60, 90)}, {0.31, noteOn(60, 60)}});
    float peak = 0.0f, largestStep = 0.0f;
    for (size_t i = 1; i < out.left.size(); ++i)
    {
        peak = std::max(peak, std::abs(out.left[i]));
        largestStep = std::max(largestStep, std::abs(out.left[i] - out.left[i - 1]));
    }
    CHECK(peak > 0.05f);
    // A middle C moves at most about 2 pi f / rate of its swing a sample; allow the overtones.
    CHECK(largestStep < peak * 0.5f);
}

TEST_CASE("Lumen steals the quietest voice without a click when they run out", "[plugins][lumen]")
{
    const double rate = 48000.0;
    auto settings = plain();
    settings[lumen::voices] = 8.0f;
    std::vector<Timed> events;
    for (int i = 0; i < 12; ++i)
        events.push_back({0.02 * i, noteOn(48 + i, 100)});
    const auto out = render(rate, 0.6, events, settings);
    float largestStep = 0.0f, peak = 0.0f;
    for (size_t i = 1; i < out.left.size(); ++i)
    {
        peak = std::max(peak, std::abs(out.left[i]));
        largestStep = std::max(largestStep, std::abs(out.left[i] - out.left[i - 1]));
    }
    CHECK(peak > 0.05f);
    CHECK(largestStep < peak * 0.5f);
}

TEST_CASE("Lumen's top note at full strike, bark and drive aliases very little", "[plugins][lumen]")
{
    const double rate = 48000.0;
    const int note = 100;
    auto settings = plain();
    settings[lumen::bell] = 0.0f; // the tine's fundamental alone, so every other line is a harmonic
    settings[lumen::bark] = 100.0f;
    settings[lumen::symmetry] = 30.0f;
    settings[lumen::drive] = 100.0f;
    const auto out = strike(note, 127, rate, settings, 0.6);
    const size_t n = 16384;
    const auto m = spectrum(out.left, rate, 0.05, n);
    const double binHz = rate / static_cast<double>(n);
    double harmonic = 0.0, other = 0.0;
    for (size_t i = 2; i < m.size(); ++i)
    {
        const double f = static_cast<double>(i) * binHz;
        const double k = std::round(f / hz(note));
        const bool near = k >= 1.0 && std::abs(f - k * hz(note)) < 6.0 * binHz;
        (near ? harmonic : other) += m[i] * m[i];
    }
    CHECK(db(std::sqrt(other / harmonic)) < -50.0);
}

TEST_CASE("Lumen sounds alike at 48 and 96 kHz", "[plugins][lumen]")
{
    const int note = 57;
    const auto at48 = strike(note, 100, 48000.0);
    const auto at96 = strike(note, 100, 96000.0);
    for (int k = 1; k <= 4; ++k)
    {
        INFO("harmonic " << k);
        const double a = db(level(at48.left, 48000.0, 0.1, 0.2, hz(note) * k) / (0.2 * 48000.0));
        const double b = db(level(at96.left, 96000.0, 0.1, 0.2, hz(note) * k) / (0.2 * 96000.0));
        CHECK(a == Catch::Approx(b).margin(1.5));
    }
}

TEST_CASE("Lumen renders the same twice, reports no latency and falls silent", "[plugins][lumen]")
{
    const double rate = 48000.0;
    const std::vector<Timed> phrase{
        {0.0, noteOn(60, 100)}, {0.05, noteOn(64, 80)}, {0.3, noteOff(60)}, {0.3, noteOff(64)}};
    const auto a = render(rate, 4.0, phrase, {});
    const auto b = render(rate, 4.0, phrase, {});
    CHECK(a.left == b.left);
    CHECK(a.right == b.right);
    // Released and damped (the chorus and phaser are off by default): silence at the end.
    bool quiet = true;
    for (size_t i = a.left.size() - block; i < a.left.size(); ++i)
        quiet = quiet && a.left[i] == 0.0f && a.right[i] == 0.0f;
    CHECK(quiet);
    CHECK(lumen::create()->latencySamples() == 0);
}

TEST_CASE("Lumen's mod wheel rides the tremolo's depth", "[plugins][lumen]")
{
    const double rate = 48000.0;
    auto settings = plain();
    settings[lumen::tremoloMode] = 1.0f; // pan
    settings[lumen::tremoloDepth] = 0.0f;
    const auto sideOf = [&](const Output& out)
    {
        double side = 0.0;
        for (size_t i = static_cast<size_t>(0.1 * rate); i < static_cast<size_t>(0.6 * rate); ++i)
            side += std::abs(out.left[i] - out.right[i]);
        return side;
    };
    const auto still = render(rate, 0.6, {{0.0, noteOn(60, 100)}}, settings);
    const auto ridden = render(rate, 0.6, {{0.0, controller(1, 127)}, {0.0, noteOn(60, 100)}}, settings);
    CHECK(sideOf(still) < 1.0e-3);
    CHECK(sideOf(ridden) > 1.0);
}

TEST_CASE("Lumen's note model: long in the bass, damped faster than it rings, a shorter strike when harder",
          "[plugins][lumen]")
{
    std::vector<float> values;
    for (const auto& info : lumen::parameterInfo())
        values.push_back(info.defaultValue);
    const auto low = lumen::noteModel(28, values.data());
    const auto high = lumen::noteModel(100, values.data());
    CHECK(low.t60[0] > high.t60[0] * 5.0);
    CHECK(low.contactSeconds > high.contactSeconds);
    for (const auto& m : {low, high})
    {
        CHECK(m.damperT60 <= m.t60[0]);
        for (size_t k = 1; k < m.t60.size(); ++k)
            CHECK(m.t60[k] < m.t60[k - 1]);
    }
    values[lumen::hardness] = 100.0f;
    const double hard = lumen::noteModel(60, values.data()).contactSeconds;
    values[lumen::hardness] = 0.0f;
    CHECK(hard * 3.9 < lumen::noteModel(60, values.data()).contactSeconds);
    CHECK(lumen::strikeSpeed(127, 0.5f) == Catch::Approx(1.0));
    CHECK(lumen::strikeSpeed(64, 1.0f) < lumen::strikeSpeed(64, 0.0f));
    // The pickup's curve: steepest slope 1 wherever the voicing puts it.
    for (const float bark : {0.0f, 0.5f, 1.0f})
    {
        double steepest = 0.0;
        for (double x = -3.0; x < 3.0; x += 1.0e-4)
            steepest = std::max(
                steepest,
                std::abs(lumen::pickupFlux(x + 1.0e-5, bark, 0.5f) - lumen::pickupFlux(x, bark, 0.5f)) /
                    1.0e-5);
        CHECK(steepest == Catch::Approx(1.0).margin(0.01));
    }
}

TEST_CASE("The modulation parts: an equal-power pan, a still amplitude tremolo at depth 0, a moving phaser",
          "[plugins][lumen][dsp]")
{
    using plugins::dsp::Tremolo;
    Tremolo t;
    for (int i = 0; i < 1000; ++i)
    {
        float l = 0.5f, r = 0.5f;
        t.process(l, r, Tremolo::Mode::pan, 0.0013, 1.0f, 0.4f);
        // Mono in: the two sides' power stays that of the centre.
        CHECK(l * l + r * r == Catch::Approx(0.5f).margin(1.0e-4));
    }
    float l = 0.5f, r = 0.5f;
    t.process(l, r, Tremolo::Mode::amplitude, 0.0013, 0.0f, 0.0f);
    CHECK(l == 0.5f);

    // The phaser's notch moves: a fixed tone's level through it changes over a sweep.
    plugins::dsp::Phaser phaser;
    const double rate = 48000.0;
    phaser.prepare(rate);
    const int n = static_cast<int>(rate * 2.0);
    std::vector<float> left(static_cast<size_t>(n)), right(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        left[static_cast<size_t>(i)] = right[static_cast<size_t>(i)] =
            static_cast<float>(std::sin(2.0 * std::numbers::pi * 700.0 * i / rate));
    phaser.process(left.data(), right.data(), n, 1.0f, 1.0f, 0.0f, 1.0f);
    double lowest = 1.0e9, highest = 0.0;
    for (int w = 0; w < 40; ++w)
    {
        const double e = rms(left, rate, 0.05 * w, 0.05 * w + 0.01);
        lowest = std::min(lowest, e);
        highest = std::max(highest, e);
    }
    CHECK(db(highest) - db(lowest) > 10.0);
}

TEST_CASE("Lumen's tremolo follows the tempo when synced", "[plugins][lumen]")
{
    const double rate = 48000.0;
    auto synth = lumen::create();
    synth->setParameter(lumen::noise, 0.0f);
    synth->setParameter(lumen::tremoloMode, 2.0f); // amplitude: the level dips once a cycle
    synth->setParameter(lumen::tremoloDepth, 100.0f);
    synth->setParameter(lumen::tremoloShape, 0.0f);
    synth->setParameter(lumen::tremoloSync, 11.0f); // 1/4
    synth->setParameter(lumen::decay, 400.0f);
    synth->prepare(rate, block);
    engine::PlayHead head;
    head.tempoBpm = 150.0; // a quarter note is 0.4 s
    head.sampleRate = rate;
    std::vector<float> out;
    std::vector<float> l(block), r(block);
    for (int b = 0; b < static_cast<int>(rate * 2.0) / block; ++b)
    {
        synth->setPlayHead(head);
        engine::MidiEventList list(4);
        if (b == 0)
            list.push(noteOn(40, 110));
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        synth->processMidi(list, l.data(), r.data(), block);
        out.insert(out.end(), l.begin(), l.end());
    }
    // The envelope's strongest period between 0.2 and 0.6 s.
    std::vector<double> env;
    const int hop = 240;
    for (size_t i = 0; i + hop <= out.size(); i += hop)
        env.push_back(rms(out, rate, static_cast<double>(i) / rate, static_cast<double>(i + hop) / rate));
    double bestScore = -1.0e9;
    int bestLag = 0;
    for (int lag = 40; lag <= 120; ++lag) // 0.2 .. 0.6 s in hops of 5 ms
    {
        double score = 0.0;
        for (size_t i = 40; i + static_cast<size_t>(lag) < env.size(); ++i)
            score += env[i] * env[i + static_cast<size_t>(lag)];
        score /= static_cast<double>(env.size() - 40 - static_cast<size_t>(lag));
        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }
    CHECK(bestLag * 0.005 == Catch::Approx(0.4).margin(0.02));
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> lumenViolations{0};
void countLumenViolation(const rt::Violation&) noexcept
{
    lumenViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Lumen does not allocate on a real-time thread, through note storms and changes",
          "[plugins][lumen][rt]")
{
    auto synth = lumen::create();
    synth->prepare(48000.0, block);
    std::vector<float> l(block), r(block);
    engine::MidiEventList list(128);
    const auto previous = rt::setViolationHandler(&countLumenViolation);
    lumenViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int b = 0; b < 200; ++b)
        {
            list.clear();
            for (int i = 0; i < 40; ++i)
                list.push({i * 6,
                           engine::MidiEvent::Type::noteOn,
                           0,
                           static_cast<std::uint8_t>(30 + (b * 7 + i) % 70),
                           static_cast<std::uint8_t>(20 + i * 2)});
            if (b % 3 == 0)
                list.push(controller(64, b % 2 == 0 ? 127 : 0));
            if (b == 50)
                synth->setParameter(lumen::bark, 90.0f);
            if (b == 80)
                synth->setParameter(lumen::phaserMix, 100.0f);
            if (b == 90)
                synth->setParameter(lumen::chorusMix, 100.0f);
            synth->processMidi(list, l.data(), r.data(), block);
        }
    }
    rt::setViolationHandler(previous);
    CHECK(lumenViolations == 0);
}
#endif

// A benchmark, hidden from the default run (northlight_unit_tests "[.bench]"):
// what Lumen costs with 8, 16 and 32 notes sounding, effects off and on, in
// percent of one core at 48 kHz with 256-sample blocks.
TEST_CASE("Lumen cost per voice count", "[.bench][lumen]")
{
    constexpr double rate = 48000.0;
    constexpr int seconds = 5;
    for (const bool effects : {false, true})
        for (const int notes : {8, 16, 32})
        {
            auto synth = lumen::create();
            synth->setParameter(lumen::decay, 400.0f);
            if (effects)
            {
                synth->setParameter(lumen::phaserMix, 100.0f);
                synth->setParameter(lumen::chorusMix, 100.0f);
            }
            synth->prepare(rate, block);
            std::vector<float> l(block), r(block);
            engine::MidiEventList list(64);
            list.push(controller(64, 127));
            for (int i = 0; i < notes; ++i)
                list.push(noteOn(36 + i * 2, 100));
            synth->processMidi(list, l.data(), r.data(), block);
            const int blocks = static_cast<int>(rate * seconds / block);
            const auto start = std::chrono::steady_clock::now();
            engine::MidiEventList none(1);
            for (int b = 0; b < blocks; ++b)
                synth->processMidi(none, l.data(), r.data(), block);
            const double spent =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            std::printf("Lumen, %d notes, effects %s: %.2f%% of a core\n",
                        notes,
                        effects ? "on" : "off",
                        100.0 * spent / seconds);
        }
}

TEST_CASE("The test phrase renders an instrument the same way every time, with its cue sheet",
          "[plugins][lumen][phrase]")
{
    namespace phrase = plugins::phrase;
    const auto cues = phrase::standardPhrase();
    const int singles = (phrase::singleHighest - phrase::singleLowest) / phrase::singleStep + 1;
    CHECK(std::count_if(cues.begin(), cues.end(), [](const phrase::Cue& c) { return c.part == "single"; }) ==
          singles * 3);
    CHECK_FALSE(phrase::render("northlight.eq", cues, 48000.0).has_value());

    // A short piece of it, twice: the same samples, and as long as asked.
    const std::vector<phrase::Cue> some(cues.begin(), cues.begin() + 3);
    const auto a = phrase::render(lumen::type, some, 48000.0, 1.0);
    const auto b = phrase::render(lumen::type, some, 48000.0, 1.0);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->left == b->left);
    const double end = some.back().seconds + some.back().length + 1.0;
    CHECK(a->left.size() == static_cast<size_t>(std::llround(end * 48000.0)));
    CHECK(rms(a->left, 48000.0, some[1].seconds, some[1].seconds + 0.5) > 1.0e-3);

    const auto sheet = phrase::cueSheet(some);
    CHECK(std::count(sheet.begin(), sheet.end(), '\n') == 4); // the header and three cues
    CHECK(sheet.find("0.500000 note 28 40 1.000000 single") != std::string::npos);
}

// Calibration renders, hidden from the default run (northlight_unit_tests
// "[.calibrate]"; docs/plans/electric-piano.md, section 4): each note and velocity
// a reference listing names (file,note,velocity), struck and held for 14 s, as a
// 32-bit float WAV in NORTHLIGHT_CALIBRATE_DIR with a listing of its own, for
// scripts/analysis/ep-measure.py to measure and compare. NORTHLIGHT_CALIBRATE_SET
// ("index=value,...") overrides parameters, for trying settings without a build.
TEST_CASE("Lumen renders the reference set's notes for calibration", "[.calibrate]")
{
    const char* listing = std::getenv("NORTHLIGHT_CALIBRATE_LISTING");
    const char* dir = std::getenv("NORTHLIGHT_CALIBRATE_DIR");
    REQUIRE(listing != nullptr);
    REQUIRE(dir != nullptr);
    auto settings = plain();
    if (const char* set = std::getenv("NORTHLIGHT_CALIBRATE_SET"))
    {
        std::stringstream items(set);
        std::string item;
        while (std::getline(items, item, ','))
            if (const auto eq = item.find('='); eq != std::string::npos)
                settings[std::stoi(item.substr(0, eq))] = std::stof(item.substr(eq + 1));
    }
    constexpr double rate = 48000.0;
    const auto writeWav = [](const std::string& path, const std::vector<float>& x)
    {
        std::ofstream f(path, std::ios::binary);
        const auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        const auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        const auto bytes = static_cast<std::uint32_t>(x.size() * 4);
        f.write("RIFF", 4);
        u32(36 + bytes);
        f.write("WAVEfmt ", 8);
        u32(16);
        u16(3); // IEEE float
        u16(1);
        u32(static_cast<std::uint32_t>(rate));
        u32(static_cast<std::uint32_t>(rate) * 4);
        u16(4);
        u16(32);
        f.write("data", 4);
        u32(bytes);
        f.write(reinterpret_cast<const char*>(x.data()), static_cast<std::streamsize>(bytes));
    };
    std::ifstream in(listing);
    std::ofstream out(std::string(dir) + "/notes.csv");
    out << "file,note,velocity\n";
    std::string line;
    int rendered = 0;
    while (std::getline(in, line))
    {
        std::stringstream fields(line);
        std::string file, note, velocity;
        if (!std::getline(fields, file, ',') || !std::getline(fields, note, ',') ||
            !std::getline(fields, velocity, ',') || file == "file")
            continue;
        const int n = std::stoi(note), v = std::stoi(velocity);
        const auto sound = render(rate, 14.0, {{0.05, noteOn(n, v)}}, settings);
        const auto name = "lumen_" + std::to_string(n) + "_" + std::to_string(v) + ".wav";
        writeWav(std::string(dir) + "/" + name, sound.left);
        out << name << "," << n << "," << v << "\n";
        ++rendered;
    }
    CHECK(rendered > 0);
}
