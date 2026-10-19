#include "engine/rt/RealtimeGuard.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace aural::rt
{

namespace
{
// Depth > 0 means the current thread is inside at least one realtime section.
thread_local int realtimeDepth = 0;

void defaultHandler(const Violation& v) noexcept
{
    const char* kind = "violation";
    switch (v.kind)
    {
        case ViolationKind::allocation:
            kind = "allocation";
            break;
        case ViolationKind::deallocation:
            kind = "deallocation";
            break;
        case ViolationKind::explicitAssert:
            kind = "forbidden operation";
            break;
    }

    // fprintf may allocate on some runtimes; we are about to abort anyway.
    if (v.file != nullptr)
        std::fprintf(
            stderr, "[aural::rt] %s on a real-time thread: %s (%s:%d)\n", kind, v.what, v.file, v.line);
    else
        std::fprintf(stderr, "[aural::rt] %s on a real-time thread: %s\n", kind, v.what);

    std::fflush(stderr);
    std::abort();
}

std::atomic<ViolationHandler> currentHandler{&defaultHandler};
} // namespace

ViolationHandler setViolationHandler(ViolationHandler handler) noexcept
{
    if (handler == nullptr)
        handler = &defaultHandler;
    return currentHandler.exchange(handler, std::memory_order_acq_rel);
}

void reportViolation(const Violation& violation) noexcept
{
    // Suspend the RT flag while the handler runs so a handler that (in tests) allocates
    // does not recurse back into reportViolation.
    ScopedRealtimeSuspend suspend;
    currentHandler.load(std::memory_order_acquire)(violation);
}

bool isRealtimeThread() noexcept
{
    return realtimeDepth > 0;
}

ScopedRealtimeSection::ScopedRealtimeSection() noexcept
{
    ++realtimeDepth;
}

ScopedRealtimeSection::~ScopedRealtimeSection() noexcept
{
    --realtimeDepth;
}

ScopedRealtimeSuspend::ScopedRealtimeSuspend() noexcept : savedDepth_(realtimeDepth)
{
    realtimeDepth = 0;
}

ScopedRealtimeSuspend::~ScopedRealtimeSuspend() noexcept
{
    realtimeDepth = savedDepth_;
}

namespace detail
{
void assertNotRealtime(const char* what, const char* file, int line) noexcept
{
    if (isRealtimeThread())
        reportViolation({ViolationKind::explicitAssert, what, file, line});
}
} // namespace detail

} // namespace aural::rt
