#pragma once

#include "engine/StreamingAudioSource.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// The one disk I/O thread. Services every registered StreamingAudioSource in turn,
// sleeping briefly when none needs work. Registration happens from the controller
// thread; the real-time thread never touches this object.

namespace aural::engine
{

class DiskThread
{
public:
    explicit DiskThread(int idleMilliseconds = 5);
    ~DiskThread();

    DiskThread(const DiskThread&) = delete;
    DiskThread& operator=(const DiskThread&) = delete;

    // Sources are held weakly; a source that has been destroyed is dropped.
    void add(const std::shared_ptr<StreamingAudioSource>& source);

    // Wakes the thread now (after a locate, for instance).
    void poke();

    std::uint64_t totalUnderruns() const;

private:
    void run();

    std::vector<std::weak_ptr<StreamingAudioSource>> sources_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stop_{false};
    int idleMilliseconds_;
    std::thread thread_;
};

} // namespace aural::engine
