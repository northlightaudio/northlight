#include "engine/rt/ThreadPriority.h"

#include "engine/rt/RealtimeGuard.h"
#include "engine/rt/RtKit.h"

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
// avrt.h needs windows.h first.
// clang-format off
#include <windows.h>
#include <avrt.h>
// clang-format on
#elif defined(__linux__)
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#endif

namespace northlight::rt
{

#if defined(__linux__)
namespace
{
// SCHED_FIFO for the calling thread at `priority`, lowered to what RLIMIT_RTPRIO
// allows; whether it took. A process with CAP_SYS_NICE is not held to the limit.
bool setFifoDirectly(int priority) noexcept
{
    int allowed = priority;
    rlimit limit{};
    if (getrlimit(RLIMIT_RTPRIO, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur > 0)
        allowed = std::min(priority, static_cast<int>(limit.rlim_cur));
    sched_param param{};
    param.sched_priority =
        std::clamp(allowed, sched_get_priority_min(SCHED_FIFO), sched_get_priority_max(SCHED_FIFO));
    // sched_* rather than pthread_*: glibc caches what pthread_setschedparam set, so
    // mixing the two (rtkit and the destructor use sched_*) reads stale values.
    return sched_setscheduler(0, SCHED_FIFO, &param) == 0;
}
} // namespace
#endif

ScopedProAudioThread::ScopedProAudioThread() noexcept
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("ScopedProAudioThread");
#if defined(_WIN32)
    DWORD taskIndex = 0;
    task_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    realtime_ = task_ != nullptr;
    if (task_ == nullptr)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#elif defined(__linux__)
    realtime_ = setFifoDirectly(realtimePriority) || rtkit::makeCurrentThreadRealtime(realtimePriority) > 0;
#endif
}

ScopedProAudioThread::~ScopedProAudioThread() noexcept
{
#if defined(_WIN32)
    if (task_ != nullptr)
        AvRevertMmThreadCharacteristics(task_);
#elif defined(__linux__)
    if (realtime_)
    {
        // Lowering one's own scheduling needs no permission, but clearing
        // SCHED_RESET_ON_FORK does, and rtkit sets it: keep it as it is.
        const int current = sched_getscheduler(0);
        const int resetOnFork = current >= 0 ? (current & SCHED_RESET_ON_FORK) : 0;
        sched_param param{};
        sched_setscheduler(0, SCHED_OTHER | resetOnFork, &param);
    }
#endif
}

const char* realtimeSchedulingName() noexcept
{
#if defined(_WIN32)
    return "MMCSS";
#else
    return "real-time";
#endif
}

void raiseDiskThreadPriority() noexcept
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("raiseDiskThreadPriority");
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#elif defined(__linux__)
    // A thread's nice level is set through its own id on Linux.
    const auto thread = static_cast<id_t>(syscall(SYS_gettid));
    if (setpriority(PRIO_PROCESS, thread, diskThreadNice) != 0)
        rtkit::makeCurrentThreadHighPriority(diskThreadNice);
#endif
}

} // namespace northlight::rt
