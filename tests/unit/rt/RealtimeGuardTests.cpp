#include "engine/rt/RealtimeGuard.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <vector>

// Note: Catch2's CHECK/REQUIRE allocate, so assertions are never made while a
// ScopedRealtimeSection is alive. Observations are captured into locals inside the
// section and checked after it ends.

using namespace northlight::rt;

namespace
{
std::atomic<int> violationCount{0};
std::atomic<ViolationKind> lastKind{ViolationKind::explicitAssert};

void countingHandler(const Violation& v) noexcept
{
    violationCount.fetch_add(1, std::memory_order_relaxed);
    lastKind.store(v.kind, std::memory_order_relaxed);
}

struct HandlerScope
{
    ViolationHandler previous;
    HandlerScope() : previous(setViolationHandler(&countingHandler)) { violationCount = 0; }
    ~HandlerScope() { setViolationHandler(previous); }
};
} // namespace

TEST_CASE("Threads are not real-time by default", "[rt]")
{
    CHECK_FALSE(isRealtimeThread());
}

TEST_CASE("ScopedRealtimeSection marks the thread and nests", "[rt]")
{
    bool inOuter = false, inInner = false, afterInner = false;
    {
        ScopedRealtimeSection outer;
        inOuter = isRealtimeThread();
        {
            ScopedRealtimeSection inner;
            inInner = isRealtimeThread();
        }
        afterInner = isRealtimeThread();
    }
    CHECK(inOuter);
    CHECK(inInner);
    CHECK(afterInner);
    CHECK_FALSE(isRealtimeThread());
}

TEST_CASE("ScopedRealtimeSuspend temporarily clears the flag", "[rt]")
{
    bool duringSuspend = true, afterSuspend = false;
    {
        ScopedRealtimeSection section;
        {
            ScopedRealtimeSuspend suspend;
            duringSuspend = isRealtimeThread();
        }
        afterSuspend = isRealtimeThread();
    }
    CHECK_FALSE(duringSuspend);
    CHECK(afterSuspend);
}

TEST_CASE("NORTHLIGHT_ASSERT_NOT_REALTIME reports only on real-time threads", "[rt]")
{
    HandlerScope scope;

    NORTHLIGHT_ASSERT_NOT_REALTIME("lock in test");
    CHECK(violationCount == 0);

    {
        ScopedRealtimeSection section;
        NORTHLIGHT_ASSERT_NOT_REALTIME("lock in test");
    }

#if defined(NORTHLIGHT_RT_CHECKS)
    CHECK(violationCount == 1);
    CHECK(lastKind == ViolationKind::explicitAssert);
#else
    CHECK(violationCount == 0);
#endif
}

#if defined(NORTHLIGHT_RT_CHECKS)

TEST_CASE("Allocation on a real-time thread is reported", "[rt]")
{
    HandlerScope scope;

    auto outside = std::make_unique<int>(1);
    CHECK(violationCount == 0);

    int afterAlloc = -1, afterFree = -1;
    ViolationKind kindAfterAlloc{}, kindAfterFree{};
    {
        ScopedRealtimeSection section;
        auto inside = std::make_unique<int>(2);
        afterAlloc = violationCount;
        kindAfterAlloc = lastKind;
        inside.reset();
        afterFree = violationCount;
        kindAfterFree = lastKind;
    }

    CHECK(afterAlloc == 1);
    CHECK(kindAfterAlloc == ViolationKind::allocation);
    CHECK(afterFree == 2);
    CHECK(kindAfterFree == ViolationKind::deallocation);

    outside.reset();
    CHECK(violationCount == 2);
}

TEST_CASE("Over-aligned allocation on a real-time thread is reported", "[rt]")
{
    HandlerScope scope;

    struct alignas(64) Wide
    {
        float samples[16];
    };

    int afterAlloc = -1, afterFree = -1;
    {
        ScopedRealtimeSection section;
        auto p = std::make_unique<Wide>();
        afterAlloc = violationCount;
        p.reset();
        afterFree = violationCount;
    }

    CHECK(afterAlloc == 1);
    CHECK(afterFree == 2);
}

TEST_CASE("Preallocated containers do not trigger violations", "[rt]")
{
    HandlerScope scope;

    std::vector<float> buffer;
    buffer.reserve(1024);

    {
        ScopedRealtimeSection section;
        for (int i = 0; i < 1024; ++i)
            buffer.push_back(static_cast<float>(i));
    }

    CHECK(violationCount == 0);
    CHECK(buffer.size() == 1024);
}

#endif // NORTHLIGHT_RT_CHECKS
