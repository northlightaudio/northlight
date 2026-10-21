#pragma once

#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#include <xmmintrin.h>
#define NORTHLIGHT_HAS_SSE_CSR 1
#endif

// Sets flush-to-zero and denormals-are-zero on the calling thread for the lifetime
// of the object and restores the previous state afterwards. JUCE-free equivalent of
// juce::ScopedNoDenormals for engine worker threads.

namespace northlight::rt
{

class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
    {
#if defined(NORTHLIGHT_HAS_SSE_CSR)
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040u); // FTZ (bit 15) | DAZ (bit 6)
#endif
    }

    ~ScopedFlushDenormals() noexcept
    {
#if defined(NORTHLIGHT_HAS_SSE_CSR)
        _mm_setcsr(saved_);
#endif
    }

    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

private:
#if defined(NORTHLIGHT_HAS_SSE_CSR)
    unsigned int saved_ = 0;
#endif
};

} // namespace northlight::rt
