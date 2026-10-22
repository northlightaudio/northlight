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
    std::thread probe([&] { joined = ScopedProAudioThread().joinedMmcss(); });
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
                joined = priority.joinedMmcss();
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
    for (int i = 0; i < 2000 && pool.mmcssWorkers() != expected; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(pool.mmcssWorkers() == expected);
}
#endif
