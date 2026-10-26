#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// The peaks of the last blocks a real-time thread measured, for any number of
// readers that each want "the peak since I last looked" without taking it from the
// others (the Audio Setup window, the Preferences Audio page and the first-run
// sheet can all be live at once). The writer stores each block's peak in a ring
// and bumps a count; a Reader remembers the count it last saw and takes the
// largest peak of the blocks since, at most the ring's length back. One writer;
// readers on any thread. Never allocates, locks or waits.

namespace northlight::rt
{

class PeakHistory
{
public:
    static constexpr std::size_t length = 256; // blocks: over 100 ms even at 16-sample buffers

    // The one writer (real-time): a block's peak.
    void publish(float peak) noexcept
    {
        const auto n = count_.load(std::memory_order_relaxed);
        slots_[static_cast<std::size_t>(n % length)].store(peak, std::memory_order_relaxed);
        count_.store(n + 1, std::memory_order_release);
    }

    // One reader's place in the history. Message thread (or any one thread per reader).
    class Reader
    {
    public:
        // The largest peak published since the last take (0 when none); a reader
        // that fell more than `length` blocks behind sees the latest `length`.
        float take(const PeakHistory& history) noexcept
        {
            const auto n = history.count_.load(std::memory_order_acquire);
            const auto from = n - seen_ > length ? n - length : seen_;
            float peak = 0.0f;
            for (auto i = from; i < n; ++i)
                peak = std::max(
                    peak,
                    history.slots_[static_cast<std::size_t>(i % length)].load(std::memory_order_relaxed));
            seen_ = n;
            return peak;
        }

    private:
        std::uint64_t seen_ = 0;
    };

private:
    std::array<std::atomic<float>, length> slots_{};
    std::atomic<std::uint64_t> count_{0};
};

} // namespace northlight::rt
