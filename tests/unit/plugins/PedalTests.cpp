#include "plugins/BuiltIns.h"
#include "plugins/Pedals.h"
#include "plugins/ProcessorBase.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

std::unique_ptr<engine::InsertProcessor> make(const std::string& type)
{
    auto p = plugins::BuiltInFactory().create(type);
    REQUIRE(p != nullptr);
    p->prepare(rate, block);
    p->setBypassed(false);
    return p;
}

struct Stereo
{
    std::vector<float> left, right;
};

// A signal through a processor in blocks.
Stereo run(engine::InsertProcessor& p, const std::function<float(int)>& signal, int samples)
{
    Stereo s{std::vector<float>(static_cast<size_t>(samples)),
             std::vector<float>(static_cast<size_t>(samples))};
    for (int i = 0; i < samples; ++i)
        s.left[static_cast<size_t>(i)] = s.right[static_cast<size_t>(i)] = signal(i);
    for (int done = 0; done < samples; done += block)
        p.process(s.left.data() + done, s.right.data() + done, std::min(block, samples - done));
    return s;
}

std::function<float(int)> sine(double hz, float amplitude)
{
    return [=](int i)
    { return amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / rate)); };
}

// A plucked string, roughly: a decaying sawtooth (every harmonic).
std::function<float(int)> pluck(double hz, float amplitude)
{
    return [=](int i)
    {
        const double t = i / rate;
        const double phase = std::fmod(hz * t, 1.0);
        return amplitude * static_cast<float>((2.0 * phase - 1.0) * std::exp(-t * 1.5));
    };
}

float rmsDb(const std::vector<float>& v, int from, int to)
{
    double sum = 0.0;
    for (int i = from; i < to; ++i)
        sum += static_cast<double>(v[static_cast<size_t>(i)]) * v[static_cast<size_t>(i)];
    const double rms = std::sqrt(sum / std::max(1, to - from));
    return static_cast<float>(20.0 * std::log10(std::max(rms, 1.0e-12)));
}

float peakOf(const std::vector<float>& v, int from, int to)
{
    float peak = 0.0f;
    for (int i = from; i < to; ++i)
        peak = std::max(peak, std::abs(v[static_cast<size_t>(i)]));
    return peak;
}

// The amplitude of one frequency in a stretch of signal (Goertzel).
double amplitudeAt(const std::vector<float>& v, int from, int to, double hz)
{
    const double w = 2.0 * std::numbers::pi * hz / rate;
    const double k = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double s0 = v[static_cast<size_t>(i)] + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - k * s1 * s2;
    return 2.0 * std::sqrt(std::max(0.0, power)) / (to - from);
}

// The harmonics' share of a tone: the 2nd to 9th over the fundamental.
double distortionOf(const std::vector<float>& v, int from, int to, double hz)
{
    double harmonics = 0.0;
    for (int h = 2; h <= 9; ++h)
        harmonics += std::pow(amplitudeAt(v, from, to, hz * h), 2.0);
    return std::sqrt(harmonics) / std::max(1.0e-12, amplitudeAt(v, from, to, hz));
}

bool finite(const Stereo& s)
{
    for (size_t i = 0; i < s.left.size(); ++i)
        if (!std::isfinite(s.left[i]) || !std::isfinite(s.right[i]))
            return false;
    return true;
}
} // namespace

// ----- Gravity ---------------------------------------------------------------------------------

TEST_CASE("Gravity brings a quiet note up toward a loud one, more with more sustain", "[plugins][gravity]")
{
    namespace g = plugins::gravity;
    // The static curve: a firmly played note comes out where it went in.
    CHECK(g::staticGainDb(5.0f, -14.0f) == Approx(0.0f).margin(0.01f));
    // A note 26 dB quieter comes out far less than 26 dB quieter.
    const auto spread = [](float sustain)
    { return (-14.0f + g::staticGainDb(sustain, -14.0f)) - (-40.0f + g::staticGainDb(sustain, -40.0f)); };
    CHECK(spread(8.0f) < 10.0f);
    CHECK(spread(2.0f) > spread(8.0f) + 6.0f);

    // Through the processor: a loud and a quiet held tone, their outputs' spread.
    const auto levels = [](float sustain)
    {
        float out[2];
        int k = 0;
        for (const float amplitude : {0.25f, 0.0125f})
        {
            auto p = make(g::type);
            p->setParameter(g::sustain, sustain);
            const auto s = run(*p, sine(440.0, amplitude), static_cast<int>(rate * 2));
            out[k++] = rmsDb(s.left, static_cast<int>(rate * 1.5), static_cast<int>(rate * 2));
        }
        return out[0] - out[1];
    };
    CHECK(levels(9.0f) < 12.0f); // 26 dB in
    CHECK(levels(1.0f) > levels(9.0f) + 6.0f);
}

TEST_CASE("Gravity's attack lets the pick through when slow and squashes it when fast", "[plugins][gravity]")
{
    namespace g = plugins::gravity;
    const auto pickToBody = [](float attack)
    {
        auto p = make(g::type);
        p->setParameter(g::sustain, 8.0f);
        p->setParameter(g::attack, attack);
        // Silence, then a note that starts at full level.
        const auto s =
            run(*p, [](int i) { return i < 4800 ? 0.0f : sine(330.0, 0.4f)(i); }, static_cast<int>(rate));
        // The note's first 20 ms against its body.
        return rmsDb(s.left, 4800, 4800 + 960) - rmsDb(s.left, 40000, 47000);
    };
    CHECK(g::attackMs(0.0f) < g::attackMs(10.0f));
    CHECK(pickToBody(10.0f) > pickToBody(0.0f) + 4.0f);
}

TEST_CASE("Gravity's blend at zero passes the dry signal at the set level", "[plugins][gravity]")
{
    namespace g = plugins::gravity;
    auto p = make(g::type);
    p->setParameter(g::blend, 0.0f);
    p->setParameter(g::level, -6.0f);
    const auto in = sine(220.0, 0.3f);
    const auto s = run(*p, in, 9600);
    for (int i = 4800; i < 9600; i += 37)
        CHECK(s.left[static_cast<size_t>(i)] == Approx(in(i)*plugins::dbToGain(-6.0f)).margin(1.0e-4));
}

TEST_CASE("Gravity does not lift the hiss between notes by its full make-up", "[plugins][gravity]")
{
    namespace g = plugins::gravity;
    auto p = make(g::type);
    p->setParameter(g::sustain, 10.0f);
    const auto makeup = g::curveFor(10.0f).makeupDb;
    REQUIRE(makeup > 20.0f);
    const auto s = run(*p, sine(1000.0, 0.0001f), static_cast<int>(rate)); // -80 dBFS
    const float gain =
        rmsDb(s.left, 24000, 48000) -
        rmsDb(run(*make(plugins::types::gain), sine(1000.0, 0.0001f), static_cast<int>(rate)).left,
              24000,
              48000);
    CHECK(gain < makeup - 10.0f);
}

// ----- Flare -----------------------------------------------------------------------------------

TEST_CASE("Flare distorts more as Drive goes up and stays bounded", "[plugins][flare]")
{
    namespace f = plugins::flare;
    const auto distortion = [](float drive, int clipper)
    {
        auto p = make(f::type);
        p->setParameter(f::drive, drive);
        p->setParameter(f::clip, static_cast<float>(clipper));
        p->setParameter(f::tone, 10.0f);
        const auto s = run(*p, sine(220.0, 0.05f), static_cast<int>(rate));
        CHECK(finite(s));
        return distortionOf(s.left, 24000, 48000, 220.0);
    };
    CHECK(distortion(1.0f, f::silicon) < 0.1);
    CHECK(distortion(8.0f, f::silicon) > distortion(1.0f, f::silicon) * 3.0);
    // Flat out on its rails, as loud as it gets: still well within full scale.
    auto p = make(f::type);
    p->setParameter(f::drive, 10.0f);
    p->setParameter(f::level, 10.0f);
    p->setParameter(f::clip, f::open);
    const auto s = run(*p, sine(110.0, 0.8f), static_cast<int>(rate));
    CHECK(finite(s));
    CHECK(peakOf(s.left, 0, static_cast<int>(rate)) < 2.0f);
}

TEST_CASE("Flare's clippers: silicon tightest, LED more headroom, open on the rails", "[plugins][flare]")
{
    namespace f = plugins::flare;
    CHECK(std::abs(f::clipCurve(f::silicon, 10.0f)) < 0.75f);
    CHECK(f::clipCurve(f::led, 10.0f) > 1.4f);
    CHECK(f::clipCurve(f::led, 10.0f) < 2.0f);
    CHECK(f::clipCurve(f::open, 10.0f) > 3.8f);
    CHECK(f::clipCurve(f::open, 10.0f) < 4.3f);
    // Small signals pass nearly straight through every clipper, and the curves are odd.
    for (int c = 0; c < 3; ++c)
    {
        CHECK(f::clipCurve(c, 0.05f) == Approx(0.05f).margin(0.003f));
        CHECK(f::clipCurve(c, -1.0f) == Approx(-f::clipCurve(c, 1.0f)));
    }
}

TEST_CASE("Flare keeps the lows tight and its Tone brightens it", "[plugins][flare]")
{
    namespace f = plugins::flare;
    // Below clipping, the stage's gain is lower in the bass than in the mids.
    const auto gainAt = [](double hz)
    {
        auto p = make(f::type);
        p->setParameter(f::drive, 5.0f);
        p->setParameter(f::tone, 10.0f);
        const auto s = run(*p, sine(hz, 0.0005f), static_cast<int>(rate));
        return rmsDb(s.left, 24000, 48000);
    };
    CHECK(gainAt(70.0) < gainAt(1000.0) - 6.0f);
    CHECK(f::toneHz(0.0f) < 600.0f);
    CHECK(f::toneHz(10.0f) > 10000.0f);
    const auto brightness = [](float tone)
    {
        auto p = make(f::type);
        p->setParameter(f::drive, 6.0f);
        p->setParameter(f::tone, tone);
        const auto s = run(*p, sine(196.0, 0.1f), static_cast<int>(rate));
        return amplitudeAt(s.left, 24000, 48000, 196.0 * 15) / amplitudeAt(s.left, 24000, 48000, 196.0);
    };
    CHECK(brightness(9.0f) > brightness(1.0f) * 3.0);
}

// ----- Comet -----------------------------------------------------------------------------------

TEST_CASE("Comet repeats at its time, and its echoes fade with Repeats down", "[plugins][comet]")
{
    namespace c = plugins::comet;
    auto p = make(c::type);
    p->setParameter(c::time, 250.0f);
    p->setParameter(c::repeats, 5.0f);
    p->setParameter(c::mix, 10.0f);
    const int n = static_cast<int>(rate * 1.2);
    // A short burst of 1 kHz (inside the repeats' bandwidth).
    const auto s = run(*p, [](int i) { return i < 480 ? sine(1000.0, 0.5f)(i) : 0.0f; }, n);
    const int at = static_cast<int>(rate * 0.25);
    const float first = peakOf(s.left, at - 200, at + 900);
    const float second = peakOf(s.left, 2 * at - 200, 2 * at + 900);
    CHECK(first > 0.1f);
    CHECK(peakOf(s.left, 2000, at - 400) < 0.01f); // nothing before the first repeat
    CHECK(second < first);
    CHECK(second > first * 0.2f);
    // A longer delay is clocked slower: darker repeats.
    CHECK(c::bandwidthHz(800.0f) < c::bandwidthHz(300.0f));
    CHECK(c::bandwidthHz(300.0f) < c::bandwidthHz(60.0f));
}

TEST_CASE("Comet runs away at the top of Repeats but stays in bounds", "[plugins][comet]")
{
    namespace c = plugins::comet;
    CHECK(c::feedbackGain(10.0f) > 1.0f);
    CHECK(c::feedbackGain(5.0f) < 0.7f);
    auto p = make(c::type);
    p->setParameter(c::time, 200.0f);
    p->setParameter(c::repeats, 10.0f);
    p->setParameter(c::mix, 10.0f);
    const int n = static_cast<int>(rate * 6);
    const auto s = run(*p, [](int i) { return i < 960 ? sine(800.0, 0.5f)(i) : 0.0f; }, n);
    CHECK(finite(s));
    const float late = peakOf(s.left, n - 24000, n);
    CHECK(late > 0.05f); // still going, five seconds later
    CHECK(late < 1.0f);  // held by the compander
}

TEST_CASE("Comet passes the dry signal untouched", "[plugins][comet]")
{
    namespace c = plugins::comet;
    auto p = make(c::type);
    p->setParameter(c::mix, 0.0f);
    p->setParameter(c::mod, 1.0f);
    const auto in = sine(330.0, 0.4f);
    const auto s = run(*p, in, 24000);
    for (int i = 0; i < 24000; i += 41)
        CHECK(s.left[static_cast<size_t>(i)] == Approx(in(i)).margin(1.0e-6));
}

// ----- Tide ------------------------------------------------------------------------------------

namespace
{
// How much a tone's level wobbles through the chorus: the spread of its RMS
// over 20 ms slices (dB).
float wobbleDb(const Stereo& s, int from, int to)
{
    float lo = 1.0e9f, hi = -1.0e9f;
    for (int i = from; i + 960 <= to; i += 960)
    {
        const float db = rmsDb(s.left, i, i + 960);
        lo = std::min(lo, db);
        hi = std::max(hi, db);
    }
    return hi - lo;
}
} // namespace

TEST_CASE("Tide shimmers the top and leaves the bass's lows steady", "[plugins][tide]")
{
    namespace t = plugins::tide;
    const auto through = [](double hz)
    {
        auto p = make(t::type);
        p->setParameter(t::rate, 5.0f);
        p->setParameter(t::depth, 10.0f);
        p->setParameter(t::mix, 10.0f);
        return run(*p, sine(hz, 0.3f), static_cast<int>(rate * 2));
    };
    const auto low = through(50.0);
    const auto high = through(1500.0);
    CHECK(wobbleDb(low, 48000, 96000) < 1.5f);
    CHECK(wobbleDb(high, 48000, 96000) > 4.0f);
    // The bass passes at its level.
    CHECK(rmsDb(low.left, 48000, 96000) ==
          Approx(rmsDb(run(*make(plugins::types::gain), sine(50.0, 0.3f), static_cast<int>(rate * 2)).left,
                       48000,
                       96000))
              .margin(1.5f));
}

TEST_CASE("Tide sweeps its two sides in opposite phase, and sums to mono cleanly", "[plugins][tide]")
{
    namespace t = plugins::tide;
    auto p = make(t::type);
    p->setParameter(t::depth, 8.0f);
    p->setParameter(t::mix, 10.0f);
    const auto s = run(*p, sine(1200.0, 0.3f), static_cast<int>(rate * 2));
    double difference = 0.0, sum = 0.0, side = 0.0;
    for (int i = 48000; i < 96000; ++i)
    {
        const double l = s.left[static_cast<size_t>(i)], r = s.right[static_cast<size_t>(i)];
        difference += (l - r) * (l - r);
        sum += 0.25 * (l + r) * (l + r);
        side += l * l;
    }
    CHECK(difference > 0.05 * side); // the sides differ
    CHECK(sum > 0.5 * side);         // and the mono sum keeps its level
}

TEST_CASE("Tide's vibrato is the delayed signal alone above the bass", "[plugins][tide]")
{
    namespace t = plugins::tide;
    for (const float mode : {0.0f, 1.0f})
    {
        auto p = make(t::type);
        p->setParameter(t::depth, 0.0f);
        p->setParameter(t::mix, 10.0f);
        p->setParameter(t::mode, mode);
        // A 3 kHz burst: well above the dry lows kept under vibrato.
        const auto s = run(*p, [](int i) { return i < 240 ? sine(3000.0, 0.4f)(i) : 0.0f; }, 4800);
        // The burst is shorter than the delay, so the two never overlap.
        const int delay = static_cast<int>(t::centreMs * 0.001 * rate);
        REQUIRE(delay > 300);
        const float direct = peakOf(s.left, 0, 300);
        const float delayed = peakOf(s.left, delay, delay + 240);
        CHECK(delayed > 0.2f);
        if (mode < 0.5f)
            CHECK(direct > 0.3f);
        else
            CHECK(direct < 0.08f);
    }
}

// ----- Polaris ---------------------------------------------------------------------------------

namespace
{
float cents(float found, float expected)
{
    return 1200.0f * std::log2(found / expected);
}
} // namespace

TEST_CASE("Polaris finds a string's pitch from a bass's low B to a guitar's top", "[plugins][polaris]")
{
    namespace pol = plugins::polaris;
    constexpr double analysis = 8000.0;
    for (const double hz : {30.87, 41.2, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 659.26, 1046.5})
    {
        INFO(hz);
        std::vector<float> window(1024);
        // Every harmonic below the analysis rate's Nyquist (a band-limited sawtooth):
        // a string's richness, with no octave to slip to.
        for (size_t i = 0; i < window.size(); ++i)
        {
            double v = 0.0;
            for (int h = 1; h * hz < analysis * 0.45; ++h)
                v += std::sin(2.0 * std::numbers::pi * h * hz * static_cast<double>(i) / analysis) / h;
            window[i] = 0.2f * static_cast<float>(v);
        }
        float clarity = 0.0f;
        const float found =
            pol::detectPitch(window.data(), static_cast<int>(window.size()), analysis, &clarity);
        REQUIRE(found > 0.0f);
        CHECK(std::abs(cents(found, static_cast<float>(hz))) < 3.0f);
        CHECK(clarity > 0.8f);
    }
    std::vector<float> silence(1024, 0.0f);
    CHECK(pol::detectPitch(silence.data(), 1024, analysis) == 0.0f);
}

TEST_CASE("Polaris names the nearest note and how far off it is", "[plugins][polaris]")
{
    namespace pol = plugins::polaris;
    auto n = pol::noteFor(440.0f, 440.0f);
    CHECK(n.midi == 69);
    CHECK(n.cents == Approx(0.0f).margin(0.01f));
    CHECK(std::string(pol::noteName(n.midi)) == "A");
    CHECK(pol::octaveOf(n.midi) == 4);
    n = pol::noteFor(445.0f, 440.0f);
    CHECK(n.midi == 69);
    CHECK(n.cents == Approx(19.56f).margin(0.05f));
    n = pol::noteFor(82.41f, 440.0f);
    CHECK(std::string(pol::noteName(n.midi)) == "E");
    CHECK(pol::octaveOf(n.midi) == 2);
    n = pol::noteFor(432.0f, 432.0f);
    CHECK(n.midi == 69);
    CHECK(std::abs(n.cents) < 0.01f);
    CHECK(std::string(pol::noteName(61)) == "C#");
}

TEST_CASE("Polaris reads a played string and its footswitch mutes the output", "[plugins][polaris]")
{
    namespace pol = plugins::polaris;
    auto p = make(pol::type);
    const auto s = run(*p, pluck(110.0, 0.4f), static_cast<int>(rate * 0.6));
    pol::Reading reading;
    REQUIRE(pol::readingOf(p.get(), reading));
    CHECK(std::abs(cents(reading.hz, 110.0f)) < 3.0f);
    CHECK(reading.level > 0.05f);
    // Unmuted, the signal passes untouched.
    const auto in = pluck(110.0, 0.4f);
    CHECK(s.left[20000] == Approx(in(20000)).margin(1.0e-6));
    // Muted, it is silent once the fade is done, and the tuner still reads.
    p->setParameter(pol::mute, 1.0f);
    const auto muted = run(*p, sine(196.0, 0.4f), static_cast<int>(rate * 0.5));
    CHECK(peakOf(muted.left, 960, static_cast<int>(rate * 0.5)) == 0.0f);
    REQUIRE(pol::readingOf(p.get(), reading));
    CHECK(std::abs(cents(reading.hz, 196.0f)) < 3.0f);
    // Any other processor has no reading.
    auto other = make(plugins::types::gain);
    CHECK_FALSE(pol::readingOf(other.get(), reading));
}
