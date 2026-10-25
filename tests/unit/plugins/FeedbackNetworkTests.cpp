#include "engine/rt/RealtimeGuard.h"
#include "plugins/FeedbackNetwork.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace northlight;
using Catch::Approx;
namespace fdn = plugins::fdn;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 256;

// A short, dense tuning unlike Bloom's, so the tests show the network follows
// whatever tuning it is given.
fdn::Tuning plateTuning()
{
    fdn::Tuning t;
    t.lineMs = {9.7f, 11.3f, 13.1f, 14.9f, 16.7f, 18.1f, 20.3f, 22.9f};
    t.allpassMs = {1.3f, 1.9f, 2.3f, 2.9f, 3.1f, 3.7f, 4.1f, 4.7f};
    t.diffuserLeftMs = {0.9f, 1.4f, 2.1f, 3.3f};
    t.diffuserRightMs = {1.0f, 1.6f, 2.3f, 3.6f};
    t.maxScale = 1.5f;
    t.maxModSeconds = 0.002f;
    return t;
}

struct Noise
{
    std::uint32_t state = 4242u;
    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
};

struct Runner
{
    fdn::FeedbackNetwork network;
    fdn::BlockSettings settings;
    fdn::Frame frame;
    Noise noise;

    explicit Runner(float decaySeconds)
    {
        network.prepare(plateTuning(), rate);
        settings.decaySeconds = decaySeconds;
        settings.dampingHz = 20000.0f;
        settings.lowCutHz = 20.0f;
        settings.modSamples = 0.0f;
    }

    // `seconds` of noise (amplitude) or silence (0); the tail's left channel.
    std::vector<float> run(double seconds, float amplitude)
    {
        std::vector<float> out;
        out.reserve(static_cast<size_t>(seconds * rate) + block);
        spin(seconds, amplitude, &out);
        return out;
    }

    // The same, keeping the tail only when `out` is given (nothing allocates).
    void spin(double seconds, float amplitude, std::vector<float>* out = nullptr)
    {
        const int total = static_cast<int>(seconds * rate);
        for (int done = 0; done < total; done += block)
        {
            network.beginBlock(settings);
            for (int i = 0; i < block; ++i)
            {
                float l = amplitude * noise.next(), r = amplitude * noise.next();
                network.diffuse(l, r);
                if (frame.freeze > 0.5f)
                    l = r = 0.0f; // frozen, the input stays out
                float tl = 0.0f, tr = 0.0f;
                network.process(frame, l, r, tl, tr);
                if (out != nullptr)
                    out->push_back(tl);
            }
        }
    }
};

double rmsOf(const std::vector<float>& x, double from, double to)
{
    const auto a = static_cast<size_t>(from * rate), b = static_cast<size_t>(to * rate);
    double sum = 0.0;
    for (size_t i = a; i < b; ++i)
        sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(b - a));
}

double db(double x)
{
    return 20.0 * std::log10(std::max(1.0e-12, x));
}
} // namespace

TEST_CASE("The feedback network's tail falls 60 dB over the decay time, at any tuning", "[plugins][fdn]")
{
    const auto measured = [](float decay)
    {
        Runner runner(decay);
        runner.run(0.3, 0.5f);
        const auto tail = runner.run(1.5, 0.0f);
        const double fall = db(rmsOf(tail, 0.2, 0.4)) - db(rmsOf(tail, 0.7, 0.9));
        return 0.5 * 60.0 / fall;
    };
    CHECK(measured(0.8f) == Approx(0.8).epsilon(0.25));
    CHECK(measured(2.0f) == Approx(2.0).epsilon(0.25));
}

TEST_CASE("A frozen feedback network holds its tail, and lets go when thawed", "[plugins][fdn]")
{
    Runner runner(1.0f);
    runner.run(0.3, 0.5f);
    runner.frame.freeze = 1.0f;
    const auto held = runner.run(3.0, 0.5f);
    const double early = db(rmsOf(held, 0.5, 1.0)), late = db(rmsOf(held, 2.5, 3.0));
    CHECK(early > -40.0);
    // Reading between samples loses a little of the highs each pass, so "held"
    // is within 3 dB, as Bloom asks of its freeze.
    CHECK(std::abs(early - late) < 3.0);
    runner.frame.freeze = 0.0f;
    const auto thawed = runner.run(2.0, 0.0f);
    CHECK(db(rmsOf(thawed, 1.5, 2.0)) < early - 40.0);
}

TEST_CASE("The feedback network's governor holds its energy under the ceiling", "[plugins][fdn]")
{
    Runner runner(1.0f);
    // Input fed into a lossless loop would build for ever; the governor holds the
    // loop (not the input itself, which here is well under the ceiling).
    runner.frame.freeze = 1.0f;
    for (int i = 0; i < 20; ++i)
    {
        runner.network.beginBlock(runner.settings);
        for (int n = 0; n < block * 20; ++n)
        {
            float l = 0.3f * runner.noise.next(), r = 0.3f * runner.noise.next(), tl = 0.0f, tr = 0.0f;
            runner.network.process(runner.frame, l, r, tl, tr);
        }
    }
    CHECK(std::sqrt(runner.network.energy()) < plateTuning().energyCeiling * 1.25f);
    CHECK(std::sqrt(runner.network.energy()) > plateTuning().energyCeiling * 0.5f); // it did build up
    CHECK_FALSE(runner.network.diverged());
}

TEST_CASE("Clearing the feedback network silences it at once", "[plugins][fdn]")
{
    Runner runner(4.0f);
    runner.run(0.3, 0.5f);
    CHECK(runner.network.energy() > 1.0e-6f);
    runner.network.clear();
    const auto after = runner.run(0.1, 0.0f);
    for (const float v : after)
        REQUIRE(v == 0.0f);
    CHECK(runner.network.energy() == 0.0f);
}

TEST_CASE("The feedback network's decay holds between its filters and shortens past them", "[plugins][fdn]")
{
    const auto t = plateTuning();
    CHECK(fdn::decayAt(t, 1000.0f, 2.0f, 1.0f, 8000.0f, 100.0f) == Approx(2.0).epsilon(0.1));
    CHECK(fdn::decayAt(t, 14000.0f, 2.0f, 1.0f, 3000.0f, 100.0f) < 1.0f);
    CHECK(fdn::decayAt(t, 40.0f, 2.0f, 1.0f, 8000.0f, 400.0f) < 1.0f);
    // Past both filters, never quicker than the ratio the tuning sets.
    CHECK(fdn::decayAt(t, 18000.0f, 2.0f, 1.0f, 500.0f, 20.0f) > 2.0f / t.bandDecayRatio * 0.9f);
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> fdnViolations{0};
void countFdnViolation(const rt::Violation&) noexcept
{
    fdnViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("The feedback network runs without allocating on the real-time thread", "[plugins][fdn][rt]")
{
    Runner runner(2.0f);
    runner.settings.pitchRatios.fill(2.0f);
    runner.frame.shimmering = true;
    runner.frame.shimmer = 0.5f;
    runner.frame.keep = 0.9f;
    runner.frame.shift = 0.4f;
    const auto previous = rt::setViolationHandler(&countFdnViolation);
    fdnViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        runner.spin(0.2, 0.5f);
        runner.network.clear();
        runner.spin(0.05, 0.5f);
    }
    rt::setViolationHandler(previous);
    CHECK(fdnViolations == 0);
}
#endif
