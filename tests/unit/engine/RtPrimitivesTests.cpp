#include "engine/rt/RealtimeGuard.h"
#include "engine/rt/SnapshotPublisher.h"
#include "engine/rt/SpscQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

using namespace northlight::rt;

TEST_CASE("SPSC queue holds capacity items and preserves order", "[rt][queue]")
{
    SpscQueue<int> q(3);
    CHECK(q.capacity() == 3);
    CHECK(q.empty());
    CHECK(q.push(1));
    CHECK(q.push(2));
    CHECK(q.push(3));
    CHECK_FALSE(q.push(4)); // full
    CHECK_FALSE(q.empty());

    int v = 0;
    CHECK(q.pop(v));
    CHECK(v == 1);
    CHECK(q.push(4)); // space again
    CHECK(q.pop(v));
    CHECK(v == 2);
    CHECK(q.pop(v));
    CHECK(v == 3);
    CHECK(q.pop(v));
    CHECK(v == 4);
    CHECK_FALSE(q.pop(v));
    CHECK(q.empty());
}

TEST_CASE("SPSC queue delivers every item across threads", "[rt][queue]")
{
    SpscQueue<std::uint32_t> q(64);
    constexpr std::uint32_t count = 100000;
    std::atomic<bool> done{false};

    std::thread producer(
        [&]
        {
            for (std::uint32_t i = 0; i < count;)
                if (q.push(i))
                    ++i;
            done = true;
        });

    std::uint32_t expected = 0;
    bool ordered = true;
    while (expected < count)
    {
        std::uint32_t v;
        if (q.pop(v))
        {
            ordered = ordered && v == expected;
            ++expected;
        }
    }
    producer.join();
    CHECK(ordered);
    CHECK(expected == count);
}

namespace
{
struct Snap
{
    std::uint64_t epoch = 0;
    int value = 0;
    static inline std::atomic<int> alive{0};
    explicit Snap(int v) : value(v) { ++alive; }
    ~Snap() { --alive; }
};
} // namespace

TEST_CASE("Snapshot publisher retires old snapshots only after the real-time side moves on", "[rt][snapshot]")
{
    {
        SnapshotPublisher<Snap> pub;
        CHECK(pub.acquire() == nullptr);

        pub.publish(std::make_unique<Snap>(1));
        const Snap* s1 = pub.acquire();
        REQUIRE(s1 != nullptr);
        CHECK(s1->value == 1);
        CHECK(s1->epoch == 1);

        pub.publish(std::make_unique<Snap>(2));
        pub.collect(true);
        CHECK(pub.retiredCount() == 1); // RT still advertises epoch 1
        CHECK(Snap::alive == 2);

        const Snap* s2 = pub.acquire();
        CHECK(s2->value == 2);
        pub.collect(true);
        CHECK(pub.retiredCount() == 0);
        CHECK(Snap::alive == 1);

        // Two publishes before the RT side runs again: both retired, neither freed yet.
        pub.publish(std::make_unique<Snap>(3));
        pub.publish(std::make_unique<Snap>(4));
        pub.collect(true);
        CHECK(pub.retiredCount() == 2);
        CHECK(pub.acquire()->value == 4);
        pub.collect(true);
        CHECK(pub.retiredCount() == 0);
        CHECK(Snap::alive == 1);

        // With no real-time thread running everything retired can go at once.
        pub.publish(std::make_unique<Snap>(5));
        pub.publish(std::make_unique<Snap>(6));
        pub.collect(false);
        CHECK(pub.retiredCount() == 0);
        CHECK(Snap::alive == 1);
    }
    CHECK(Snap::alive == 0); // destructor frees the current one
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> rtViolations{0};
void countRtViolation(const Violation&) noexcept
{
    rtViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Queue and publisher real-time operations do not allocate", "[rt][queue][snapshot]")
{
    const auto previous = setViolationHandler(&countRtViolation);
    rtViolations = 0;

    SpscQueue<int> q(16);
    SnapshotPublisher<Snap> pub;
    pub.publish(std::make_unique<Snap>(1));
    {
        ScopedRealtimeSection section;
        int v;
        q.push(1);
        q.pop(v);
        (void)pub.acquire();
    }

    setViolationHandler(previous);
    CHECK(rtViolations == 0);
}
#endif
