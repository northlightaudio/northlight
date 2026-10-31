#include "engine/BackgroundQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace northlight::engine;

namespace
{
// A job that runs until it is let go, so a test can hold the queue busy.
struct Gate
{
    std::promise<void> started;
    std::promise<void> open;
    std::shared_future<void> opened = open.get_future().share();
    std::function<void()> job()
    {
        return [this]
        {
            started.set_value();
            opened.wait();
        };
    }
};
} // namespace

TEST_CASE("A background queue runs its jobs one at a time, in the order they were added", "[engine][queue]")
{
    std::mutex mutex;
    std::vector<int> order;
    std::atomic<int> running{0};
    std::atomic<bool> overlapped{false};
    {
        BackgroundQueue queue;
        for (int i = 0; i < 20; ++i)
            queue.add(
                [&, i]
                {
                    if (running.fetch_add(1) != 0)
                        overlapped = true;
                    std::this_thread::sleep_for(std::chrono::microseconds(200));
                    {
                        const std::lock_guard<std::mutex> lock(mutex);
                        order.push_back(i);
                    }
                    running.fetch_sub(1);
                });
        REQUIRE(queue.waitUntilIdle(-1));
    }
    REQUIRE(order.size() == 20);
    for (int i = 0; i < 20; ++i)
        CHECK(order[static_cast<size_t>(i)] == i);
    CHECK_FALSE(overlapped);
}

TEST_CASE("A background queue's destructor waits for the running job and drops the rest", "[engine][queue]")
{
    Gate gate;
    std::atomic<bool> finished{false};
    std::atomic<bool> laterRan{false};
    auto queue = std::make_unique<BackgroundQueue>();
    queue->add(
        [&]
        {
            gate.started.set_value();
            gate.opened.wait();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            finished = true;
        });
    queue->add([&] { laterRan = true; });
    gate.started.get_future().wait();

    // Destroyed on another thread, so this one can let the running job go.
    auto destroyed = std::async(std::launch::async, [&] { queue.reset(); });
    CHECK(destroyed.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
    gate.open.set_value();
    destroyed.get();
    CHECK(finished); // waited for, not abandoned
    CHECK_FALSE(laterRan);
}

TEST_CASE("Dropping a background queue's pending jobs leaves the running one to finish", "[engine][queue]")
{
    Gate gate;
    std::atomic<int> ran{0};
    BackgroundQueue queue;
    queue.add(gate.job());
    gate.started.get_future().wait();
    queue.add([&] { ++ran; });
    queue.add([&] { ++ran; });
    queue.dropPending();
    gate.open.set_value();
    REQUIRE(queue.waitUntilIdle(-1));
    CHECK(ran == 0);

    queue.add([&] { ++ran; }); // still takes new work
    REQUIRE(queue.waitUntilIdle(-1));
    CHECK(ran == 1);
}

TEST_CASE("Waiting for a background queue to be idle gives up at its time limit", "[engine][queue]")
{
    Gate gate;
    BackgroundQueue queue;
    CHECK(queue.waitUntilIdle(0)); // nothing to do
    queue.add(gate.job());
    gate.started.get_future().wait();
    CHECK_FALSE(queue.waitUntilIdle(20));
    gate.open.set_value();
    CHECK(queue.waitUntilIdle(-1));
}
