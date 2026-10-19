#include "engine/WorkerPool.h"

#include "engine/rt/Denormals.h"
#include "engine/rt/RealtimeGuard.h"

#include <algorithm>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(_M_X64) || defined(__x86_64__)
#include <immintrin.h>
#define AURAL_PAUSE() _mm_pause()
#else
#define AURAL_PAUSE() ((void)0)
#endif

namespace aural::engine
{

namespace
{
void raiseThreadPriority() noexcept
{
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}
} // namespace

int WorkerPool::recommendedWorkerCount() noexcept
{
    const auto hardware = static_cast<int>(std::thread::hardware_concurrency());
    return std::clamp(hardware - 1, 0, 15);
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
        const int index = next_.fetch_add(1, std::memory_order_acq_rel);
        if (index >= count_)
            return;
        job_(context_, index);
        remaining_.fetch_sub(1, std::memory_order_acq_rel);
    }
}

void WorkerPool::workerLoop()
{
    raiseThreadPriority();
    while (true)
    {
        wake_.acquire();
        if (stop_.load(std::memory_order_acquire))
            return;
        {
            rt::ScopedRealtimeSection realtime;
            rt::ScopedFlushDenormals denormals;
            takeJobs();
        }
    }
}

void WorkerPool::run(int count, Job job, void* context) noexcept
{
    if (count <= 0)
        return;

    count_ = count;
    job_ = job;
    context_ = context;
    remaining_.store(count, std::memory_order_relaxed);
    next_.store(0, std::memory_order_release);

    if (!threads_.empty())
        wake_.release(static_cast<std::ptrdiff_t>(std::min<int>(count, numWorkers())));

    takeJobs();

    // Wait for the jobs other threads picked up. They are bounded by one track's
    // render, on time-critical threads, so this spin is short.
    while (remaining_.load(std::memory_order_acquire) > 0)
        AURAL_PAUSE();
}

} // namespace aural::engine
