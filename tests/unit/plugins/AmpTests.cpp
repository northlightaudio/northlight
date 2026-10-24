#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/Amp.h"
#include "plugins/BuiltIns.h"
#include "plugins/Cabinet.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <thread>
#include <vector>

using namespace northlight;
using Catch::Approx;
namespace pa = plugins::amp;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

// The amp with its cabinet, reverb and tremolo out of the way, so what is
// measured is the amp's own circuit.
std::unique_ptr<engine::InsertProcessor> makeAmp(double sampleRate = rate)
{
    auto p = plugins::BuiltInFactory().create(pa::type);
    REQUIRE(p != nullptr);
    p->prepare(sampleRate, block);
    p->setBypassed(false);
    p->setParameter(pa::cabinetOn, 0.0f);
    p->setParameter(pa::reverbOn, 0.0f);
    p->setParameter(pa::tremoloOn, 0.0f);
    return p;
}

struct Stereo
{
    std::vector<float> left, right;
};

Stereo run(engine::InsertProcessor& p, Stereo in, int blockSize = block)
{
    const auto n = static_cast<int>(in.left.size());
    for (int done = 0; done < n; done += blockSize)
    {
        const int count = std::min(blockSize, n - done);
        p.process(in.left.data() + done, in.right.data() + done, count);
    }
    return in;
}

// A tone at the jack, as a centred mono track hands it to its inserts (panned at
// constant power: each side 3 dB down).
constexpr float centred = 0.70710678f;

Stereo sine(double hz, double seconds, float amplitude, double sampleRate = rate)
{
    Stereo s;
    s.left.resize(static_cast<size_t>(seconds * sampleRate));
    for (size_t i = 0; i < s.left.size(); ++i)
        s.left[i] =
            centred * amplitude *
            static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / sampleRate));
    s.right = s.left;
    return s;
}

Stereo silence(double seconds, double sampleRate = rate)
{
    Stereo s{std::vector<float>(static_cast<size_t>(seconds * sampleRate), 0.0f), {}};
    s.right = s.left;
    return s;
}

double amplitudeAt(const std::vector<float>& x, double hz, double sampleRate, size_t from, size_t to)
{
    std::complex<double> sum = 0.0;
    double weight = 0.0;
    const double n = static_cast<double>(to - from);
    for (size_t i = from; i < to; ++i)
    {
        const double k = static_cast<double>(i - from);
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * k / n);
        sum += static_cast<double>(x[i]) * w * std::polar(1.0, -2.0 * std::numbers::pi * hz * k / sampleRate);
        weight += w;
    }
    return 2.0 * std::abs(sum) / weight;
}

double db(double x)
{
    return 20.0 * std::log10(std::max(1.0e-12, x));
}

float peak(const std::vector<float>& x, size_t from = 0)
{
    float p = 0.0f;
    for (size_t i = from; i < x.size(); ++i)
        p = std::max(p, std::abs(x[i]));
    return p;
}

double rms(const std::vector<float>& x, size_t from = 0, size_t to = 0)
{
    if (to == 0)
        to = x.size();
    double s = 0.0;
    for (size_t i = from; i < to; ++i)
        s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(std::max<size_t>(1, to - from)));
}

// Total harmonic distortion (harmonics 2..10 over the fundamental) of a 220 Hz
// tone through the amp, and the fundamental's level.
struct Distortion
{
    double thd = 0.0, levelDb = 0.0;
};

Distortion distortion(engine::InsertProcessor& p, float amplitude = 0.25f, double hz = 220.0)
{
    const auto out = run(p, sine(hz, 1.0, amplitude));
    const size_t from = out.left.size() / 2, to = out.left.size();
    const double fundamental = amplitudeAt(out.left, hz, rate, from, to);
    double harmonics = 0.0;
    for (int h = 2; h <= 10; ++h)
        harmonics += std::pow(amplitudeAt(out.left, hz * h, rate, from, to), 2.0);
    return {std::sqrt(harmonics) / std::max(1.0e-12, fundamental), db(fundamental)};
}

// The loudest alias (dB under the fundamental) of a hard-driven tone: the
// harmonics above Nyquist fold back between the real ones.
double worstAliasDb(engine::InsertProcessor& p, double hz, double sampleRate = rate)
{
    const auto out = run(p, sine(hz, 1.0, 0.5f, sampleRate));
    const size_t from = out.left.size() / 2, to = out.left.size();
    const double fundamental = amplitudeAt(out.left, hz, sampleRate, from, to);
    const double nyquist = sampleRate / 2.0;
    double worst = 0.0;
    for (int h = 2; h <= 40; ++h)
    {
        double f = std::fmod(hz * h, sampleRate);
        if (f > nyquist)
            f = sampleRate - f;
        if (hz * h < nyquist || f < 20.0)
            continue; // a real harmonic, or folded onto DC
        bool onHarmonic = false;
        for (int k = 1; hz * k < nyquist; ++k)
            onHarmonic = onHarmonic || std::abs(f - hz * k) < 40.0;
        if (!onHarmonic)
            worst = std::max(worst, amplitudeAt(out.left, f, sampleRate, from, to));
    }
    return db(worst / fundamental);
}
} // namespace

TEST_CASE("Probe: the amp's distortion and level across its Volume", "[.probe][ampprobe]")
{
    for (float boost : {0.0f, 1.0f})
        for (float volume : {2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 10.0f})
        {
            auto p = makeAmp();
            p->setParameter(pa::volume, volume);
            p->setParameter(pa::boost, boost);
            const auto d = distortion(*p);
            std::printf("boost %d volume %4.1f: THD %6.2f%%  level %6.1f dB\n",
                        static_cast<int>(boost),
                        volume,
                        d.thd * 100.0,
                        d.levelDb);
        }
    for (float master : {10.0f, 5.0f, 3.0f})
    {
        auto p = makeAmp();
        p->setParameter(pa::volume, 10.0f);
        p->setParameter(pa::master, master);
        const auto d = distortion(*p);
        std::printf(
            "volume 10 master %4.1f: THD %6.2f%%  level %6.1f dB\n", master, d.thd * 100.0, d.levelDb);
    }
    {
        auto p = makeAmp();
        p->setParameter(pa::cabinetOn, 1.0f);
        const auto out = run(*p, sine(220.0, 1.0, 0.25f));
        std::printf("defaults with the cabinet: peak %.1f dB rms %.1f dB\n",
                    db(peak(out.left, 24000)),
                    db(rms(out.left, 24000)));
    }
    for (int q = 0; q < 3; ++q)
    {
        auto p = makeAmp();
        p->setParameter(pa::volume, 10.0f);
        p->setParameter(pa::boost, 1.0f);
        p->setParameter(pa::quality, static_cast<float>(q));
        std::printf(
            "quality %dx: worst alias of 3.1 kHz %.1f dB\n", pa::qualityFactors[q], worstAliasDb(*p, 3100.0));
    }
    for (int q = 0; q < 3; ++q)
    {
        auto p = makeAmp();
        p->setParameter(pa::cabinetOn, 1.0f);
        p->setParameter(pa::reverbOn, 1.0f);
        p->setParameter(pa::tremoloOn, 1.0f);
        p->setParameter(pa::quality, static_cast<float>(q));
        auto in = sine(220.0, 5.0, 0.25f);
        const auto start = std::chrono::steady_clock::now();
        run(*p, in, 64);
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf(
            "quality %dx: %.1f%% of a core at 48 kHz\n", pa::qualityFactors[q], 100.0 * seconds / 5.0);
    }
}

TEST_CASE("The amp reports no latency and answers a click within a millisecond", "[plugins][amp]")
{
    auto p = makeAmp();
    CHECK(p->latencySamples() == 0);
    p->setParameter(pa::cabinetOn, 1.0f);
    auto in = silence(0.1);
    in.left[0] = in.right[0] = 0.05f;
    const auto out = run(*p, in);
    size_t loudest = 0;
    for (size_t i = 0; i < out.left.size(); ++i)
        if (std::abs(out.left[i]) > std::abs(out.left[loudest]))
            loudest = i;
    CHECK(loudest < static_cast<size_t>(0.001 * rate));
    CHECK(std::abs(out.left[2]) > 0.0f); // the response has begun by the third sample
}

TEST_CASE("Up to half way the amp stays clean, and past 6 it breaks up more with every step",
          "[plugins][amp]")
{
    // A strummed chord at the jack peaks around a quarter of a volt (-12 dBFS here).
    std::vector<double> thd;
    for (float volume : {3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 10.0f})
    {
        auto p = makeAmp();
        p->setParameter(pa::volume, volume);
        thd.push_back(distortion(*p).thd);
    }
    CHECK(thd[0] < 0.02);
    CHECK(thd[2] < 0.03);
    CHECK(thd[4] > 0.05);
    CHECK(thd[6] > 0.2);
    for (size_t i = 1; i < thd.size(); ++i)
        CHECK(thd[i] >= thd[i - 1] * 0.98);
}

TEST_CASE("The boost drives the preamp harder, and a low master keeps the drive but not the loudness",
          "[plugins][amp]")
{
    auto plain = makeAmp(), boosted = makeAmp();
    plain->setParameter(pa::volume, 5.0f);
    boosted->setParameter(pa::volume, 5.0f);
    boosted->setParameter(pa::boost, 1.0f);
    CHECK(distortion(*boosted).thd > 3.0 * distortion(*plain).thd);

    auto full = makeAmp(), quiet = makeAmp();
    full->setParameter(pa::volume, 10.0f);
    quiet->setParameter(pa::volume, 10.0f);
    quiet->setParameter(pa::master, 3.0f);
    const auto loud = distortion(*full), low = distortion(*quiet);
    CHECK(low.levelDb < loud.levelDb - 15.0);
    CHECK(low.thd > 0.1);
}

TEST_CASE("The bright switch lifts the treble at low volume and not at full", "[plugins][amp]")
{
    const auto trebleLift = [](float volume, float amplitude)
    {
        double level[2];
        for (int on = 0; on < 2; ++on)
        {
            auto p = makeAmp();
            p->setParameter(pa::volume, volume);
            p->setParameter(pa::bright, static_cast<float>(on));
            const auto out = run(*p, sine(4000.0, 0.5, amplitude));
            level[on] = db(amplitudeAt(out.left, 4000.0, rate, out.left.size() / 2, out.left.size()));
        }
        return level[1] - level[0];
    };
    CHECK(trebleLift(3.0f, 0.05f) > 4.0);
    CHECK(std::abs(trebleLift(10.0f, 0.002f)) < 1.0);
}

TEST_CASE("Treble, middle and bass move their own bands", "[plugins][amp]")
{
    const auto levelAt = [](int param, float value, double hz)
    {
        auto p = makeAmp();
        p->setParameter(pa::volume, 3.0f);
        p->setParameter(param, value);
        const auto out = run(*p, sine(hz, 0.5, 0.02f));
        return db(amplitudeAt(out.left, hz, rate, out.left.size() / 2, out.left.size()));
    };
    CHECK(levelAt(pa::treble, 10.0f, 3000.0) > levelAt(pa::treble, 1.0f, 3000.0) + 6.0);
    CHECK(levelAt(pa::bass, 10.0f, 80.0) > levelAt(pa::bass, 1.0f, 80.0) + 6.0);
    CHECK(levelAt(pa::middle, 10.0f, 600.0) > levelAt(pa::middle, 1.0f, 600.0) + 3.0);
}

TEST_CASE("The amp sounds the same at 44.1, 48 and 96 kHz", "[plugins][amp]")
{
    std::vector<Distortion> results;
    for (double r : {44100.0, 48000.0, 96000.0})
    {
        auto p = makeAmp(r);
        p->setParameter(pa::volume, 7.0f);
        const auto out = run(*p, sine(220.0, 1.0, 0.25f, r));
        const size_t from = out.left.size() / 2, to = out.left.size();
        const double fundamental = amplitudeAt(out.left, 220.0, r, from, to);
        double harmonics = 0.0;
        for (int h = 2; h <= 10; ++h)
            harmonics += std::pow(amplitudeAt(out.left, 220.0 * h, r, from, to), 2.0);
        results.push_back({std::sqrt(harmonics) / fundamental, db(fundamental)});
    }
    for (const auto& d : results)
    {
        CHECK(d.thd == Approx(results[1].thd).epsilon(0.2));
        CHECK(d.levelDb == Approx(results[1].levelDb).margin(0.5));
    }
}

TEST_CASE("The tremolo pulses at its speed and depth, and its footswitch stops it", "[plugins][amp]")
{
    // The output's level in 5 ms windows, and how often it peaks a second.
    const auto pulse = [](float speedKnob, bool on)
    {
        auto p = makeAmp();
        p->setParameter(pa::volume, 3.0f);
        p->setParameter(pa::tremoloOn, on ? 1.0f : 0.0f);
        p->setParameter(pa::intensity, 10.0f);
        p->setParameter(pa::speed, speedKnob);
        // 400 Hz: a 5 ms window holds two whole cycles, so a steady tone reads steady.
        const auto out = run(*p, sine(400.0, 3.0, 0.1f));
        const auto window = static_cast<size_t>(0.005 * rate);
        std::vector<double> envelope;
        for (auto i = static_cast<size_t>(rate); i + window <= out.left.size(); i += window)
            envelope.push_back(rms(out.left, i, i + window));
        const auto [lo, hi] = std::minmax_element(envelope.begin(), envelope.end());
        // Rising crossings of the mean count the cycles (over two seconds).
        double mean = 0.0;
        for (double e : envelope)
            mean += e / static_cast<double>(envelope.size());
        int cycles = 0;
        for (size_t i = 1; i < envelope.size(); ++i)
            cycles += envelope[i - 1] < mean && envelope[i] >= mean;
        return std::make_pair(db(*hi / std::max(1.0e-9, *lo)), cycles / 2.0);
    };
    const auto slow = pulse(1.0f, true);
    const auto fast = pulse(10.0f, true);
    CHECK(slow.first > 10.0);
    CHECK(slow.second == Approx(1.5).margin(0.6));
    CHECK(fast.second == Approx(8.0).margin(1.0));
    CHECK(fast.first > 6.0);
    CHECK(pulse(5.0f, false).first < 0.5);
}

TEST_CASE("The spring reverb rings on after the note, and its footswitch silences it", "[plugins][amp]")
{
    const auto tail = [](bool on)
    {
        auto p = makeAmp();
        p->setParameter(pa::volume, 4.0f);
        p->setParameter(pa::reverbOn, on ? 1.0f : 0.0f);
        p->setParameter(pa::reverb, 7.0f);
        auto in = sine(330.0, 1.3, 0.2f);
        std::fill(in.left.begin() + static_cast<std::ptrdiff_t>(0.3 * rate), in.left.end(), 0.0f);
        in.right = in.left;
        const auto out = run(*p, in);
        return db(rms(out.left, static_cast<size_t>(0.6 * rate), static_cast<size_t>(1.3 * rate)));
    };
    CHECK(tail(true) > -50.0);
    CHECK(tail(false) < -90.0);
}

TEST_CASE("The noise gate shuts out quiet hiss and opens for playing", "[plugins][amp]")
{
    // Hiss at about -70 dBFS.
    Stereo hiss = silence(0.5);
    std::uint32_t seed = 1u;
    for (auto& v : hiss.left)
    {
        seed = seed * 1664525u + 1013904223u;
        v = 3.0e-4f * (static_cast<float>(seed) * (2.0f / 4294967296.0f) - 1.0f);
    }
    hiss.right = hiss.left;

    auto p = makeAmp();
    p->setParameter(pa::gate, -50.0f);
    p->setParameter(pa::volume, 7.0f);
    const auto shut = run(*p, hiss);
    CHECK(db(rms(shut.left, static_cast<size_t>(0.3 * rate))) < -80.0);
    CHECK(p->feedback()->latest().reduction > 20.0f);
    // Playing opens it.
    const auto open = run(*p, sine(220.0, 0.5, 0.1f));
    CHECK(db(rms(open.left, static_cast<size_t>(0.2 * rate))) > -30.0);
    // Off, the hiss goes through.
    auto q = makeAmp();
    q->setParameter(pa::volume, 7.0f);
    CHECK(db(rms(run(*q, hiss).left, static_cast<size_t>(0.3 * rate))) > -80.0);
}

TEST_CASE("Silence after a loud passage settles to silence", "[plugins][amp]")
{
    auto p = makeAmp();
    p->setParameter(pa::volume, 10.0f);
    p->setParameter(pa::boost, 1.0f);
    p->setParameter(pa::cabinetOn, 1.0f);
    run(*p, sine(110.0, 0.5, 0.8f));
    const auto out = run(*p, silence(1.0));
    CHECK(peak(out.left, static_cast<size_t>(0.5 * rate)) < 1.0e-5f);
}

TEST_CASE("Bypassed, the amp passes its input untouched", "[plugins][amp]")
{
    auto p = makeAmp();
    p->setBypassed(true);
    run(*p, silence(0.05)); // the bypass crossfade
    const auto in = sine(220.0, 0.1, 0.3f);
    const auto out = run(*p, in);
    CHECK(out.left == in.left);
}

TEST_CASE("Driven hard, the amp keeps its aliases far under the note", "[plugins][amp]")
{
    // A high note through the boosted amp at full volume: the harmonics above
    // Nyquist fold back between the real ones unless the oversampling stops them.
    for (int q = 0; q < 3; ++q)
    {
        auto p = makeAmp();
        p->setParameter(pa::volume, 10.0f);
        p->setParameter(pa::boost, 1.0f);
        p->setParameter(pa::quality, static_cast<float>(q));
        INFO("quality " << pa::qualityFactors[q] << "x");
        CHECK(worstAliasDb(*p, 3100.0) < (q == 0 ? -50.0 : -55.0));
    }
}

// ----- the cabinet ------------------------------------------------------------------------

namespace
{
double responseDb(const std::vector<float>& h, double hz)
{
    std::complex<double> sum = 0.0;
    for (size_t i = 0; i < h.size(); ++i)
        sum += static_cast<double>(h[i]) *
               std::polar(1.0, -2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate);
    return db(std::abs(sum));
}

plugins::cabinet::Settings oneMic(const plugins::cabinet::Mic& mic, int model = plugins::cabinet::open2x12)
{
    plugins::cabinet::Settings s;
    s.model = model;
    s.mics[0] = mic;
    s.mics[1].on = false;
    s.room = 0.0f;
    return s;
}
} // namespace

TEST_CASE("A mic moved from the cap to the cone's edge sounds darker", "[plugins][amp][cabinet]")
{
    plugins::cabinet::Mic cap, edge;
    cap.position = 0.05f;
    edge.position = 0.95f;
    const auto brightness = [](const plugins::cabinet::Mic& m)
    { return plugins::cabinet::directDb(plugins::cabinet::open2x12, m, 6000.0); };
    CHECK(brightness(cap) > brightness(edge) + 6.0);
}

TEST_CASE("Up close a directional mic's lows swell, and an angled mic is duller", "[plugins][amp][cabinet]")
{
    plugins::cabinet::Mic near, far;
    near.type = far.type = plugins::cabinet::ribbon;
    near.distanceCm = 2.0f;
    far.distanceCm = 60.0f;
    const auto lowRise = [](const plugins::cabinet::Mic& m)
    {
        return plugins::cabinet::directDb(plugins::cabinet::open2x12, m, 120.0) -
               plugins::cabinet::directDb(plugins::cabinet::open2x12, m, 1000.0);
    };
    CHECK(lowRise(near) > lowRise(far) + 6.0);

    plugins::cabinet::Mic straight, angled;
    angled.angleDeg = 45.0f;
    CHECK(plugins::cabinet::directDb(plugins::cabinet::open2x12, angled, 5000.0) <
          plugins::cabinet::directDb(plugins::cabinet::open2x12, straight, 5000.0) - 5.0);
}

TEST_CASE("Each mic type has its own voice", "[plugins][amp][cabinet]")
{
    const auto presence = [](int type)
    {
        plugins::cabinet::Mic m;
        m.type = type;
        m.distanceCm = 30.0f;
        return plugins::cabinet::directDb(plugins::cabinet::open2x12, m, 6000.0);
    };
    // The dynamic's presence peak over the ribbon's dark top.
    CHECK(presence(plugins::cabinet::dynamic) > presence(plugins::cabinet::ribbon) + 4.0);
}

TEST_CASE("Two equal mics at different distances comb when blended", "[plugins][amp][cabinet]")
{
    // 17.15 cm further back is 0.5 ms later: the sum dips at 1 kHz and adds at
    // 2 kHz (not fully: the far mic also hears the floor and the other speaker).
    plugins::cabinet::Settings s;
    s.room = 0.0f;
    s.mics[0].distanceCm = 3.0f;
    s.mics[1] = s.mics[0];
    s.mics[1].distanceCm = 3.0f + 17.15f;
    const auto both = plugins::cabinet::build(s, rate);
    s.mics[1].on = false;
    const auto one = plugins::cabinet::build(s, rate);
    const double notch = responseDb(both.left, 1000.0) - responseDb(one.left, 1000.0);
    const double crest = responseDb(both.left, 2000.0) - responseDb(one.left, 2000.0);
    CHECK(notch < -3.0);
    CHECK(crest > 2.0);
    CHECK(crest - notch > 6.0);
}

TEST_CASE("A mic panned hard left reaches only the left", "[plugins][amp][cabinet]")
{
    plugins::cabinet::Mic m;
    m.pan = -1.0f;
    const auto r = plugins::cabinet::build(oneMic(m), rate);
    CHECK(rms(r.left) > 0.01);
    CHECK(rms(r.right) < 1.0e-9);
}

TEST_CASE("The built cabinet is minimum phase and levelled", "[plugins][amp][cabinet]")
{
    plugins::cabinet::Mic m;
    m.distanceCm = 1.0f;
    const auto r = plugins::cabinet::build(oneMic(m), rate);
    REQUIRE(static_cast<int>(r.left.size()) == plugins::cabinet::responseLength(rate));
    // Most of the energy in the first 2 ms.
    const auto early = static_cast<size_t>(0.002 * rate);
    double head = 0.0, total = 0.0;
    for (size_t i = 0; i < r.left.size(); ++i)
    {
        const double e = static_cast<double>(r.left[i]) * r.left[i];
        total += e;
        if (i < early)
            head += e;
    }
    CHECK(head > 0.8 * total);
    // The band 150 Hz - 4 kHz sits about 0 dB.
    CHECK(responseDb(r.left, 1000.0) == Approx(0.0).margin(8.0));
}

TEST_CASE("Changing the cabinet while playing swaps the response without a click", "[plugins][amp][cabinet]")
{
    auto p = makeAmp();
    p->setParameter(pa::cabinetOn, 1.0f);
    pa::settleCabinet(*p);
    const auto tone = [](size_t offset, double seconds)
    {
        Stereo s;
        s.left.resize(static_cast<size_t>(seconds * rate));
        for (size_t i = 0; i < s.left.size(); ++i)
            s.left[i] = 0.1f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 *
                                                           static_cast<double>(i + offset) / rate));
        s.right = s.left;
        return s;
    };
    const auto steady = run(*p, tone(0, 0.5));
    float jump = 0.0f;
    for (size_t i = steady.left.size() / 2; i < steady.left.size(); ++i)
        jump = std::max(jump, std::abs(steady.left[i] - steady.left[i - 1]));
    p->setParameter(pa::cabinet, static_cast<float>(plugins::cabinet::closed4x10));
    p->setParameter(pa::micParam(0, pa::micPosition), 90.0f);
    pa::settleCabinet(*p);
    const auto after = run(*p, tone(steady.left.size(), 0.5));
    float worst = std::abs(after.left[0] - steady.left.back());
    for (size_t i = 1; i < after.left.size(); ++i)
        worst = std::max(worst, std::abs(after.left[i] - after.left[i - 1]));
    CHECK(worst < 2.0f * jump);
}

TEST_CASE("The background thread rebuilds the cabinet after a change by itself", "[plugins][amp][cabinet]")
{
    auto p = makeAmp();
    p->setParameter(pa::cabinetOn, 1.0f);
    pa::settleCabinet(*p);
    auto click = silence(0.05);
    click.left[0] = click.right[0] = 0.05f;
    const auto before = run(*p, click);
    p->setParameter(pa::micParam(0, pa::micPan), -100.0f);
    p->setParameter(pa::room, 0.0f);
    // Within a second the new response (all on the left) is playing.
    bool moved = false;
    for (int i = 0; i < 100 && !moved; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        run(*p, silence(0.05));
        const auto out = run(*p, click);
        moved = rms(out.right) < 1.0e-3 * rms(out.left);
    }
    CHECK(moved);
    CHECK(rms(before.right) > 0.1 * rms(before.left));
}

TEST_CASE("A user's response file stands in for the cabinet, and a bad one says why",
          "[plugins][amp][cabinet]")
{
    plugins::sampler::Loader loader = [](const std::string& path, std::string& error)
    {
        if (path != "good.wav")
        {
            error = "not found";
            return std::shared_ptr<const plugins::sampler::Sample>();
        }
        auto s = std::make_shared<plugins::sampler::Sample>();
        s->path = path;
        s->sampleRate = rate;
        s->left.assign(1000, 0.0f);
        s->left[40] = 1.0f; // a plain delay of 40 samples
        return std::shared_ptr<const plugins::sampler::Sample>(s);
    };
    auto p = plugins::BuiltInFactory(loader).create(pa::type);
    p->prepare(rate, block);
    p->setParameter(pa::cabinet, static_cast<float>(plugins::cabinet::custom));
    p->setParameter(pa::reverbOn, 0.0f);
    p->setState(pa::encodeState("good.wav"));
    pa::settleCabinet(*p);
    CHECK(pa::userResponsePath(*p) == "good.wav");
    CHECK(pa::userResponseError(*p).empty());
    auto click = silence(0.05);
    click.left[0] = click.right[0] = 0.05f;
    const auto out = run(*p, click);
    size_t loudest = 0;
    for (size_t i = 0; i < out.left.size(); ++i)
        if (std::abs(out.left[i]) > std::abs(out.left[loudest]))
            loudest = i;
    CHECK(loudest >= 40);
    CHECK(loudest < 40 + 48);

    p->setState(pa::encodeState("missing.wav"));
    CHECK(pa::userResponseError(*p) == "not found");
    CHECK(pa::userResponsePath(*p) == "missing.wav");
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> ampViolations{0};
void countAmpViolation(const rt::Violation&) noexcept
{
    ampViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("The amp processes without allocating on the real-time thread", "[plugins][amp][rt]")
{
    auto p = makeAmp();
    p->setParameter(pa::cabinetOn, 1.0f);
    p->setParameter(pa::reverbOn, 1.0f);
    p->setParameter(pa::tremoloOn, 1.0f);
    p->setParameter(pa::gate, -60.0f);
    std::vector<float> l(block, 0.3f), r(block, -0.2f);
    const auto previous = rt::setViolationHandler(&countAmpViolation);
    ampViolations = 0;
    for (int round = 0; round < 4; ++round)
    {
        {
            rt::ScopedRealtimeSection section;
            for (int i = 0; i < 40; ++i)
            {
                if (i == 10)
                    p->setParameter(pa::quality, static_cast<float>(round % 3));
                if (i == 20)
                    p->setParameter(pa::treble, 2.0f + static_cast<float>(round));
                if (i == 30)
                    p->setParameter(pa::cabinetOn, round % 2 == 0 ? 0.0f : 1.0f);
                p->process(l.data(), r.data(), block);
            }
            p->setBypassed(round == 2);
        }
        // A new cabinet built off the real-time thread, then swapped in on it.
        p->setParameter(pa::micParam(0, pa::micPosition), 20.0f * static_cast<float>(round));
        pa::settleCabinet(*p);
    }
    rt::setViolationHandler(previous);
    CHECK(ampViolations == 0);
}
#endif

TEST_CASE("Probe: the cabinet's direct response by mic position", "[.probe][ampprobe]")
{
    for (float position : {0.05f, 0.5f, 0.95f})
    {
        plugins::cabinet::Mic m;
        m.position = position;
        std::printf("position %.2f:", position);
        for (double hz : {100.0, 300.0, 1000.0, 2000.0, 3000.0, 4000.0, 6000.0, 8000.0})
            std::printf(" %.0f:%+.1f", hz, plugins::cabinet::directDb(plugins::cabinet::open2x12, m, hz));
        std::printf("\n");
    }
}
