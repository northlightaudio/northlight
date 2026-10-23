#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/BuiltIns.h"
#include "plugins/TapeEcho.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;
namespace te = plugins::tapeecho;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

// A clean machine: no wobble, new tape, all wet, one head.
std::unique_ptr<engine::InsertProcessor> makeEcho()
{
    auto p = plugins::BuiltInFactory().create(te::type);
    REQUIRE(p != nullptr);
    p->prepare(rate, block);
    p->setBypassed(false);
    p->setParameter(te::heads, 0.0f);
    p->setParameter(te::feedback, 0.0f);
    p->setParameter(te::wow, 0.0f);
    p->setParameter(te::flutter, 0.0f);
    p->setParameter(te::age, 0.0f);
    p->setParameter(te::mix, 100.0f);
    p->setParameter(te::killDry, 1.0f);
    return p;
}

struct Stereo
{
    std::vector<float> left, right;
};

// The input through the processor in blocks.
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

Stereo impulse(double seconds, float amplitude)
{
    Stereo s{std::vector<float>(static_cast<size_t>(seconds * rate), 0.0f), {}};
    s.left[0] = amplitude;
    s.right = s.left;
    return s;
}

Stereo silence(double seconds)
{
    Stereo s{std::vector<float>(static_cast<size_t>(seconds * rate), 0.0f), {}};
    s.right = s.left;
    return s;
}

int loudestIn(const std::vector<float>& y, int from, int to)
{
    int best = from;
    for (int i = from; i < to && i < static_cast<int>(y.size()); ++i)
        if (std::abs(y[static_cast<size_t>(i)]) > std::abs(y[static_cast<size_t>(best)]))
            best = i;
    return best;
}

double energy(const std::vector<float>& y, int from, int to)
{
    double e = 0.0;
    for (int i = std::max(0, from); i < to && i < static_cast<int>(y.size()); ++i)
        e += static_cast<double>(y[static_cast<size_t>(i)]) * y[static_cast<size_t>(i)];
    return e;
}

float peak(const std::vector<float>& y, int from = 0)
{
    float p = 0.0f;
    for (size_t i = static_cast<size_t>(from); i < y.size(); ++i)
        p = std::max(p, std::abs(y[i]));
    return p;
}
} // namespace

TEST_CASE("Tape echo's head modes, sync and motor range", "[plugins][tapeecho]")
{
    CHECK(te::headMask(0.0f) == 0b0001);
    CHECK(te::headMask(4.0f) == 0b0101);
    CHECK(te::headMask(7.0f) == 0b1111);
    CHECK(te::headMask(99.0f) == 0b1111);
    CHECK(te::activeHeadCount(0b1101) == 3);
    CHECK(te::headOneSeconds(250.0f, 0.0f, 120.0) == Approx(0.25));
    CHECK(te::headOneSeconds(5.0f, 0.0f, 120.0) == Approx(te::minTimeMs * 0.001));
    // 1/8 at 120 bpm is 250 ms; a whole note at 60 bpm is past the motor's range.
    CHECK(te::headOneSeconds(100.0f, 8.0f, 120.0) == Approx(0.25));
    CHECK_FALSE(te::syncClamped(8.0f, 120.0));
    CHECK(te::headOneSeconds(100.0f, 16.0f, 60.0) == Approx(te::maxTimeMs * 0.001));
    CHECK(te::syncClamped(16.0f, 60.0));
    // Older tape and a slower motor are darker.
    CHECK(te::highLossHz(80.0f, 0.18) < te::highLossHz(10.0f, 0.18));
    CHECK(te::highLossHz(30.0f, 0.8) < te::highLossHz(30.0f, 0.1));
}

TEST_CASE("Tape echo plays each active head at its spacing from the record head", "[plugins][tapeecho]")
{
    auto p = makeEcho();
    p->setParameter(te::heads, 4.0f); // heads 1 + 3
    p->setParameter(te::time, 100.0f);
    const auto out = run(*p, impulse(0.5, 0.2f));
    const int unit = static_cast<int>(0.1 * rate);
    const int first = loudestIn(out.left, unit / 2, 2 * unit - unit / 2);
    const int third = loudestIn(out.left, 2 * unit + unit / 2, 4 * unit - unit / 2);
    CHECK(std::abs(first - unit) <= 4);
    CHECK(std::abs(third - 3 * unit) <= 4);
    // Head 2 is off: nothing at two units.
    CHECK(energy(out.left, 2 * unit - 200, 2 * unit + 200) <
          1.0e-4 * energy(out.left, unit - 200, unit + 200));
    // Nothing before the first head: no latency, no dry.
    CHECK(peak(std::vector<float>(out.left.begin(), out.left.begin() + unit - 100)) < 1.0e-6f);
}

TEST_CASE("Tape echo synced puts head 1 on the note value at the tempo", "[plugins][tapeecho]")
{
    auto p = makeEcho();
    engine::PlayHead head;
    head.tempoBpm = 120.0;
    p->setPlayHead(head);
    p->setParameter(te::sync, 8.0f); // 1/8: 250 ms
    const auto out = run(*p, impulse(0.5, 0.2f));
    CHECK(std::abs(loudestIn(out.left, 1000, 23000) - 12000) <= 4);
}

namespace
{
// The frequency of a signal over [from, to) from its rising zero crossings.
double frequencyOver(const std::vector<float>& y, int from, int to)
{
    int first = -1, last = -1, count = 0;
    for (int i = from + 1; i < to; ++i)
        if (y[static_cast<size_t>(i - 1)] < 0.0f && y[static_cast<size_t>(i)] >= 0.0f)
        {
            if (first < 0)
                first = i;
            last = i;
            ++count;
        }
    return count > 1 ? (count - 1) * rate / (last - first) : 0.0;
}
} // namespace

TEST_CASE("Tape echo's motor glides to a new speed: the pitch bends, nothing clicks", "[plugins][tapeecho]")
{
    auto p = makeEcho();
    p->setParameter(te::time, 200.0f);
    constexpr double hz = 500.0;
    double phase = 0.0;
    const auto sine = [&](double seconds)
    {
        Stereo s{std::vector<float>(static_cast<size_t>(seconds * rate)), {}};
        for (auto& v : s.left)
        {
            v = 0.2f * static_cast<float>(std::sin(phase));
            phase += 2.0 * std::numbers::pi * hz / rate;
        }
        s.right = s.left;
        return s;
    };
    const auto before = run(*p, sine(1.0));
    CHECK(frequencyOver(before.left, 24000, 48000) == Approx(hz).margin(1.0));
    p->setParameter(te::time, 400.0f); // the motor slows: the delay grows, the pitch falls
    const auto after = run(*p, sine(2.0));
    const double during = frequencyOver(after.left, 2400, 12000);
    CHECK(during < 0.92 * hz);
    CHECK(during > 0.3 * hz);
    CHECK(frequencyOver(after.left, 72000, 96000) == Approx(hz).margin(1.5));
    // No step anywhere: the largest sample-to-sample change stays within what a
    // pitch bend of a 0.2 sine can make.
    float step = 0.0f;
    for (size_t i = 1; i < after.left.size(); ++i)
        step = std::max(step, std::abs(after.left[i] - after.left[i - 1]));
    const float sineStep = 0.2f * static_cast<float>(2.0 * std::numbers::pi * hz / rate);
    CHECK(step < 1.6f * sineStep);
}

TEST_CASE("Tape echo past 100% feedback self-oscillates into the tape and stays bounded",
          "[plugins][tapeecho]")
{
    auto p = makeEcho();
    p->setParameter(te::time, 80.0f);
    p->setParameter(te::feedback, 120.0f);
    p->setParameter(te::heads, 7.0f); // all four heads
    auto out = run(*p, impulse(1.0, 0.5f));
    float most = peak(out.left);
    for (int i = 0; i < 8; ++i)
    {
        out = run(*p, silence(1.0));
        most = std::max(most, peak(out.left));
    }
    CHECK(peak(out.left) > 0.05f); // still going after nine seconds
    CHECK(most < 1.0f);            // and held by the tape's saturation
    // The last second's frames show the tape saturating.
    std::uint64_t cursor = 0;
    std::vector<engine::ProcessorFeedback::Frame> frames(200);
    const int n = p->feedback()->readHistory(cursor, frames.data(), 200);
    float squash = 0.0f;
    for (int i = 0; i < n; ++i)
        squash = std::max(squash, frames[static_cast<size_t>(i)].reduction);
    CHECK(squash > 0.5f);

    // Below the edge, the repeats die away.
    auto q = makeEcho();
    q->setParameter(te::time, 80.0f);
    q->setParameter(te::feedback, 60.0f);
    run(*q, impulse(1.0, 0.5f));
    for (int i = 0; i < 6; ++i)
        out = run(*q, silence(1.0));
    CHECK(peak(out.left) < 1.0e-4f);
}

TEST_CASE("Tape echo's loop gain follows the feedback and the loop's tone", "[plugins][tapeecho]")
{
    const float flat = te::loopGain(100.0f, 0.0f, 0.0f, 0.0f, 0.18, rate);
    CHECK(flat == Approx(1.0f).margin(0.03f));
    CHECK(te::loopGain(50.0f, 0.0f, 0.0f, 0.0f, 0.18, rate) == Approx(0.5f * flat));
    CHECK(te::loopGain(50.0f, 12.0f, 0.0f, 0.0f, 0.18, rate) > 1.5f);
    CHECK(te::loopGain(50.0f, -12.0f, -12.0f, 0.0f, 0.18, rate) < 0.5f);
}

TEST_CASE("Tape echo's repeats lose their highs with each pass", "[plugins][tapeecho]")
{
    auto p = makeEcho();
    p->setParameter(te::time, 100.0f);
    p->setParameter(te::feedback, 80.0f);
    p->setParameter(te::age, 70.0f);
    const auto out = run(*p, impulse(0.6, 0.3f));
    const int unit = static_cast<int>(0.1 * rate);
    // High-frequency share of each echo: the energy of its slope over its energy.
    std::vector<double> brightness;
    for (int n = 1; n <= 4; ++n)
    {
        double e = 0.0, d = 0.0;
        for (int i = n * unit - 60; i < n * unit + 200; ++i)
        {
            const double v = out.left[static_cast<size_t>(i)];
            const double dv = v - out.left[static_cast<size_t>(i - 1)];
            e += v * v;
            d += dv * dv;
        }
        REQUIRE(e > 0.0);
        brightness.push_back(d / e);
    }
    for (size_t i = 1; i < brightness.size(); ++i)
        CHECK(brightness[i] < 0.9 * brightness[i - 1]);
}

TEST_CASE("Tape echo's ping-pong sends the repeats to alternate sides", "[plugins][tapeecho]")
{
    auto p = makeEcho();
    p->setParameter(te::time, 100.0f);
    p->setParameter(te::feedback, 60.0f);
    p->setParameter(te::stereo, static_cast<float>(te::stereoPingPong));
    p->setParameter(te::spread, 100.0f);
    const auto out = run(*p, impulse(0.35, 0.2f));
    const int unit = static_cast<int>(0.1 * rate);
    const auto window = [&](const std::vector<float>& y, int n)
    { return energy(y, n * unit - 100, n * unit + 300); };
    CHECK(window(out.left, 1) > 0.0);
    CHECK(window(out.right, 1) < 1.0e-6 * window(out.left, 1));
    CHECK(window(out.left, 2) < 1.0e-6 * window(out.right, 2));
    CHECK(window(out.right, 2) > 0.1 * window(out.left, 1));
}

TEST_CASE("Tape echo's mix and kill dry", "[plugins][tapeecho]")
{
    auto p = makeEcho();
    p->setParameter(te::killDry, 0.0f);
    p->setParameter(te::mix, 0.0f);
    auto in = impulse(0.3, 0.5f);
    in.left[1000] = 0.25f;
    const auto dry = run(*p, in);
    CHECK(dry.left[0] == Approx(0.5f).margin(1.0e-6f));
    CHECK(dry.left[1000] == Approx(0.25f).margin(1.0e-6f));
    CHECK(peak(dry.left, 1001) < 1.0e-6f);

    auto q = makeEcho();
    q->setParameter(te::mix, 0.0f); // and kill dry: nothing at all
    CHECK(peak(run(*q, impulse(0.3, 0.5f)).left) < 1.0e-6f);
}

TEST_CASE("Tape echo's drive saturates the tape without blowing up the level", "[plugins][tapeecho]")
{
    auto p = makeEcho();
    p->setParameter(te::drive, 18.0f);
    p->setParameter(te::heads, 7.0f);
    p->setParameter(te::feedback, 90.0f);
    Stereo s{std::vector<float>(static_cast<size_t>(2.0 * rate)), {}};
    for (size_t i = 0; i < s.left.size(); ++i)
        s.left[i] = 0.99f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * i / rate));
    s.right = s.left;
    const auto out = run(*p, s);
    CHECK(peak(out.left) < 1.2f);
    CHECK(p->feedback()->latest().reduction > 3.0f);
}

TEST_CASE("Tape echo's spring disperses: its highs arrive after its lows, and it dies away",
          "[plugins][tapeecho]")
{
    // The spring's part: the echo with the spring minus the echo without.
    const auto echo = [](float spring)
    {
        auto p = makeEcho();
        p->setParameter(te::time, 20.0f);
        p->setParameter(te::spring, spring);
        return run(*p, impulse(3.0, 0.3f)).left;
    };
    const auto with = echo(100.0f), without = echo(0.0f);
    std::vector<float> wet(with.size());
    for (size_t i = 0; i < wet.size(); ++i)
        wet[i] = with[i] - without[i];
    const auto centroid = [&](double hz)
    {
        plugins::Biquad band;
        band.setBandPass(rate, hz, 2.0);
        double sum = 0.0, weighted = 0.0;
        // The first pass through the spring (the echo at 20 ms, the loop's
        // second time round at about 56 ms).
        for (size_t i = 0; i < static_cast<size_t>(0.05 * rate); ++i)
        {
            const double v = band.process(wet[i]);
            sum += v * v;
            weighted += v * v * static_cast<double>(i);
        }
        return sum > 0.0 ? weighted / sum : 0.0;
    };
    CHECK(centroid(3500.0) > centroid(500.0) + 0.003 * rate);
    const double early = energy(wet, 0, static_cast<int>(0.5 * rate));
    CHECK(early > 1.0e-4);
    CHECK(energy(wet, static_cast<int>(2.5 * rate), static_cast<int>(3.0 * rate)) < 1.0e-6 * early);
}

TEST_CASE("Tape echo bypassed passes the input and keeps its tape running", "[plugins][tapeecho]")
{
    auto p = makeEcho();
    p->setParameter(te::time, 50.0f);
    p->setBypassed(true);
    p->prepare(rate, block); // settled bypass
    auto in = impulse(0.2, 0.4f);
    const auto out = run(*p, in);
    CHECK(out.left[0] == Approx(0.4f));
    CHECK(peak(out.left, 1) < 1.0e-6f);
    // Back on, the echo of what played while bypassed is still on the tape.
    p->setBypassed(false);
    const auto later = run(*p, silence(0.1)); // the impulse's echo, 50 ms after it
    CHECK(peak(later.left) < 1.0e-6f);        // (already past: 200 ms have gone by)
    auto q = makeEcho();
    q->setParameter(te::time, 150.0f);
    q->setBypassed(true);
    q->prepare(rate, block);
    run(*q, impulse(0.1, 0.4f));
    q->setBypassed(false);
    CHECK(peak(run(*q, silence(0.1)).left) > 0.05f); // its echo at 150 ms arrives
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> tapeViolations{0};
void countTapeViolation(const rt::Violation&) noexcept
{
    tapeViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Tape echo processes without allocating on the real-time thread", "[plugins][tapeecho][rt]")
{
    auto p = makeEcho();
    p->setParameter(te::heads, 7.0f);
    p->setParameter(te::feedback, 110.0f);
    p->setParameter(te::wow, 80.0f);
    p->setParameter(te::flutter, 80.0f);
    p->setParameter(te::age, 90.0f);
    p->setParameter(te::spring, 60.0f);
    p->setParameter(te::stereo, static_cast<float>(te::stereoPingPong));
    std::vector<float> l(block, 0.4f), r(block, -0.3f);
    const auto previous = rt::setViolationHandler(&countTapeViolation);
    tapeViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int i = 0; i < 200; ++i)
        {
            if (i == 100)
                p->setParameter(te::time, 600.0f);
            p->process(l.data(), r.data(), block);
        }
        p->setBypassed(true);
        for (int i = 0; i < 20; ++i)
            p->process(l.data(), r.data(), block);
    }
    rt::setViolationHandler(previous);
    CHECK(tapeViolations == 0);
}
#endif
