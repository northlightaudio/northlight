#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/Orbit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;
namespace ob = plugins::orbit;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;
constexpr int window = 4800; // whole cycles of 1 kHz, 2 kHz ... 10 kHz at 48 kHz

// A clean machine: no flutter, no hiss, all wet, OUTPUT not following INPUT.
std::unique_ptr<engine::InsertProcessor> makeOrbit()
{
    auto p = plugins::BuiltInFactory().create(ob::type);
    REQUIRE(p != nullptr);
    p->prepare(rate, block);
    p->setBypassed(false);
    p->feedback()->setWatched(true); // as its card does: the meters run only then
    p->setParameter(ob::flutter, 0.0f);
    p->setParameter(ob::hiss, 0.0f);
    p->setParameter(ob::mix, 100.0f);
    p->setParameter(ob::link, 0.0f);
    return p;
}

struct Stereo
{
    std::vector<float> left, right;
};

Stereo run(engine::InsertProcessor& p, Stereo in)
{
    const auto n = static_cast<int>(in.left.size());
    for (int done = 0; done < n; done += block)
    {
        const int count = std::min(block, n - done);
        p.process(in.left.data() + done, in.right.data() + done, count);
    }
    return in;
}

Stereo sine(double hz, float amplitude, double seconds = 0.4)
{
    Stereo s;
    const auto n = static_cast<size_t>(seconds * rate);
    s.left.resize(n);
    for (size_t i = 0; i < n; ++i)
        s.left[i] = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / rate));
    s.right = s.left;
    return s;
}

Stereo silence(double seconds)
{
    Stereo s{std::vector<float>(static_cast<size_t>(seconds * rate), 0.0f), {}};
    s.right = s.left;
    return s;
}

// The amplitude of `hz` over the last `window` samples (a single-bin DFT).
double amplitudeAt(const std::vector<float>& y, double hz)
{
    double re = 0.0, im = 0.0;
    const size_t from = y.size() - window;
    for (size_t i = 0; i < window; ++i)
    {
        const double a = 2.0 * std::numbers::pi * hz * static_cast<double>(i) / rate;
        re += y[from + i] * std::cos(a);
        im += y[from + i] * std::sin(a);
    }
    return 2.0 * std::sqrt(re * re + im * im) / window;
}

double db(double ratio)
{
    return 20.0 * std::log10(std::max(ratio, 1.0e-12));
}

// The machine's gain at `hz` for a tone at `amplitude` (dB).
double gainAt(engine::InsertProcessor& p, double hz, float amplitude)
{
    const auto out = run(p, sine(hz, amplitude));
    return db(amplitudeAt(out.left, hz) / amplitude);
}

double rms(const std::vector<float>& y, size_t from)
{
    double e = 0.0;
    for (size_t i = from; i < y.size(); ++i)
        e += static_cast<double>(y[i]) * y[i];
    return std::sqrt(e / static_cast<double>(y.size() - from));
}

float peak(const std::vector<float>& y, size_t from = 0)
{
    float p = 0.0f;
    for (size_t i = from; i < y.size(); ++i)
        p = std::max(p, std::abs(y[i]));
    return p;
}
} // namespace

TEST_CASE("Orbit's machine: speeds, standards, bumps and levels", "[plugins][orbit]")
{
    CHECK(ob::speedIps(ob::ips7) == 7.5f);
    CHECK(ob::speedIps(ob::ips30) == 30.0f);
    CHECK(ob::operatingLevelDb(ob::plus9) == 9.0f);
    // NAB is 50 us at both slow speeds; CCIR 70 us at 7.5 and 35 us at 15; AES at 30.
    CHECK(ob::emphasisHz(ob::ips15, ob::nab) == Approx(3183.1).margin(0.5));
    CHECK(ob::emphasisHz(ob::ips7, ob::ccir) == Approx(2273.6).margin(0.5));
    CHECK(ob::emphasisHz(ob::ips15, ob::ccir) == Approx(4547.3).margin(0.5));
    CHECK(ob::emphasisHz(ob::ips30, ob::nab) == Approx(9094.6).margin(0.5));
    CHECK(ob::emphasisHz(ob::ips30, ob::ccir) == ob::emphasisHz(ob::ips30, ob::nab));
    CHECK(ob::hasLowEmphasis(ob::ips15, ob::nab));
    CHECK_FALSE(ob::hasLowEmphasis(ob::ips15, ob::ccir));
    CHECK_FALSE(ob::hasLowEmphasis(ob::ips30, ob::nab));
    CHECK(ob::headBumpHz(ob::ips30) == Approx(2.0 * ob::headBumpHz(ob::ips15)));
    CHECK(ob::gapLossHz(ob::ips7, rate) < ob::gapLossHz(ob::ips15, rate));
    CHECK(ob::gapLossHz(ob::ips30, 44100.0) <= 0.45 * 44100.0);
    CHECK(ob::hissDbfs(0.0f, ob::ips15) <= -120.0f);
    CHECK(ob::hissDbfs(50.0f, ob::ips7) == Approx(ob::hissDbfs(50.0f, ob::ips15) + 3.0f));
    // Bias moves the reversible share: under-biased shows the hysteresis.
    CHECK(ob::tapeModel(ob::classic, -100.0f).c < ob::tapeModel(ob::classic, 0.0f).c);
    CHECK(ob::tapeModel(ob::classic, 100.0f).c > ob::tapeModel(ob::classic, 0.0f).c);
    CHECK(ob::tapeModel(ob::modern, 0.0f).ms > ob::tapeModel(ob::classic, 0.0f).ms);
    CHECK(ob::fieldGain(ob::plus9) == Approx(ob::fieldGain(ob::plus3) * std::pow(10.0, 6.0 / 20.0)));
}

TEST_CASE("Orbit passes a quiet signal at unity, 1.5 ms late, and says so", "[plugins][orbit]")
{
    auto p = makeOrbit();
    CHECK(p->latencySamples() == 72);
    CHECK(gainAt(*p, 1000.0, 0.01f) == Approx(0.0).margin(0.5));
    // All dry, the input comes out exactly the latency later.
    p->setParameter(ob::mix, 0.0f);
    run(*p, silence(0.2)); // the mix glides over 15 ms
    auto in = silence(0.05);
    in.left[100] = in.right[100] = 0.5f;
    const auto out = run(*p, in);
    CHECK(out.left[172] == Approx(0.5f).margin(1.0e-4));
    CHECK(peak(out.left, 173) < 1.0e-4f);
}

TEST_CASE("Orbit's tape compresses softly with mostly odd harmonics as it is driven", "[plugins][orbit]")
{
    const auto measure = [](float amplitude, double& gain, double& h2, double& h3)
    {
        auto p = makeOrbit();
        const auto out = run(*p, sine(1000.0, amplitude));
        const double fundamental = amplitudeAt(out.left, 1000.0);
        gain = db(fundamental / amplitude);
        h2 = db(amplitudeAt(out.left, 2000.0) / fundamental);
        h3 = db(amplitudeAt(out.left, 3000.0) / fundamental);
    };
    double quietGain = 0.0, quietH2 = 0.0, quietH3 = 0.0;
    double loudGain = 0.0, loudH2 = 0.0, loudH3 = 0.0;
    measure(0.126f, quietGain, quietH2, quietH3); // 0 VU
    measure(1.0f, loudGain, loudH2, loudH3);      // full scale: +18 VU
    CHECK(quietGain == Approx(0.0).margin(0.5));
    CHECK(quietH3 < -45.0); // a tenth of a percent or so at 0 VU
    CHECK(loudGain < quietGain - 1.0);
    CHECK(loudH3 > quietH3 + 20.0);
    CHECK(loudH3 > loudH2 + 20.0); // symmetric: odd harmonics
    CHECK(loudH3 < -10.0);         // soft, not a square
}

TEST_CASE("Orbit's highs saturate first: the record EQ drives them harder", "[plugins][orbit]")
{
    auto p = makeOrbit();
    const double lowQuiet = gainAt(*p, 1000.0, 0.01f), lowLoud = gainAt(*p, 1000.0, 0.5f);
    const double highQuiet = gainAt(*p, 10000.0, 0.01f), highLoud = gainAt(*p, 10000.0, 0.5f);
    CHECK(highQuiet - highLoud > (lowQuiet - lowLoud) + 1.0);
}

TEST_CASE("Orbit's operating level and INPUT set how hard the tape is hit", "[plugins][orbit]")
{
    const auto squash = [](float levelChoice, float inputDb)
    {
        auto p = makeOrbit();
        p->setParameter(ob::level, levelChoice);
        p->setParameter(ob::input, inputDb);
        const double quiet = gainAt(*p, 1000.0, 0.01f);
        return quiet - gainAt(*p, 1000.0, 0.5f);
    };
    CHECK(squash(ob::plus9, 0.0f) > squash(ob::plus3, 0.0f) + 0.5);
    CHECK(squash(ob::plus6, 9.0f) > squash(ob::plus6, 0.0f) + 0.5);
}

TEST_CASE("Orbit's bias: under-biased is gritty and bright, over-biased clean and dark", "[plugins][orbit]")
{
    const auto measure = [](float biasPercent, double& h3, double& highs)
    {
        auto p = makeOrbit();
        p->setParameter(ob::bias, biasPercent);
        const auto out = run(*p, sine(1000.0, 0.05f));
        h3 = db(amplitudeAt(out.left, 3000.0) / amplitudeAt(out.left, 1000.0));
        highs = gainAt(*p, 12000.0, 0.005f);
    };
    double underH3 = 0.0, underHighs = 0.0, overH3 = 0.0, overHighs = 0.0;
    measure(-100.0f, underH3, underHighs);
    measure(100.0f, overH3, overHighs);
    CHECK(underH3 > overH3 + 6.0);
    CHECK(underHighs > overHighs + 3.0);
}

TEST_CASE("Orbit's speed moves the head bump and the high end", "[plugins][orbit]")
{
    const auto at = [](float speedChoice, double hz)
    {
        auto p = makeOrbit();
        p->setParameter(ob::speed, speedChoice);
        return gainAt(*p, hz, 0.005f);
    };
    // The bump sits where its speed puts it (15 ips: about 52 Hz; 30 ips: about 105 Hz).
    CHECK(at(ob::ips15, 50.0) > at(ob::ips15, 500.0) + 1.0);
    CHECK(at(ob::ips30, 100.0) > at(ob::ips15, 100.0) + 1.0);
    // 7.5 ips carries fewer highs.
    CHECK(at(ob::ips7, 16000.0) < at(ob::ips15, 16000.0) - 1.0);
}

TEST_CASE("Orbit's Link keeps the loudness while INPUT drives the tape", "[plugins][orbit]")
{
    auto off = makeOrbit();
    off->setParameter(ob::input, 6.0f);
    CHECK(gainAt(*off, 1000.0, 0.01f) == Approx(6.0).margin(0.6));
    auto on = makeOrbit();
    on->setParameter(ob::input, 6.0f);
    on->setParameter(ob::link, 1.0f);
    CHECK(gainAt(*on, 1000.0, 0.01f) == Approx(0.0).margin(0.6));
}

TEST_CASE("Orbit hisses at its level only when asked, more at a slower speed", "[plugins][orbit]")
{
    auto p = makeOrbit();
    CHECK(peak(run(*p, silence(0.3)).left) < 1.0e-6f);
    p->setParameter(ob::hiss, 50.0f);
    const auto noisy = run(*p, silence(0.5));
    CHECK(db(rms(noisy.left, 4800)) == Approx(ob::hissDbfs(50.0f, ob::ips15)).margin(2.0));
    p->setParameter(ob::speed, static_cast<float>(ob::ips7));
    const auto slower = run(*p, silence(0.5));
    CHECK(rms(slower.left, 4800) > rms(noisy.left, 4800));
    // The two tracks hiss apart.
    CHECK(std::abs(noisy.left[10000] - noisy.right[10000]) > 0.0f);
}

TEST_CASE("Orbit's flutter wanders the pitch; without it the tone holds still", "[plugins][orbit]")
{
    const auto deviation = [](float flutterPercent)
    {
        auto steady = makeOrbit();
        auto wobbly = makeOrbit();
        wobbly->setParameter(ob::flutter, flutterPercent);
        const auto a = run(*steady, sine(3000.0, 0.01f, 2.0));
        const auto b = run(*wobbly, sine(3000.0, 0.01f, 2.0));
        double diff = 0.0;
        for (size_t i = 4800; i < a.left.size(); ++i)
            diff = std::max(diff, static_cast<double>(std::abs(a.left[i] - b.left[i])));
        return diff;
    };
    CHECK(deviation(0.0f) < 1.0e-6);
    CHECK(deviation(0.3f) > 0.002); // a fifth of the tone's amplitude: its phase moves
}

TEST_CASE("Orbit's mix blends the latency-matched dry signal", "[plugins][orbit]")
{
    // Quiet, the tape is all but transparent: half and half sums back to unity
    // only if the dry signal is delayed to match the tape's.
    auto p = makeOrbit();
    p->setParameter(ob::mix, 50.0f);
    CHECK(gainAt(*p, 1000.0, 0.01f) == Approx(0.0).margin(0.3));
    CHECK(gainAt(*p, 8000.0, 0.01f) == Approx(0.0).margin(0.8)); // the gap loss turns the phase a little
}

TEST_CASE("Orbit bypassed passes the input, delayed by its latency", "[plugins][orbit]")
{
    auto p = makeOrbit();
    p->setBypassed(true);
    p->prepare(rate, block); // settled bypass
    auto in = silence(0.05);
    in.left[10] = 0.3f;
    const auto out = run(*p, in);
    CHECK(out.left[82] == Approx(0.3f).margin(1.0e-5));
    CHECK(peak(out.left, 83) < 1.0e-5f);
}

TEST_CASE("Orbit stays finite and bounded however hard it is driven", "[plugins][orbit]")
{
    auto p = makeOrbit();
    p->setParameter(ob::input, 18.0f);
    p->setParameter(ob::level, static_cast<float>(ob::plus9));
    p->setParameter(ob::bias, -100.0f);
    p->setParameter(ob::flutter, ob::maxFlutterPercent);
    p->setParameter(ob::hiss, 100.0f);
    Stereo in;
    in.left.resize(static_cast<size_t>(rate * 0.5));
    std::uint32_t state = 1;
    for (auto& x : in.left)
    {
        state = state * 1664525u + 1013904223u;
        x = static_cast<float>(state) / 4294967296.0f * 2.0f - 1.0f;
    }
    in.right = in.left;
    const auto out = run(*p, in);
    bool finite = true;
    for (const float x : out.left)
        finite = finite && std::isfinite(x);
    CHECK(finite);
    CHECK(peak(out.left) < 8.0f);
}

TEST_CASE("Orbit's meters read the chosen point, and the counter the song's position", "[plugins][orbit]")
{
    auto p = makeOrbit();
    auto* fb = p->feedback();
    REQUIRE(fb != nullptr);
    p->setParameter(ob::input, 6.0f);
    p->setParameter(ob::meter, static_cast<float>(ob::meterInput));
    engine::PlayHead head;
    head.playing = true;
    head.samples = 96000;
    p->setPlayHead(head);
    run(*p, sine(1000.0, 0.01f, 0.6));
    // A VU's level: a sine's rms (0.707 of its peak), steady, 6 dB up after INPUT.
    const auto frame = fb->latest();
    const double rms = 0.01 * std::numbers::sqrt2 / 2.0;
    CHECK(db(frame.extra / rms) == Approx(6.0).margin(0.5));
    CHECK(db(frame.extra2 / rms) == Approx(6.0).margin(0.5));
    CHECK(fb->transportSeconds() == Approx(2.0));
    CHECK(fb->transportPlaying());
    // The output point reads the output: here the same 6 dB louder (Link off).
    p->setParameter(ob::meter, static_cast<float>(ob::meterOutput));
    run(*p, sine(1000.0, 0.01f, 0.6));
    CHECK(db(fb->latest().extra / rms) == Approx(6.0).margin(0.7));
    // Quiet, the tape is barely compressing.
    CHECK(fb->latest().reduction < 0.5f);
}

TEST_CASE("Orbit's meters move as a VU's needle: most of a step by 100 ms, there by 300 ms",
          "[plugins][orbit]")
{
    auto p = makeOrbit();
    p->setParameter(ob::meter, static_cast<float>(ob::meterInput));
    const auto tone = sine(1000.0, 0.1f, 0.6);
    const double steady = 0.1 * std::numbers::sqrt2 / 2.0;
    std::vector<float> l(48), r(48); // 1 ms blocks
    std::vector<double> reading;
    for (size_t done = 0; done + 48 <= tone.left.size(); done += 48)
    {
        std::copy(tone.left.begin() + done, tone.left.begin() + done + 48, l.begin());
        r = l;
        p->process(l.data(), r.data(), 48);
        reading.push_back(db(p->feedback()->latest().extra / steady));
    }
    CHECK(reading[20] < -6.0);                      // 20 ms in: well short
    CHECK(reading[100] > -4.0);                     // 100 ms: most of the way
    CHECK(reading[300] == Approx(0.0).margin(0.4)); // 300 ms: there
    CHECK(reading[550] == Approx(0.0).margin(0.3));
    // It never jumps: at its steepest a VU rises under 1% of the step a millisecond, so
    // under 5% from one published frame (5 ms) to the next.
    for (size_t i = 5; i < reading.size(); i += 5)
        CHECK(std::pow(10.0, reading[i] / 20.0) - std::pow(10.0, reading[i - 5] / 20.0) < 0.05);
}

TEST_CASE("Orbit's meters read the average, so a signal's peaks do not pin them", "[plugins][orbit]")
{
    // Bursts as drums are: a tenth of the time at -6 dBFS peak, silence between.
    auto p = makeOrbit();
    p->setParameter(ob::meter, static_cast<float>(ob::meterInput));
    Stereo in = sine(1000.0, 0.5f, 1.0);
    for (size_t i = 0; i < in.left.size(); ++i)
        if ((i / 480) % 10 != 0)
            in.left[i] = in.right[i] = 0.0f;
    std::vector<float> l(block), r(block);
    float loudest = 0.0f;
    for (size_t done = 0; done + block <= in.left.size(); done += block)
    {
        std::copy(in.left.begin() + done, in.left.begin() + done + block, l.begin());
        std::copy(in.right.begin() + done, in.right.begin() + done + block, r.begin());
        p->process(l.data(), r.data(), block);
        loudest = std::max(loudest, p->feedback()->latest().extra);
    }
    // A peak meter would read the -6 dBFS peaks (+12 VU); the average sits well under 0 VU.
    CHECK(db(loudest) - ob::vuReferenceDbfs < 0.0);
    CHECK(db(loudest) - ob::vuReferenceDbfs > -14.0);
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> orbitViolations{0};
void countOrbitViolation(const rt::Violation&) noexcept
{
    orbitViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Orbit processes without allocating on the real-time thread", "[plugins][orbit][rt]")
{
    auto p = makeOrbit();
    p->setParameter(ob::hiss, 60.0f);
    p->setParameter(ob::flutter, 0.2f);
    std::vector<float> l(block, 0.4f), r(block, -0.3f);
    const auto previous = rt::setViolationHandler(&countOrbitViolation);
    orbitViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int i = 0; i < 200; ++i)
        {
            if (i == 50)
                p->setParameter(ob::speed, static_cast<float>(ob::ips30));
            if (i == 100)
                p->setParameter(ob::bias, 60.0f);
            if (i == 150)
                p->setParameter(ob::tape, static_cast<float>(ob::modern));
            p->process(l.data(), r.data(), block);
        }
        p->setBypassed(true);
        for (int i = 0; i < 20; ++i)
            p->process(l.data(), r.data(), block);
    }
    rt::setViolationHandler(previous);
    CHECK(orbitViolations == 0);
}
#endif
