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

namespace
{
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
    if (void* p = _aligned_malloc(size, static_cast<std::size_t>(alignment)))
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
    return _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    checkAllocation();
    return _aligned_malloc(size == 0 ? 1 : size, static_cast<std::size_t>(alignment));
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
    _aligned_free(p);
}

void operator delete[](void* p, std::align_val_t) noexcept
{
    checkDeallocation();
    _aligned_free(p);
}

void operator delete(void* p, std::size_t, std::align_val_t) noexcept
{
    checkDeallocation();
    _aligned_free(p);
}

void operator delete[](void* p, std::size_t, std::align_val_t) noexcept
{
    checkDeallocation();
    _aligned_free(p);
}

#endif // NORTHLIGHT_RT_CHECKS
