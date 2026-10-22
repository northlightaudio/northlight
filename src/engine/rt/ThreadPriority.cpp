#include "engine/rt/ThreadPriority.h"

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
// avrt.h needs windows.h first.
// clang-format off
#include <windows.h>
#include <avrt.h>
// clang-format on
#endif

namespace northlight::rt
{

ScopedProAudioThread::ScopedProAudioThread() noexcept
{
#if defined(_WIN32)
    DWORD taskIndex = 0;
    task_ = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (task_ == nullptr)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}

ScopedProAudioThread::~ScopedProAudioThread() noexcept
{
#if defined(_WIN32)
    if (task_ != nullptr)
        AvRevertMmThreadCharacteristics(task_);
#endif
}

void raiseDiskThreadPriority() noexcept
{
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
}

} // namespace northlight::rt
