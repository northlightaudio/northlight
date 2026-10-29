#include "engine/WorkerPool.h"

#include "engine/rt/Denormals.h"
#include "engine/rt/RealtimeGuard.h"
#include "engine/rt/ThreadPriority.h"

#include <algorithm>

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#define NORTHLIGHT_PAUSE() _mm_pause()
#else
#define NORTHLIGHT_PAUSE() ((void)0)
#endif

namespace northlight::engine
{

namespace
{
constexpr int spinsBeforeYield = 2000; // roughly 20-50 us of pausing before yielding the core
} // namespace

int WorkerPool::recommendedWorkerCount() noexcept
{
    const auto hardware = static_cast<int>(std::thread::hardware_concurrency());
    return std::clamp(hardware - 2, 0, 15);
}

WorkerPool::WorkerPool(int numWorkers)
{
    numWorkers = std::max(0, numWorkers);
    threads_.reserve(static_cast<size_t>(numWorkers));
    for (int i = 0; i < numWorkers; ++i)
        threads_.emplace_back([this] { workerLoop(); });
}

WorkerPool::~WorkerPool()
{
    stop_.store(true, std::memory_order_release);
    wake_.release(static_cast<std::ptrdiff_t>(threads_.size()) + 1);
    for (auto& t : threads_)
        if (t.joinable())
            t.join();
}

void WorkerPool::takeJobs() noexcept
{
    for (;;)
    {
        // Count and index come from the same load, so an index can only be
        // compared with the count of the batch it was taken from.
        const auto state = state_.fetch_add(1, std::memory_order_acq_rel);
        const auto count = static_cast<int>(state >> 32);
        const auto index = static_cast<int>(state & indexMask);
        if (index >= count)
            return;
        job_(context_, index);
        remaining_.fetch_sub(1, std::memory_order_acq_rel);
    }
}

void WorkerPool::workerLoop()
{
    const rt::ScopedProAudioThread priority;
    if (priority.joinedMmcss())
        mmcssWorkers_.fetch_add(1, std::memory_order_relaxed);
    while (true)
    {
        wake_.acquire();
        permits_.fetch_sub(1, std::memory_order_acq_rel);
        if (stop_.load(std::memory_order_acquire))
            return;
        inBatch_.fetch_add(1, std::memory_order_acq_rel);
        {
            rt::ScopedRealtimeSection realtime;
            rt::ScopedFlushDenormals denormals;
            takeJobs();
        }
        inBatch_.fetch_sub(1, std::memory_order_acq_rel);
    }
}

void WorkerPool::run(int count, Job job, void* context) noexcept
{
    if (count <= 0)
        return;

    // Everything a worker reads is written before the release store that opens
    // the batch.
    job_ = job;
    context_ = context;
    remaining_.store(count, std::memory_order_relaxed);
    state_.store(pack(count, 0), std::memory_order_release);

    if (!threads_.empty())
    {
        // Wake at most one worker per job and never more than are not already on
        // their way: a permit a worker has not consumed yet still counts.
        const int wanted = std::min(count, numWorkers());
        int outstanding = permits_.load(std::memory_order_acquire);
        int toRelease = wanted - outstanding;
        while (toRelease > 0 && !permits_.compare_exchange_weak(
                                    outstanding, outstanding + toRelease, std::memory_order_acq_rel))
            toRelease = wanted - outstanding;
        if (toRelease > 0)
            wake_.release(toRelease);
    }

    takeJobs();

    // Wait for the jobs other threads picked up, then for those threads to leave the
    // batch so they can never observe the next batch's state through this one's
    // exhausted index. Bounded by one track's render on a time-critical thread; a
    // descheduled worker is given its core back by yielding after a short spin.
    int spins = 0;
    while (remaining_.load(std::memory_order_acquire) > 0 || inBatch_.load(std::memory_order_acquire) > 0)
    {
        if (++spins < spinsBeforeYield)
        {
            NORTHLIGHT_PAUSE();
        }
        else
        {
            spins = 0;
            std::this_thread::yield();
        }
    }
    // Close the batch so a worker arriving late sees it exhausted.
    state_.store(pack(0, 0), std::memory_order_release);
}

} // namespace northlight::engine
