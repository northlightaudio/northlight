// Global operator new/delete override that reports allocations on real-time threads.
//
// This file is compiled into an OBJECT library (northlight_rt_checks) and linked into every
// executable so the override cannot be dropped by the linker. It does nothing unless
// NORTHLIGHT_RT_CHECKS is defined.
//
// Only C++ operator new/delete are hooked. malloc/free from C libraries and plugins
// are not, so this catches our own mistakes, not third-party ones.

#include "engine/rt/RealtimeGuard.h"

#if defined(NORTHLIGHT_RT_CHECKS)

#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace
{
// Over-aligned blocks: MSVC's CRT has no std::aligned_alloc, and its aligned
// blocks must be freed with _aligned_free.
void* alignedMalloc(std::size_t size, std::size_t alignment) noexcept
{
#if defined(_WIN32)
    return _aligned_malloc(size, alignment);
#else
    void* p = nullptr;
    if (alignment < sizeof(void*))
        alignment = sizeof(void*);
    return posix_memalign(&p, alignment, size) == 0 ? p : nullptr;
#endif
}

void alignedFree(void* p) noexcept
{
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

void checkAllocation() noexcept
{
    if (northlight::rt::isRealtimeThread())
        northlight::rt::reportViolation(
            {northlight::rt::ViolationKind::allocation, "operator new", nullptr, 0});
}

void checkDeallocation() noexcept
{
    if (northlight::rt::isRealtimeThread())
        northlight::rt::reportViolation(
            {northlight::rt::ViolationKind::deallocation, "operator delete", nullptr, 0});
}

void* allocate(std::size_t size)
{
    checkAllocation();
    if (size == 0)
        size = 1;
    if (void* p = std::malloc(size))
        return p;
    throw std::bad_alloc{};
}

void* allocateAligned(std::size_t size, std::align_val_t alignment)
{
    checkAllocation();
    if (size == 0)
        size = 1;
    if (void* p = alignedMalloc(size, static_cast<std::size_t>(alignment)))
        return p;
    throw std::bad_alloc{};
}
} // namespace

void* operator new(std::size_t size)
{
    return allocate(size);
}

void* operator new[](std::size_t size)
{
    return allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    checkAllocation();
    return std::malloc(size == 0 ? 1 : size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    checkAllocation();
    return std::malloc(size == 0 ? 1 : size);
}

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocateAligned(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocateAligned(size, alignment);
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    checkAllocation();
    return alignedMalloc(size == 0 ? 1 : size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    checkAllocation();
    return alignedMalloc(size == 0 ? 1 : size, static_cast<std::size_t>(alignment));
}

void operator delete(void* p) noexcept
{
    checkDeallocation();
    std::free(p);
}

void operator delete[](void* p) noexcept
{
    checkDeallocation();
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept
{
    checkDeallocation();
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    checkDeallocation();
    std::free(p);
}

void operator delete(void* p, std::align_val_t) noexcept
{
    checkDeallocation();
    alignedFree(p);
}

void operator delete[](void* p, std::align_val_t) noexcept
{
    checkDeallocation();
    alignedFree(p);
}

void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    checkDeallocation();
    alignedFree(p);
}

void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    checkDeallocation();
    alignedFree(p);
}

#endif // NORTHLIGHT_RT_CHECKS
