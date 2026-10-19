#pragma once

#include <atomic>
#include <cstdint>
#include <semaphore>
#include <thread>
#include <vector>

// A pool of real-time worker threads for the render graph. The audio callback
// thread hands out a batch of independent jobs (one per track, Phase 2) through an
// atomic counter; it wakes the workers, takes jobs itself, and spins until every job
// has finished. No locks and no allocation on the hot path: the only system calls
// are the semaphore release that wakes workers, which never blocks the caller.
//
// Workers block on the semaphore between batches (outside any real-time section)
// and run each job inside one with denormals flushed.

namespace aural::engine
{

class WorkerPool
{
public:
    using Job = void (*)(void* context, int index) noexcept;

    // numWorkers = 0 runs every batch on the calling thread.
    explicit WorkerPool(int numWorkers);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    int numWorkers() const noexcept { return static_cast<int>(threads_.size()); }

    // Real-time. Runs job(context, i) for every i in [0, count) and returns when all
    // have completed.
    void run(int count, Job job, void* context) noexcept;

    // Recommended worker count for this machine: one per hardware thread minus the
    // callback thread, capped.
    static int recommendedWorkerCount() noexcept;

private:
    void workerLoop();
    void takeJobs() noexcept;

    std::vector<std::thread> threads_;
    std::counting_semaphore<1024> wake_{0};
    std::atomic<bool> stop_{false};

    // Batch state, written by the caller before waking workers.
    std::atomic<int> next_{0};
    std::atomic<int> remaining_{0};
    int count_ = 0;
    Job job_ = nullptr;
    void* context_ = nullptr;
};

} // namespace aural::engine
