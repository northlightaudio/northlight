#include "engine/rt/CallbackStats.h"
#include "engine/rt/RealtimeGuard.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>

using Catch::Approx;
using northlight::rt::CallbackStats;

TEST_CASE("Callback stats report percentiles, max and overruns", "[rt][stats]")
{
    CallbackStats stats;
    CHECK(stats.summary().callbacks == 0);

    for (int i = 0; i < 98; ++i)
        stats.record(0.25);
    stats.record(0.9);
    stats.record(1.5); // one overrun

    const auto s = stats.summary();
    CHECK(s.callbacks == 100);
    CHECK(s.overruns == 1);
    CHECK(s.max == Approx(1.5));
    CHECK(s.p50 == Approx(0.25).margin(1.0 / 64.0));
    CHECK(s.p99 == Approx(1.5).margin(1.0 / 64.0));

    stats.reset();
    CHECK(stats.summary().callbacks == 0);
    CHECK(stats.summary().max == 0.0);
}

TEST_CASE("Loads beyond twice the deadline saturate rather than overflow", "[rt][stats]")
{
    CallbackStats stats;
    stats.record(50.0);
    stats.record(-1.0);
    const auto s = stats.summary();
    CHECK(s.callbacks == 2);
    CHECK(s.overruns == 1);
    CHECK(s.max == Approx(50.0));
}

TEST_CASE("Callbacks that arrive on time are not late", "[rt][stats]")
{
    CallbackStats stats;
    const double block = 0.01;
    for (int i = 0; i < 1000; ++i)
        CHECK(stats.recordStart(i * block, block) == Approx(0.0).margin(1e-6));
    CHECK(stats.summary().late == 0);
    CHECK(stats.summary().maxLateness == Approx(0.0).margin(1e-6));
}

TEST_CASE("A callback that starts half a block late or more counts once", "[rt][stats]")
{
    CallbackStats stats;
    const double block = 0.01;
    double t = 0.0;
    for (int i = 0; i < 10; ++i, t += block)
        stats.recordStart(t, block);
    // A 15 ms stall: the next callback starts 1.5 blocks late...
    t += 0.015;
    CHECK(stats.recordStart(t, block) == Approx(1.5));
    // ...and the ones after it, on time from there, are not late again.
    for (int i = 0; i < 10; ++i)
    {
        t += block;
        CHECK(stats.recordStart(t, block) == Approx(0.0).margin(1e-9));
    }
    const auto s = stats.summary();
    CHECK(s.late == 1);
    CHECK(s.maxLateness == Approx(1.5));
}

TEST_CASE("Small blocks on a USB driver's 1 ms frames are not late", "[rt][stats]")
{
    // 64 samples at 48 kHz (1.33 ms) handed out on a 1 ms grid: each callback starts
    // on the first millisecond after it is due, up to half a block behind.
    CallbackStats stats;
    const double block = 64.0 / 48000.0;
    for (int i = 0; i < 48000; ++i)
        stats.recordStart(std::ceil(i * block * 1000.0) / 1000.0, block);
    CHECK(stats.summary().late == 0);
    CHECK(stats.summary().maxLateness > 0.4);
}

TEST_CASE("Jitter under half a block is not late but sets the max", "[rt][stats]")
{
    CallbackStats stats;
    const double block = 0.01;
    stats.recordStart(0.0, block);
    CHECK(stats.recordStart(0.0130, block) == Approx(0.3));
    const auto s = stats.summary();
    CHECK(s.late == 0);
    CHECK(s.maxLateness == Approx(0.3));
}

TEST_CASE("A period split into shorter blocks is not late", "[rt][stats]")
{
    // The device delivers 10 ms periods; each is processed as 7 ms then 3 ms back to
    // back, so the first block of each period arrives 7 ms after the 3 ms block.
    CallbackStats stats;
    for (int period = 0; period < 100; ++period)
    {
        const double t = period * 0.010;
        CHECK(stats.recordStart(t, 0.007) == Approx(0.0).margin(1e-9));
        stats.recordStart(t + 0.0001, 0.003);
    }
    CHECK(stats.summary().late == 0);
}

TEST_CASE("Clock drift between device and CPU never adds up to a late callback", "[rt][stats]")
{
    // The device runs 0.1% slow against the CPU clock for an hour of 10 ms blocks.
    CallbackStats stats;
    const double block = 0.01;
    for (int i = 0; i < 360000; ++i)
        stats.recordStart(i * block * 1.001, block);
    CHECK(stats.summary().late == 0);
    CHECK(stats.summary().maxLateness < 0.2);
}

TEST_CASE("Reset forgets the due time", "[rt][stats]")
{
    CallbackStats stats;
    stats.recordStart(0.0, 0.01);
    stats.recordStart(0.05, 0.01);
    CHECK(stats.summary().late == 1);
    stats.reset();
    CHECK(stats.summary().late == 0);
    CHECK(stats.recordStart(100.0, 0.01) == 0.0); // the first start after a reset is never late
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> statsViolations{0};
void countStatsViolation(const northlight::rt::Violation&) noexcept
{
    statsViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Recording stats does not allocate on a real-time thread", "[rt][stats]")
{
    CallbackStats stats;
    const auto previous = northlight::rt::setViolationHandler(&countStatsViolation);
    statsViolations = 0;
    {
        northlight::rt::ScopedRealtimeSection section;
        for (int i = 0; i < 1000; ++i)
        {
            stats.recordStart(static_cast<double>(i) * 0.01, 0.01);
            stats.record(static_cast<double>(i) / 500.0);
        }
    }
    northlight::rt::setViolationHandler(previous);
    CHECK(statsViolations == 0);
}
#endif
