#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

namespace northlight::ui
{

// Paint-time statistics for the heavy views (11-accessibility-performance.md
// 11.8): the arrange view and the mixer record how long each paint took; the
// Diagnostics panel and --benchmark-ui read the percentiles over the last
// minute. Message thread only.
class FrameStats
{
public:
    struct Summary
    {
        double p50Ms = 0.0, p99Ms = 0.0, maxMs = 0.0;
        int frames = 0;
    };

    // Records one paint's duration.
    static void record(double milliseconds) noexcept
    {
        auto& s = instance();
        s.samples_[s.next_ % capacity] = milliseconds;
        ++s.next_;
    }

    // The percentiles over the samples kept (up to the last `capacity` paints).
    static Summary summary()
    {
        const auto& s = instance();
        const std::size_t count = std::min<std::size_t>(s.next_, capacity);
        Summary out;
        if (count == 0)
            return out;
        std::vector<double> sorted(s.samples_.begin(),
                                   s.samples_.begin() + static_cast<std::ptrdiff_t>(count));
        std::sort(sorted.begin(), sorted.end());
        out.frames = static_cast<int>(count);
        out.p50Ms = sorted[count / 2];
        out.p99Ms = sorted[std::min(count - 1, count * 99 / 100)];
        out.maxMs = sorted.back();
        return out;
    }

    static void reset() noexcept { instance().next_ = 0; }

    // Times a paint from construction to destruction.
    class Scope
    {
    public:
        Scope() : start_(juce::Time::getMillisecondCounterHiRes()) {}
        ~Scope() { record(juce::Time::getMillisecondCounterHiRes() - start_); }

    private:
        double start_;
    };

private:
    static constexpr std::size_t capacity = 1800; // a minute at 30 Hz
    static FrameStats& instance()
    {
        static FrameStats stats;
        return stats;
    }
    std::array<double, capacity> samples_{};
    std::size_t next_ = 0;
};

} // namespace northlight::ui
