#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

// One background thread that runs jobs in the order they were added: an autosave
// write, a folder listing. Not for the audio callback or the render workers.
//
// It replaces juce::ThreadPool, whose destructor waits only 500 ms for a thread to
// stop and then kills it. A thread killed while it holds a lock leaves the lock
// held for ever; one killed while it starts or ends holds Windows' loader lock,
// and every later DLL load or unload in the process then waits for ever (quitting
// included). This queue never kills: it waits for the running job and joins the
// thread (docs/decisions.md).

namespace northlight::engine
{

class BackgroundQueue
{
public:
    BackgroundQueue();
    // Drops the jobs that have not started, waits for the running one and joins the
    // thread. A job that can take long should watch a flag its owner sets first.
    ~BackgroundQueue();

    BackgroundQueue(const BackgroundQueue&) = delete;
    BackgroundQueue& operator=(const BackgroundQueue&) = delete;

    void add(std::function<void()> job);
    // Drops the jobs that have not started; the running one carries on.
    void dropPending();
    // Waits until no job is queued or running, for at most `timeoutMs` (negative:
    // no limit). Returns whether it is idle.
    bool waitUntilIdle(int timeoutMs);

private:
    void run();

    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::function<void()>> jobs_;
    bool running_ = false; // a job is running
    bool stopping_ = false;
    std::thread thread_; // last: started once the rest is ready
};

} // namespace northlight::engine
