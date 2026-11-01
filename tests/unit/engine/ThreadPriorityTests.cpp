#include "engine/WorkerPool.h"
#include "engine/rt/ThreadPriority.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using northlight::engine::WorkerPool;
using northlight::rt::ScopedProAudioThread;

namespace
{
// Whether a fresh thread on this machine can join MMCSS (a service a stripped-down
// Windows install may not run).
bool machineOffersMmcss()
{
    bool joined = false;
    std::thread probe([&] { joined = ScopedProAudioThread().isRealtime(); });
    probe.join();
    return joined;
}
} // namespace

TEST_CASE("A pro-audio thread joins MMCSS or falls back to time-critical, and reverts afterwards",
          "[engine][rt][priority]")
{
    int inside = 0, after = 0;
    bool joined = false;
    std::thread thread(
        [&]
        {
            {
                const ScopedProAudioThread priority;
                joined = priority.isRealtime();
                inside = GetThreadPriority(GetCurrentThread());
            }
            after = GetThreadPriority(GetCurrentThread());
        });
    thread.join();

    INFO("joined MMCSS: " << joined << ", priority inside: " << inside << ", after: " << after);
    // MMCSS sets an absolute priority, which GetThreadPriority reports relative to
    // the process's class: 15 at Normal, higher when the tests run below it
    // (build.ps1 -LowPriority).
    CHECK(inside >= THREAD_PRIORITY_TIME_CRITICAL);
    if (joined)
        CHECK(after == THREAD_PRIORITY_NORMAL);
}

TEST_CASE("The disk thread runs above normal work and below the render workers", "[engine][rt][priority]")
{
    int priority = 0;
    std::thread thread(
        [&]
        {
            northlight::rt::raiseDiskThreadPriority();
            priority = GetThreadPriority(GetCurrentThread());
        });
    thread.join();
    CHECK(priority == THREAD_PRIORITY_HIGHEST);
}

TEST_CASE("Every render worker joins MMCSS when the machine offers it", "[engine][pool][priority]")
{
    const int expected = machineOffersMmcss() ? 3 : 0;
    WorkerPool pool(3);
    // Workers join as they start, which is not synchronised with construction.
    for (int i = 0; i < 2000 && pool.realtimeWorkers() != expected; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(pool.realtimeWorkers() == expected);
}

#elif defined(__linux__)
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

using northlight::engine::WorkerPool;
using northlight::rt::ScopedProAudioThread;

namespace
{
// Whether a fresh thread here gets real-time scheduling: the user's limits allow
// it, or rtkit grants it. Neither holds on most build machines.
bool machineOffersRealtime()
{
    bool realtime = false;
    std::thread probe([&] { realtime = ScopedProAudioThread().isRealtime(); });
    probe.join();
    return realtime;
}

// The calling thread's scheduling policy, without SCHED_RESET_ON_FORK (which rtkit
// adds and Linux reports as part of the policy). Read with sched_*, not pthread_*,
// whose values glibc caches.
int currentPolicy(int& priority)
{
    sched_param param{};
    sched_getparam(0, &param);
    priority = param.sched_priority;
    return sched_getscheduler(0) & ~SCHED_RESET_ON_FORK;
}
} // namespace

TEST_CASE("A pro-audio thread runs real-time when the system allows it, and reverts afterwards",
          "[engine][rt][priority]")
{
    int insidePolicy = -1, insidePriority = -1, afterPolicy = -1, afterPriority = -1;
    bool realtime = false;
    std::thread thread(
        [&]
        {
            {
                const ScopedProAudioThread priority;
                realtime = priority.isRealtime();
                insidePolicy = currentPolicy(insidePriority);
            }
            afterPolicy = currentPolicy(afterPriority);
        });
    thread.join();

    INFO("real-time: " << realtime << ", priority inside: " << insidePriority);
    if (realtime)
    {
        // SCHED_FIFO when set directly; rtkit grants SCHED_RR.
        CHECK((insidePolicy == SCHED_FIFO || insidePolicy == SCHED_RR));
        CHECK(insidePriority >= 1);
        CHECK(insidePriority <= northlight::rt::realtimePriority);
    }
    else
    {
        CHECK(insidePolicy == SCHED_OTHER); // a refusal leaves the thread as it was
    }
    CHECK(afterPolicy == SCHED_OTHER);
    CHECK(afterPriority == 0);
}

TEST_CASE("The disk thread's nice level is raised when the system allows it, and never lowered",
          "[engine][rt][priority]")
{
    int nice = 1;
    std::thread thread(
        [&]
        {
            northlight::rt::raiseDiskThreadPriority();
            nice = getpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)));
        });
    thread.join();
    // The process's own level when nothing allowed the change; diskThreadNice (or
    // rtkit's floor above it) when something did.
    const int own = getpriority(PRIO_PROCESS, 0);
    INFO("nice: " << nice << ", process: " << own);
    CHECK(nice <= own);
    CHECK(nice >= northlight::rt::diskThreadNice);
}

TEST_CASE("Every render worker runs real-time when the system allows it", "[engine][pool][priority]")
{
    const int expected = machineOffersRealtime() ? 3 : 0;
    WorkerPool pool(3);
    for (int i = 0; i < 2000 && pool.realtimeWorkers() != expected; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(pool.realtimeWorkers() == expected);
}
#endif
