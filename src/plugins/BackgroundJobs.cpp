#include "plugins/BackgroundJobs.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace northlight::plugins::background
{

namespace
{
struct Worker
{
    std::mutex mutex; // held while the jobs run, so remove() waits for a poll to finish
    std::condition_variable wake;
    std::vector<Job*> jobs;
    std::thread thread;
    // A thread runs while the generation it started in lasts; stopping moves it
    // on, so a thread being joined never mistakes a newer start for its own.
    int generation = 0;

    void run(int mine)
    {
        std::unique_lock lock(mutex);
        while (generation == mine)
        {
            wake.wait_for(lock, std::chrono::milliseconds(15), [&] { return generation != mine; });
            if (generation != mine)
                break;
            for (auto* job : jobs)
                job->poll();
        }
    }
};

// Never destroyed: a processor may outlive static destruction at exit.
Worker& worker()
{
    static auto* w = new Worker;
    return *w;
}
} // namespace

void add(Job* job)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("background::add");
    auto& w = worker();
    std::unique_lock lock(w.mutex);
    if (std::find(w.jobs.begin(), w.jobs.end(), job) != w.jobs.end())
        return;
    w.jobs.push_back(job);
    if (!w.thread.joinable())
        w.thread = std::thread([&w, mine = w.generation] { w.run(mine); });
}

void remove(Job* job)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("background::remove");
    auto& w = worker();
    std::thread finished;
    {
        std::unique_lock lock(w.mutex);
        std::erase(w.jobs, job);
        if (w.jobs.empty() && w.thread.joinable())
        {
            ++w.generation;
            finished = std::move(w.thread);
        }
    }
    if (finished.joinable())
    {
        w.wake.notify_all();
        finished.join();
    }
}

} // namespace northlight::plugins::background
