#pragma once

#include <array>
#include <atomic>
#include <cstdint>

// Callback-time statistics published from the real-time thread without locks or
// allocation: a fixed histogram of callback durations as a fraction of the block's
// deadline, plus running max and overrun counters. The controller side reads
// percentiles from the histogram whenever it likes.
//
// It also times when each callback starts. Work inside the callback can be fast
// while the callback itself arrives late (another driver holding the CPU, power
// saving, a USB stall), and a late start is heard as a click even though no
// overrun is counted. recordStart() keeps the time the next callback is due: the
// previous start plus the previous block's length. A callback can be delayed but
// never early, so an early one moves the due time back to it at once; a late one
// within the threshold moves it a little, so drift between the audio clock and the
// CPU clock never adds up to false lates; a late one past the threshold re-anchors
// it so one stall counts once. A block that starts right after the previous one
// (under a quarter of its length) is the same device period split into shorter
// blocks, or the driver catching up, and only adds its length to the due time.

namespace northlight::rt
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
        std::uint64_t late = 0;     // callbacks that started late (isLate)
        double maxLateness = 0.0;   // the latest start, as a fraction of its block
    };

    // A callback that starts this far behind its due time (fraction of its block),
    // and at least lateFloorSeconds behind, counts as late. The floor is for USB
    // drivers, which hand out small blocks on the bus's 1 ms frames: at 64 samples
    // every callback is up to half a block off the ideal schedule, harmlessly.
    static constexpr double lateThreshold = 0.5;
    static constexpr double lateFloorSeconds = 0.002;
    static constexpr bool isLate(double behindSeconds, double blockSeconds) noexcept
    {
        return behindSeconds >= lateThreshold * blockSeconds && behindSeconds >= lateFloorSeconds;
    }
    // How far the due time moves towards each arrival.
    static constexpr double driftFollow = 0.01;

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

    // Real-time, at the start of every callback: `now` is a steady clock in seconds
    // and `blockSeconds` the length of the block about to be processed. Returns how
    // late this callback started as a fraction of the block (0 when on time).
    double recordStart(double now, double blockSeconds) noexcept
    {
        double lateness = 0.0;
        const bool continuation = hasDue_ && now - lastStart_ < 0.25 * lastBlock_;
        if (hasDue_ && !continuation && blockSeconds > 0.0)
        {
            const double behind = now - due_;
            lateness = behind > 0.0 ? behind / blockSeconds : 0.0;
            const bool late = isLate(behind, blockSeconds);
            if (behind <= 0.0 || late)
                due_ = now; // early: the clocks moved; late: one stall counts once
            else
                due_ += behind * driftFollow;
            if (late)
                late_.fetch_add(1, std::memory_order_relaxed);
            float current = maxLateness_.load(std::memory_order_relaxed);
            const auto candidate = static_cast<float>(lateness);
            while (candidate > current &&
                   !maxLateness_.compare_exchange_weak(current, candidate, std::memory_order_relaxed))
            {
            }
        }
        else if (!hasDue_)
        {
            due_ = now;
            hasDue_ = true;
        }
        due_ += blockSeconds;
        lastStart_ = now;
        lastBlock_ = blockSeconds;
        return lateness;
    }

    // Controller thread.
    Summary summary() const noexcept
    {
        Summary s;
        s.callbacks = callbacks_.load(std::memory_order_relaxed);
        s.overruns = overruns_.load(std::memory_order_relaxed);
        s.max = max_.load(std::memory_order_relaxed);
        s.late = late_.load(std::memory_order_relaxed);
        s.maxLateness = maxLateness_.load(std::memory_order_relaxed);
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

    // Only while no callback runs (the device is stopped).
    void reset() noexcept
    {
        for (auto& b : histogram_)
            b.store(0, std::memory_order_relaxed);
        callbacks_.store(0, std::memory_order_relaxed);
        overruns_.store(0, std::memory_order_relaxed);
        max_.store(0.0f, std::memory_order_relaxed);
        late_.store(0, std::memory_order_relaxed);
        maxLateness_.store(0.0f, std::memory_order_relaxed);
        hasDue_ = false;
        due_ = 0.0;
        lastStart_ = 0.0;
        lastBlock_ = 0.0;
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
    std::atomic<std::uint64_t> late_{0};
    std::atomic<float> maxLateness_{0.0f};
    double due_ = 0.0;       // real-time thread only
    double lastStart_ = 0.0; // real-time thread only
    double lastBlock_ = 0.0; // real-time thread only
    bool hasDue_ = false;    // real-time thread only
};

} // namespace northlight::rt
