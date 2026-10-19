#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// Publishes immutable snapshots from a controller thread to one real-time thread.
//
// The controller calls publish() with a new snapshot; the real-time thread calls
// acquire() once per block and uses the result for that block only. Old snapshots
// are never freed by the real-time thread: each snapshot carries an epoch, the
// real-time thread advertises the epoch it is using, and the controller's collect()
// deletes retired snapshots once the real-time thread has moved past them (or when
// told that no real-time thread is running at all).
//
// T must have a `std::uint64_t epoch` member that the publisher may set.

namespace aural::rt
{

template <typename T> class SnapshotPublisher
{
public:
    SnapshotPublisher() = default;
    ~SnapshotPublisher()
    {
        delete current_.load(std::memory_order_acquire);
        for (auto& r : retired_)
            delete r.snapshot;
    }

    SnapshotPublisher(const SnapshotPublisher&) = delete;
    SnapshotPublisher& operator=(const SnapshotPublisher&) = delete;

    // Controller thread. Takes ownership; the previous snapshot is retired.
    void publish(std::unique_ptr<T> snapshot)
    {
        snapshot->epoch = ++epoch_;
        T* previous = current_.exchange(snapshot.release(), std::memory_order_acq_rel);
        if (previous != nullptr)
            retired_.push_back({previous, previous->epoch});
    }

    // Controller thread. Frees retired snapshots the real-time thread can no longer
    // be using. `realtimeRunning` false means no real-time thread exists right now,
    // so everything retired can go.
    void collect(bool realtimeRunning)
    {
        const auto rtEpoch = realtimeEpoch_.load(std::memory_order_acquire);
        std::erase_if(retired_,
                      [&](const Retired& r)
                      {
                          if (realtimeRunning && r.epoch >= rtEpoch)
                              return false;
                          delete r.snapshot;
                          return true;
                      });
    }

    std::size_t retiredCount() const noexcept { return retired_.size(); }

    // Real-time thread. Valid until the next call to acquire() on that thread.
    const T* acquire() noexcept
    {
        const T* s = current_.load(std::memory_order_acquire);
        realtimeEpoch_.store(s != nullptr ? s->epoch : 0, std::memory_order_release);
        return s;
    }

    // Controller thread; for inspection only.
    const T* currentForController() const noexcept { return current_.load(std::memory_order_acquire); }

private:
    struct Retired
    {
        T* snapshot;
        std::uint64_t epoch;
    };

    std::atomic<T*> current_{nullptr};
    std::atomic<std::uint64_t> realtimeEpoch_{0};
    std::uint64_t epoch_ = 0;
    std::vector<Retired> retired_;
};

} // namespace aural::rt
