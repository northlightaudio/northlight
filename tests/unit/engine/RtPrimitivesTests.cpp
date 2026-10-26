#include "engine/rt/MpscQueue.h"
#include "engine/rt/PeakHistory.h"
#include "engine/rt/RealtimeGuard.h"
#include "engine/rt/SnapshotPublisher.h"
#include "engine/rt/SpscQueue.h"
#include "engine/rt/TelemetryRing.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

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

TEST_CASE("MPSC queue holds its capacity, preserves order and drops when full", "[rt][queue]")
{
    MpscQueue<int> q(3);
    CHECK(q.capacity() == 4); // rounded up to a power of two
    int v = 0;
    CHECK_FALSE(q.pop(v));
    for (int i = 0; i < 4; ++i)
        CHECK(q.push(i));
    CHECK_FALSE(q.push(4)); // full: dropped
    CHECK(q.pop(v));
    CHECK(v == 0);
    CHECK(q.push(5)); // space again
    for (const int expected : {1, 2, 3, 5})
    {
        REQUIRE(q.pop(v));
        CHECK(v == expected);
    }
    CHECK_FALSE(q.pop(v));
}

TEST_CASE("MPSC queue delivers every item from many producers, each in its order", "[rt][queue]")
{
    MpscQueue<std::uint32_t> q(64);
    constexpr std::uint32_t producers = 4;
    constexpr std::uint32_t each = 50000;
    std::vector<std::thread> threads;
    for (std::uint32_t p = 0; p < producers; ++p)
        threads.emplace_back(
            [&q, p]
            {
                for (std::uint32_t i = 0; i < each;)
                    if (q.push(p << 24 | i))
                        ++i;
            });
    std::vector<std::uint32_t> next(producers, 0);
    bool ordered = true;
    std::uint32_t received = 0;
    while (received < producers * each)
    {
        std::uint32_t v;
        if (!q.pop(v))
            continue;
        const auto p = v >> 24;
        ordered = ordered && p < producers && (v & 0xffffff) == next[p];
        if (p < producers)
            ++next[p];
        ++received;
    }
    for (auto& t : threads)
        t.join();
    CHECK(ordered);
    CHECK(received == producers * each);
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

TEST_CASE("A telemetry ring hands each reader the frames since its cursor, oldest first", "[rt][telemetry]")
{
    using Ring = TelemetryRing<3, 64>;
    Ring ring;
    std::uint64_t cursor = 0;
    std::array<Ring::Frame, 64> out{};
    CHECK(ring.read(cursor, out.data(), 64) == 0);

    for (int i = 0; i < 5; ++i)
        ring.publish({static_cast<float>(i), static_cast<float>(10 * i), -1.0f});
    CHECK(ring.framesWritten() == 5);
    REQUIRE(ring.read(cursor, out.data(), 64) == 5);
    CHECK(cursor == 5);
    CHECK(out[0][0] == 0.0f);
    CHECK(out[4][1] == 40.0f);
    CHECK(out[4][2] == -1.0f);
    CHECK(ring.read(cursor, out.data(), 64) == 0); // nothing new

    // A slow reader gets the newest it asked for, and never the slots by the head.
    for (int i = 5; i < 200; ++i)
        ring.publish({static_cast<float>(i), 0.0f, 0.0f});
    std::uint64_t late = 0;
    CHECK(ring.read(late, out.data(), 3) == 3);
    CHECK(out[0][0] == 197.0f);
    CHECK(out[2][0] == 199.0f);
    std::uint64_t behind = 0;
    CHECK(ring.read(behind, out.data(), 64) == 64 - 16);
    CHECK(out[0][0] == static_cast<float>(200 - 48));
    CHECK(behind == 200);
}

TEST_CASE("Peak history gives every reader the peak since its own last look", "[rt][peaks]")
{
    PeakHistory history;
    PeakHistory::Reader a, b;
    CHECK(a.take(history) == 0.0f);
    history.publish(0.5f);
    history.publish(0.25f);
    CHECK(a.take(history) == 0.5f);
    CHECK(a.take(history) == 0.0f); // nothing new for a
    history.publish(0.125f);
    CHECK(b.take(history) == 0.5f); // b still sees everything since it last looked
    CHECK(a.take(history) == 0.125f);

    // A reader far behind sees the last `length` blocks.
    PeakHistory::Reader late;
    history.publish(0.9f);
    for (std::size_t i = 0; i < PeakHistory::length; ++i)
        history.publish(0.1f);
    CHECK(late.take(history) == 0.1f);
}
