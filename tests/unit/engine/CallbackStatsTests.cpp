#include "engine/rt/CallbackStats.h"
#include "engine/rt/RealtimeGuard.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>

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
            stats.record(static_cast<double>(i) / 500.0);
    }
    northlight::rt::setViolationHandler(previous);
    CHECK(statsViolations == 0);
}
#endif
