#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

// ITU-R BS.1770 loudness for the master: K-weighting (a high shelf and a high-pass
// per channel) feeding mean-square accumulators over a 400 ms window (momentary)
// and a 3 s window (short-term). Integrated loudness with gating is left for the
// export stage. Real-time: processBlock allocates nothing after prepare().

namespace northlight::engine
{

class LoudnessMeter
{
public:
    struct Reading
    {
        float momentaryLufs = -100.0f;
        float shortTermLufs = -100.0f;
        float integratedLufs = -100.0f;
    };

    static constexpr int momentaryMs = 400;
    static constexpr int shortTermMs = 3000;
    static constexpr int hopMs = 100; // window updates every 100 ms, as the spec overlaps

    // Controller thread.
    void prepare(double sampleRate)
    {
        sampleRate_ = sampleRate;
        hopSamples_ = static_cast<int>(sampleRate * hopMs / 1000.0);
        for (auto& f : shelf_)
            f = designShelf(sampleRate);
        for (auto& f : highPass_)
            f = designHighPass(sampleRate);
        hops_.assign(static_cast<size_t>(shortTermMs / hopMs), 0.0);
        hopIndex_ = 0;
        hopSum_ = 0.0;
        blocks_.assign(static_cast<size_t>(maxBlocks), 0.0f);
        blockCount_.store(0, std::memory_order_relaxed);
        hopCount_ = 0;
        momentary_.store(-100.0f, std::memory_order_relaxed);
        shortTerm_.store(-100.0f, std::memory_order_relaxed);
    }

    // Real-time.
    void processBlock(const float* left, const float* right, int numSamples) noexcept
    {
        if (hopSamples_ <= 0)
            return;
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = highPass_[0].process(shelf_[0].process(left[i]));
            const float r = highPass_[1].process(shelf_[1].process(right[i]));
            hopSum_ += static_cast<double>(l) * l + static_cast<double>(r) * r;
            if (++hopCount_ >= hopSamples_)
            {
                hops_[static_cast<size_t>(hopIndex_)] = hopSum_ / hopSamples_;
                hopIndex_ = (hopIndex_ + 1) % static_cast<int>(hops_.size());
                hopSum_ = 0.0;
                hopCount_ = 0;
                publish();
            }
        }
    }

    // Any thread.
    Reading reading() const noexcept
    {
        return {momentary_.load(std::memory_order_relaxed),
                shortTerm_.load(std::memory_order_relaxed),
                integratedLufs()};
    }

private:
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
        float process(float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return static_cast<float>(y);
        }
    };

    // The BS.1770 pre-filter: a high shelf of about +4 dB above 1.5 kHz.
    static Biquad designShelf(double fs)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan(std::numbers::pi * f0 / fs);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad f;
        f.b0 = (Vh + Vb * K / Q + K * K) / a0;
        f.b1 = 2.0 * (K * K - Vh) / a0;
        f.b2 = (Vh - Vb * K / Q + K * K) / a0;
        f.a1 = 2.0 * (K * K - 1.0) / a0;
        f.a2 = (1.0 - K / Q + K * K) / a0;
        return f;
    }

    // The RLB weighting: a 2nd-order high-pass at about 38 Hz.
    static Biquad designHighPass(double fs)
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan(std::numbers::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad f;
        // BS.1770 gives b = {1, -2, 1} with only the a's normalised: the filter has
        // +0.04 dB in the pass band and the -0.691 offset accounts for that.
        f.b0 = 1.0;
        f.b1 = -2.0;
        f.b2 = 1.0;
        f.a1 = 2.0 * (K * K - 1.0) / a0;
        f.a2 = (1.0 - K / Q + K * K) / a0;
        return f;
    }

    void publish() noexcept
    {
        const auto n = static_cast<int>(hops_.size());
        const auto average = [&](int count)
        {
            double sum = 0.0;
            for (int k = 1; k <= count; ++k)
                sum += hops_[static_cast<size_t>((hopIndex_ - k + n) % n)];
            return sum / count;
        };
        const auto lufs = [](double meanSquare)
        { return meanSquare > 1e-12 ? static_cast<float>(-0.691 + 10.0 * std::log10(meanSquare)) : -100.0f; };
        const double momentaryMean = average(momentaryMs / hopMs);
        momentary_.store(lufs(momentaryMean), std::memory_order_relaxed);
        shortTerm_.store(lufs(average(shortTermMs / hopMs)), std::memory_order_relaxed);
        // Gating blocks for the integrated measure: the 400 ms window at every hop.
        if (resetRequested_.exchange(false, std::memory_order_acq_rel))
        {
            blockCount_.store(0, std::memory_order_relaxed);
            hopsSinceReset_ = 0;
        }
        // Only full 400 ms windows of material since the reset count as blocks.
        if (++hopsSinceReset_ < momentaryMs / hopMs)
            return;
        const int stored = blockCount_.load(std::memory_order_relaxed);
        if (stored < maxBlocks)
        {
            blocks_[static_cast<size_t>(stored)] = static_cast<float>(momentaryMean);
            blockCount_.store(stored + 1, std::memory_order_release);
        }
    }

public:
    // Controller: restarts the integrated measurement (the next hop clears it).
    void resetIntegrated() noexcept { resetRequested_.store(true, std::memory_order_release); }

    // Controller: integrated loudness per BS.1770-4 over the blocks since the last
    // reset: blocks under -70 LUFS are dropped (absolute gate), then blocks more
    // than 10 LU under the mean of the rest (relative gate). -100 when nothing
    // passes the gates.
    float integratedLufs() const noexcept
    {
        const int n = std::min(maxBlocks, blockCount_.load(std::memory_order_acquire));
        const auto lufsOf = [](double meanSquare)
        { return meanSquare > 1e-12 ? -0.691 + 10.0 * std::log10(meanSquare) : -100.0; };
        double sum = 0.0;
        int count = 0;
        for (int i = 0; i < n; ++i)
            if (const double ms = blocks_[static_cast<size_t>(i)]; lufsOf(ms) > -70.0)
            {
                sum += ms;
                ++count;
            }
        if (count == 0)
            return -100.0f;
        const double relative = lufsOf(sum / count) - 10.0;
        sum = 0.0;
        count = 0;
        for (int i = 0; i < n; ++i)
            if (const double ms = blocks_[static_cast<size_t>(i)];
                lufsOf(ms) > relative && lufsOf(ms) > -70.0)
            {
                sum += ms;
                ++count;
            }
        return count == 0 ? -100.0f : static_cast<float>(lufsOf(sum / count));
    }

private:
    static constexpr int maxBlocks = 72000; // two hours of 100 ms hops

    double sampleRate_ = 48000.0;
    int hopSamples_ = 0;
    std::array<Biquad, 2> shelf_, highPass_;
    std::vector<double> hops_;
    int hopIndex_ = 0;
    double hopSum_ = 0.0;
    int hopCount_ = 0;
    std::atomic<float> momentary_{-100.0f};
    std::atomic<float> shortTerm_{-100.0f};
    std::vector<float> blocks_; // mean square of the 400 ms window at each hop since the reset
    std::atomic<int> blockCount_{0};
    int hopsSinceReset_ = 0;
    std::atomic<bool> resetRequested_{false};
};

} // namespace northlight::engine
