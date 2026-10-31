#include "engine/BackgroundQueue.h"

#include "engine/rt/RealtimeGuard.h"

#include <chrono>
#include <utility>

namespace northlight::engine
{

BackgroundQueue::BackgroundQueue() : thread_([this] { run(); }) {}

BackgroundQueue::~BackgroundQueue()
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        jobs_.clear();
        stopping_ = true;
    }
    changed_.notify_all();
    thread_.join();
}

void BackgroundQueue::add(std::function<void()> job)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("BackgroundQueue::add");
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back(std::move(job));
    }
    changed_.notify_all();
}

void BackgroundQueue::dropPending()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("BackgroundQueue::dropPending");
    const std::lock_guard<std::mutex> lock(mutex_);
    jobs_.clear();
    // Nothing to notify: a waiter is woken when the running job ends.
}

bool BackgroundQueue::waitUntilIdle(int timeoutMs)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("BackgroundQueue::waitUntilIdle");
    std::unique_lock<std::mutex> lock(mutex_);
    const auto idle = [this] { return jobs_.empty() && !running_; };
    if (timeoutMs < 0)
    {
        changed_.wait(lock, idle);
        return true;
    }
    return changed_.wait_for(lock, std::chrono::milliseconds(timeoutMs), idle);
}

void BackgroundQueue::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;)
    {
        changed_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
        if (stopping_)
            return;
        auto job = std::move(jobs_.front());
        jobs_.pop_front();
        running_ = true;
        lock.unlock();
        job();
        job = nullptr; // what it holds goes before it counts as done
        lock.lock();
        running_ = false;
        changed_.notify_all();
    }
}

} // namespace northlight::engine
