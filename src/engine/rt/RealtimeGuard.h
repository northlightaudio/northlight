#pragma once

#include <cstddef>

// Real-time safety guard.
//
// Any thread that is inside a ScopedRealtimeSection is a real-time thread for the
// duration of that scope: it must not allocate, lock, block, log, or touch the
// filesystem. See docs/rt-rules.md.
//
// When NORTHLIGHT_RT_CHECKS is defined (Debug and RelWithDebInfo by default), the global
// operator new/delete are hooked (RealtimeAllocationHook.cpp) and any allocation on a
// real-time thread reports a violation. Code that takes a lock or does other
// forbidden work should call NORTHLIGHT_ASSERT_NOT_REALTIME() so the same reporting path
// catches it.
//
// In builds without NORTHLIGHT_RT_CHECKS everything here compiles to nothing.

namespace northlight::rt
{

enum class ViolationKind
{
    allocation,
    deallocation,
    explicitAssert,
};

struct Violation
{
    ViolationKind kind;
    const char* what; // static string describing the operation
    const char* file; // may be null for allocation hooks
    int line;         // 0 when unknown
};

// Handler must itself be real-time safe: no allocation, no locks. The default handler
// writes to stderr and aborts.
using ViolationHandler = void (*)(const Violation&) noexcept;

ViolationHandler setViolationHandler(ViolationHandler handler) noexcept;
void reportViolation(const Violation& violation) noexcept;

// True when the calling thread is currently inside a real-time section.
bool isRealtimeThread() noexcept;

// Marks the calling thread as real-time for the lifetime of the object. Nestable.
class ScopedRealtimeSection
{
public:
    ScopedRealtimeSection() noexcept;
    ~ScopedRealtimeSection() noexcept;

    ScopedRealtimeSection(const ScopedRealtimeSection&) = delete;
    ScopedRealtimeSection& operator=(const ScopedRealtimeSection&) = delete;
};

// Temporarily leaves the real-time section on the calling thread. Only for use in
// debug tooling that must allocate from an RT context (e.g. the violation handler in
// tests). Never use this in production engine code.
class ScopedRealtimeSuspend
{
public:
    ScopedRealtimeSuspend() noexcept;
    ~ScopedRealtimeSuspend() noexcept;

    ScopedRealtimeSuspend(const ScopedRealtimeSuspend&) = delete;
    ScopedRealtimeSuspend& operator=(const ScopedRealtimeSuspend&) = delete;

private:
    int savedDepth_;
};

namespace detail
{
void assertNotRealtime(const char* what, const char* file, int line) noexcept;
}

} // namespace northlight::rt

#if defined(NORTHLIGHT_RT_CHECKS)
#define NORTHLIGHT_ASSERT_NOT_REALTIME(what)                                                                 \
    ::northlight::rt::detail::assertNotRealtime((what), __FILE__, __LINE__)
#else
#define NORTHLIGHT_ASSERT_NOT_REALTIME(what) ((void)0)
#endif
