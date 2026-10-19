#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

// The meter block: one slot per channel (tracks, buses, master) that the real-time
// thread writes after each span and the UI drains. Peaks hold their maximum until
// read; RMS is the last span's value. Slots are assigned by the snapshot compiler in
// project order: tracks first, then buses, then the master.

namespace aural::rt
{

class MeterBlock
{
public:
    static constexpr int maxSlots = 512;

    struct Reading
    {
        float peakLeft = 0.0f;
        float peakRight = 0.0f;
        float rmsLeft = 0.0f;
        float rmsRight = 0.0f;
    };

    // Real-time: measures the stereo pair and publishes into `slot`.
    void measure(int slot, const float* left, const float* right, int numSamples) noexcept
    {
        if (slot < 0 || slot >= maxSlots || numSamples <= 0)
            return;
        float peakL = 0.0f, peakR = 0.0f;
        double sumL = 0.0, sumR = 0.0;
        for (int i = 0; i < numSamples; ++i)
        {
            peakL = std::max(peakL, std::abs(left[i]));
            peakR = std::max(peakR, std::abs(right[i]));
            sumL += static_cast<double>(left[i]) * left[i];
            sumR += static_cast<double>(right[i]) * right[i];
        }
        auto& s = slots_[static_cast<size_t>(slot)];
        raise(s.peakLeft, peakL);
        raise(s.peakRight, peakR);
        s.rmsLeft.store(static_cast<float>(std::sqrt(sumL / numSamples)), std::memory_order_relaxed);
        s.rmsRight.store(static_cast<float>(std::sqrt(sumR / numSamples)), std::memory_order_relaxed);
    }

    // Real-time: a silent span (muted channel) so the RMS falls to zero.
    void silence(int slot) noexcept
    {
        if (slot < 0 || slot >= maxSlots)
            return;
        auto& s = slots_[static_cast<size_t>(slot)];
        s.rmsLeft.store(0.0f, std::memory_order_relaxed);
        s.rmsRight.store(0.0f, std::memory_order_relaxed);
    }

    // UI thread: returns the reading and resets the peak hold.
    Reading take(int slot) noexcept
    {
        Reading r;
        if (slot < 0 || slot >= maxSlots)
            return r;
        auto& s = slots_[static_cast<size_t>(slot)];
        r.peakLeft = s.peakLeft.exchange(0.0f, std::memory_order_relaxed);
        r.peakRight = s.peakRight.exchange(0.0f, std::memory_order_relaxed);
        r.rmsLeft = s.rmsLeft.load(std::memory_order_relaxed);
        r.rmsRight = s.rmsRight.load(std::memory_order_relaxed);
        return r;
    }

private:
    struct Slot
    {
        std::atomic<float> peakLeft{0.0f}, peakRight{0.0f}, rmsLeft{0.0f}, rmsRight{0.0f};
    };

    static void raise(std::atomic<float>& target, float candidate) noexcept
    {
        float current = target.load(std::memory_order_relaxed);
        while (candidate > current &&
               !target.compare_exchange_weak(current, candidate, std::memory_order_relaxed))
        {
        }
    }

    std::array<Slot, maxSlots> slots_{};
};

} // namespace aural::rt
