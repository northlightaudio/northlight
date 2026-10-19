#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// Callback-time statistics published from the real-time thread without locks or
// allocation: a fixed histogram of callback durations as a fraction of the block's
// deadline, plus running max and overrun counters. The controller side reads
// percentiles from the histogram whenever it likes.

namespace aural::rt
{

class CallbackStats
{
public:
    static constexpr int bucketCount =
        128; // each bucket is 1/64 of the deadline; 2x deadline and above saturate

    struct Summary
    {
        double p50 = 0.0; // fractions of the block deadline (1.0 = deadline)
        double p99 = 0.0;
        double max = 0.0;
        std::uint64_t callbacks = 0;
        std::uint64_t overruns = 0; // callbacks that took longer than their deadline
    };

    // Real-time. `load` is callback time divided by the block's wall-clock duration.
    void record(double load) noexcept
    {
        const int bucket = load <= 0.0 ? 0 : (load >= 2.0 ? bucketCount - 1 : static_cast<int>(load * 64.0));
        histogram_[static_cast<size_t>(bucket)].fetch_add(1, std::memory_order_relaxed);
        callbacks_.fetch_add(1, std::memory_order_relaxed);
        if (load > 1.0)
            overruns_.fetch_add(1, std::memory_order_relaxed);
        float current = max_.load(std::memory_order_relaxed);
        const auto candidate = static_cast<float>(load);
        while (candidate > current &&
               !max_.compare_exchange_weak(current, candidate, std::memory_order_relaxed))
        {
        }
    }

    // Controller thread.
    Summary summary() const noexcept
    {
        Summary s;
        s.callbacks = callbacks_.load(std::memory_order_relaxed);
        s.overruns = overruns_.load(std::memory_order_relaxed);
        s.max = max_.load(std::memory_order_relaxed);
        std::uint64_t total = 0;
        std::array<std::uint64_t, bucketCount> counts{};
        for (size_t i = 0; i < counts.size(); ++i)
        {
            counts[i] = histogram_[i].load(std::memory_order_relaxed);
            total += counts[i];
        }
        if (total == 0)
            return s;
        s.p50 = percentile(counts, total, 0.50);
        s.p99 = percentile(counts, total, 0.99);
        return s;
    }

    void reset() noexcept
    {
        for (auto& b : histogram_)
            b.store(0, std::memory_order_relaxed);
        callbacks_.store(0, std::memory_order_relaxed);
        overruns_.store(0, std::memory_order_relaxed);
        max_.store(0.0f, std::memory_order_relaxed);
    }

private:
    static double
    percentile(const std::array<std::uint64_t, bucketCount>& counts, std::uint64_t total, double p)
    {
        const auto target = static_cast<std::uint64_t>(static_cast<double>(total) * p);
        std::uint64_t seen = 0;
        for (size_t i = 0; i < counts.size(); ++i)
        {
            seen += counts[i];
            if (seen > target)
                return (static_cast<double>(i) + 0.5) / 64.0;
        }
        return 2.0;
    }

    std::array<std::atomic<std::uint64_t>, bucketCount> histogram_{};
    std::atomic<std::uint64_t> callbacks_{0};
    std::atomic<std::uint64_t> overruns_{0};
    std::atomic<float> max_{0.0f};
};

} // namespace aural::rt
