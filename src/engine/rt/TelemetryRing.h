#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

// A lock-free ring of fixed-size frames of floats, for a processor's live display:
// the real-time thread publishes a frame now and then and any number of UI readers
// follow it, each with its own cursor. Every value is an atomic, so a reader that
// races the writer reads a torn frame at worst, never undefined behaviour; readers
// stay clear of the slots near the write head. Allocated once, at construction.

namespace northlight::rt
{

template <int ValueCount, int Length> class TelemetryRing
{
    static_assert(ValueCount > 0 && Length > 16);

public:
    static constexpr int valueCount = ValueCount;
    static constexpr int length = Length;
    using Frame = std::array<float, ValueCount>;

    TelemetryRing() : frames_(new std::atomic<float>[static_cast<size_t>(Length) * ValueCount]()) {}

    // ----- real-time thread (one writer) ----------------------------------------------------
    void publish(const Frame& values) noexcept
    {
        const auto w = written_.load(std::memory_order_relaxed);
        auto* slot = frames_.get() + (w % Length) * ValueCount;
        for (int i = 0; i < ValueCount; ++i)
            slot[i].store(values[static_cast<size_t>(i)], std::memory_order_relaxed);
        written_.store(w + 1, std::memory_order_release);
    }

    // ----- any reader -----------------------------------------------------------------------
    // Frames since `cursor` (the newest `maxFrames` of them, and never the 16 slots
    // nearest the write head), oldest first; advances the cursor to the newest.
    int read(std::uint64_t& cursor, Frame* out, int maxFrames) const noexcept
    {
        const auto end = written_.load(std::memory_order_acquire);
        constexpr std::uint64_t safe = Length - 16; // keep clear of the slot being written
        std::uint64_t start = std::max(cursor, end > safe ? end - safe : 0);
        if (end - start > static_cast<std::uint64_t>(std::max(0, maxFrames)))
            start = end - static_cast<std::uint64_t>(std::max(0, maxFrames));
        int n = 0;
        for (auto i = start; i < end; ++i, ++n)
        {
            const auto* slot = frames_.get() + (i % Length) * ValueCount;
            for (int v = 0; v < ValueCount; ++v)
                out[n][static_cast<size_t>(v)] = slot[v].load(std::memory_order_relaxed);
        }
        cursor = end;
        return n;
    }

    std::uint64_t framesWritten() const noexcept { return written_.load(std::memory_order_acquire); }

private:
    std::unique_ptr<std::atomic<float>[]> frames_;
    std::atomic<std::uint64_t> written_{0};
};

} // namespace northlight::rt
