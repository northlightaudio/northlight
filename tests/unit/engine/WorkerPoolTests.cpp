#include "engine/WorkerPool.h"
#include "engine/rt/RealtimeGuard.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using northlight::engine::WorkerPool;

namespace
{
struct Batch
{
    std::vector<std::atomic<int>> hits;
    std::vector<std::atomic<std::size_t>> threadHash;
    explicit Batch(int n) : hits(static_cast<size_t>(n)), threadHash(static_cast<size_t>(n)) {}
};

void countJob(void* context, int index) noexcept
{
    auto& batch = *static_cast<Batch*>(context);
    batch.hits[static_cast<size_t>(index)].fetch_add(1);
    batch.threadHash[static_cast<size_t>(index)].store(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    // A little work so other threads get a chance to join in.
    volatile int sink = 0;
    for (int i = 0; i < 2000; ++i)
        sink = sink + i;
}
} // namespace

TEST_CASE("Every job in a batch runs exactly once, with or without workers", "[engine][pool]")
{
    for (int workers : {0, 1, 3})
    {
        WorkerPool pool(workers);
        CHECK(pool.numWorkers() == workers);
        for (int round = 0; round < 20; ++round)
        {
            Batch batch(37);
            pool.run(37, &countJob, &batch);
            for (const auto& h : batch.hits)
                REQUIRE(h.load() == 1);
        }
        Batch empty(1);
        pool.run(0, &countJob, &empty);
        CHECK(empty.hits[0].load() == 0);
    }
}

namespace
{
// A batch whose jobs on the calling thread wait (up to a deadline) until a worker
// has taken one, so the test does not depend on how fast workers wake: on a busy
// two-core build machine the caller could otherwise finish every short job first.
struct SpreadBatch
{
    std::thread::id caller = std::this_thread::get_id();
    std::atomic<bool> workerRan{false};
};

void spreadJob(void* context, int) noexcept
{
    auto& batch = *static_cast<SpreadBatch*>(context);
    if (std::this_thread::get_id() != batch.caller)
    {
        batch.workerRan.store(true);
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!batch.workerRan.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
}
} // namespace

TEST_CASE("With workers, jobs are spread over more than one thread", "[engine][pool]")
{
    WorkerPool pool(3);
    SpreadBatch batch;
    pool.run(64, &spreadJob, &batch);
    CHECK(batch.workerRan.load());
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> poolViolations{0};
void countPoolViolation(const northlight::rt::Violation&) noexcept
{
    poolViolations.fetch_add(1, std::memory_order_relaxed);
}
void rtJob(void*, int) noexcept
{
    // Workers must be inside a real-time section while running jobs.
    if (!northlight::rt::isRealtimeThread())
        poolViolations.fetch_add(1000, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Running a batch does not allocate on the calling or worker threads", "[engine][pool][rt]")
{
    WorkerPool pool(2);
    const auto previous = northlight::rt::setViolationHandler(&countPoolViolation);
    poolViolations = 0;
    {
        northlight::rt::ScopedRealtimeSection section;
        for (int round = 0; round < 100; ++round)
            pool.run(16, &rtJob, nullptr);
    }
    northlight::rt::setViolationHandler(previous);
    CHECK(poolViolations == 0);
}
#endif
