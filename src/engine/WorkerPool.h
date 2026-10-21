#pragma once

#include <atomic>
#include <cstdint>
#include <semaphore>
#include <thread>
#include <vector>

// A pool of real-time worker threads for the render graph. The audio callback
// thread hands out a batch of independent jobs (one per track, Phase 2) through a
// single atomic that packs the batch's job count with the next index, so a worker
// that wakes late for an earlier batch can never run a job against a later batch's
// state. The callback thread wakes the workers, takes jobs itself, and waits until
// every job has finished and every worker has left the batch.
//
// No locks and no allocation on the hot path: the only system call is the
// semaphore release that wakes workers, which never blocks the caller. Outstanding
// wake permits are counted so the semaphore can never overflow however many
// batches a block issues. Workers block on the semaphore between batches (outside
// any real-time section) and run each job inside one with denormals flushed.

namespace northlight::engine
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

    // Recommended worker count for this machine: the hardware threads minus two
    // (one for the device callback thread, one for the message thread and the
    // rest of the system) so a worker is never waiting behind the callback.
    static int recommendedWorkerCount() noexcept;

    // How many times run() had to yield while waiting for workers; diagnostics.
    std::uint64_t waitYields() const noexcept { return waitYields_.load(std::memory_order_relaxed); }

private:
    void workerLoop();
    void takeJobs() noexcept;

    static constexpr std::uint64_t indexMask = 0xffffffffull;
    static std::uint64_t pack(int count, int next) noexcept
    {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(count)) << 32) |
               static_cast<std::uint32_t>(next);
    }

    std::vector<std::thread> threads_;
    std::counting_semaphore<4096> wake_{0};
    std::atomic<bool> stop_{false};

    // Batch state: job_/context_ are written before the release store of state_
    // and read after the acquire fetch_add on it.
    std::atomic<std::uint64_t> state_{0}; // (count << 32) | next index
    std::atomic<int> remaining_{0};       // jobs not yet finished
    std::atomic<int> inBatch_{0};         // workers currently inside takeJobs
    std::atomic<int> permits_{0};         // wake permits released but not yet consumed
    std::atomic<std::uint64_t> waitYields_{0};
    Job job_ = nullptr;
    void* context_ = nullptr;
};

} // namespace northlight::engine
