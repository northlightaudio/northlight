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

namespace aural::engine
{

class LoudnessMeter
{
public:
    struct Reading
    {
        float momentaryLufs = -100.0f;
        float shortTermLufs = -100.0f;
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
        return {momentary_.load(std::memory_order_relaxed), shortTerm_.load(std::memory_order_relaxed)};
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
        f.b0 = 1.0 / a0;
        f.b1 = -2.0 / a0;
        f.b2 = 1.0 / a0;
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
        momentary_.store(lufs(average(momentaryMs / hopMs)), std::memory_order_relaxed);
        shortTerm_.store(lufs(average(shortTermMs / hopMs)), std::memory_order_relaxed);
    }

    double sampleRate_ = 48000.0;
    int hopSamples_ = 0;
    std::array<Biquad, 2> shelf_, highPass_;
    std::vector<double> hops_;
    int hopIndex_ = 0;
    double hopSum_ = 0.0;
    int hopCount_ = 0;
    std::atomic<float> momentary_{-100.0f};
    std::atomic<float> shortTerm_{-100.0f};
};

} // namespace aural::engine
