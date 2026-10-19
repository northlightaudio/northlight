#include "plugins/BuiltIns.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace aural::plugins
{

namespace
{
constexpr float minusInfinityDb = -120.0f;

float dbToGain(float db) noexcept
{
    return db <= minusInfinityDb ? 0.0f : std::pow(10.0f, db / 20.0f);
}

float gainToDb(float gain) noexcept
{
    return gain > 0.0f ? 20.0f * std::log10(gain) : minusInfinityDb;
}

// Shared parameter storage: atomics the real-time thread snapshots per block.
class Base : public engine::InsertProcessor
{
public:
    Base(std::string typeName, std::vector<engine::ParameterInfo> infos)
        : typeName_(std::move(typeName)), infos_(std::move(infos)), values_(infos_.size())
    {
        for (size_t i = 0; i < infos_.size(); ++i)
            values_[i].store(infos_[i].defaultValue, std::memory_order_relaxed);
    }

    const std::string& typeName() const noexcept override { return typeName_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return infos_; }

    void setParameter(int index, float value) noexcept override
    {
        if (index < 0 || static_cast<size_t>(index) >= infos_.size())
            return;
        const auto& info = infos_[static_cast<size_t>(index)];
        values_[static_cast<size_t>(index)].store(std::clamp(value, info.minimum, info.maximum),
                                                  std::memory_order_relaxed);
    }

    float parameter(int index) const noexcept override
    {
        if (index < 0 || static_cast<size_t>(index) >= infos_.size())
            return 0.0f;
        return values_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
    }

protected:
    double sampleRate_ = 48000.0;

private:
    std::string typeName_;
    std::vector<engine::ParameterInfo> infos_;
    std::vector<std::atomic<float>> values_;
};

// ----- gain -----------------------------------------------------------------------------------

class Gain final : public Base
{
public:
    Gain() : Base(types::gain, {{"Gain", "dB", -60.0f, 24.0f, 0.0f, false}}) {}

    void prepare(double sampleRate, int) override
    {
        sampleRate_ = sampleRate;
        current_ = dbToGain(parameter(0));
    }

    void process(float* left, float* right, int numSamples) noexcept override
    {
        const float target = isBypassed() ? 1.0f : dbToGain(parameter(0));
        // Ramp across the block so gain changes never click.
        const float step = (target - current_) / static_cast<float>(std::max(1, numSamples));
        float g = current_;
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] *= g;
            right[i] *= g;
            g += step;
        }
        current_ = target;
    }

private:
    float current_ = 1.0f;
};

// ----- EQ -------------------------------------------------------------------------------------

// RBJ cookbook biquad, transposed direct form II.
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process(float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void bypass() noexcept
    {
        b0 = 1.0f;
        b1 = b2 = a1 = a2 = 0.0f;
    }

    void setPeaking(double sampleRate, double hz, double q, double gainDb) noexcept
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * std::clamp(hz, 10.0, sampleRate * 0.49) / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * std::max(0.1, q));
        const double c = std::cos(w0);
        const double a0 = 1.0 + alpha / A;
        set((1.0 + alpha * A) / a0,
            (-2.0 * c) / a0,
            (1.0 - alpha * A) / a0,
            (-2.0 * c) / a0,
            (1.0 - alpha / A) / a0);
    }

    void setLowShelf(double sampleRate, double hz, double gainDb) noexcept
    {
        shelf(sampleRate, hz, gainDb, true);
    }

    void setHighShelf(double sampleRate, double hz, double gainDb) noexcept
    {
        shelf(sampleRate, hz, gainDb, false);
    }

    void setHighPass(double sampleRate, double hz) noexcept
    {
        const double w0 = 2.0 * std::numbers::pi * std::clamp(hz, 10.0, sampleRate * 0.49) / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * std::numbers::sqrt2 / 2.0); // Butterworth Q
        const double c = std::cos(w0);
        const double a0 = 1.0 + alpha;
        set((1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, (-2.0 * c) / a0, (1.0 - alpha) / a0);
    }

private:
    void shelf(double sampleRate, double hz, double gainDb, bool low) noexcept
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * std::numbers::pi * std::clamp(hz, 10.0, sampleRate * 0.49) / sampleRate;
        const double c = std::cos(w0);
        const double s = std::sin(w0);
        const double alpha = s / 2.0 * std::sqrt(2.0); // S = 1
        const double twoRootAAlpha = 2.0 * std::sqrt(A) * alpha;
        if (low)
        {
            const double a0 = (A + 1.0) + (A - 1.0) * c + twoRootAAlpha;
            set(A * ((A + 1.0) - (A - 1.0) * c + twoRootAAlpha) / a0,
                2.0 * A * ((A - 1.0) - (A + 1.0) * c) / a0,
                A * ((A + 1.0) - (A - 1.0) * c - twoRootAAlpha) / a0,
                -2.0 * ((A - 1.0) + (A + 1.0) * c) / a0,
                ((A + 1.0) + (A - 1.0) * c - twoRootAAlpha) / a0);
        }
        else
        {
            const double a0 = (A + 1.0) - (A - 1.0) * c + twoRootAAlpha;
            set(A * ((A + 1.0) + (A - 1.0) * c + twoRootAAlpha) / a0,
                -2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0,
                A * ((A + 1.0) + (A - 1.0) * c - twoRootAAlpha) / a0,
                2.0 * ((A - 1.0) - (A + 1.0) * c) / a0,
                ((A + 1.0) - (A - 1.0) * c - twoRootAAlpha) / a0);
        }
    }

    void set(double nb0, double nb1, double nb2, double na1, double na2) noexcept
    {
        b0 = static_cast<float>(nb0);
        b1 = static_cast<float>(nb1);
        b2 = static_cast<float>(nb2);
        a1 = static_cast<float>(na1);
        a2 = static_cast<float>(na2);
    }
};

class Eq final : public Base
{
public:
    enum Param
    {
        highPassHz,
        lowHz,
        lowDb,
        midHz,
        midDb,
        midQ,
        highHz,
        highDb,
    };

    Eq()
        : Base(types::eq,
               {{"High-pass", "Hz", 0.0f, 1000.0f, 0.0f, true},
                {"Low freq", "Hz", 30.0f, 1000.0f, 120.0f, true},
                {"Low gain", "dB", -18.0f, 18.0f, 0.0f, false},
                {"Mid freq", "Hz", 100.0f, 10000.0f, 1000.0f, true},
                {"Mid gain", "dB", -18.0f, 18.0f, 0.0f, false},
                {"Mid Q", "", 0.3f, 10.0f, 1.0f, true},
                {"High freq", "Hz", 1500.0f, 18000.0f, 8000.0f, true},
                {"High gain", "dB", -18.0f, 18.0f, 0.0f, false}})
    {
    }

    void prepare(double sampleRate, int) override
    {
        sampleRate_ = sampleRate;
        for (auto& f : filters_)
            f.reset();
        lastParams_.fill(std::nanf(""));
    }

    void process(float* left, float* right, int numSamples) noexcept override
    {
        if (isBypassed())
            return;
        updateCoefficients();
        for (int i = 0; i < numSamples; ++i)
        {
            float l = left[i], r = right[i];
            for (int f = 0; f < 4; ++f)
            {
                l = filters_[static_cast<size_t>(f)].process(l);
                r = filters_[static_cast<size_t>(4 + f)].process(r);
            }
            left[i] = l;
            right[i] = r;
        }
    }

private:
    void updateCoefficients() noexcept
    {
        std::array<float, 8> params{};
        bool changed = false;
        for (int i = 0; i < 8; ++i)
        {
            params[static_cast<size_t>(i)] = parameter(i);
            changed = changed || params[static_cast<size_t>(i)] != lastParams_[static_cast<size_t>(i)];
        }
        if (!changed)
            return;
        lastParams_ = params;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* f = filters_.data() + ch * 4;
            if (params[highPassHz] > 0.0f)
                f[0].setHighPass(sampleRate_, params[highPassHz]);
            else
                f[0].bypass();
            f[1].setLowShelf(sampleRate_, params[lowHz], params[lowDb]);
            f[2].setPeaking(sampleRate_, params[midHz], params[midQ], params[midDb]);
            f[3].setHighShelf(sampleRate_, params[highHz], params[highDb]);
        }
    }

    std::array<Biquad, 8> filters_; // 4 per channel
    std::array<float, 8> lastParams_{};
};

// ----- compressor -------------------------------------------------------------------------------

class Compressor final : public Base
{
public:
    enum Param
    {
        thresholdDb,
        ratio,
        attackMs,
        releaseMs,
        kneeDb,
        makeupDb,
    };

    Compressor()
        : Base(types::compressor,
               {{"Threshold", "dB", -60.0f, 0.0f, -18.0f, false},
                {"Ratio", ":1", 1.0f, 20.0f, 4.0f, true},
                {"Attack", "ms", 0.1f, 200.0f, 10.0f, true},
                {"Release", "ms", 10.0f, 2000.0f, 100.0f, true},
                {"Knee", "dB", 0.0f, 24.0f, 6.0f, false},
                {"Makeup", "dB", 0.0f, 24.0f, 0.0f, false}})
    {
    }

    void prepare(double sampleRate, int) override
    {
        sampleRate_ = sampleRate;
        envelopeDb_ = minusInfinityDb;
    }

    // Peak detector in the log domain, stereo-linked, feed-forward. With a
    // sidechain the detector listens to the key signal instead of the audio.
    bool acceptsSidechain() const noexcept override { return true; }
    void process(float* left, float* right, int numSamples) noexcept override
    {
        processWithSidechain(left, right, left, right, numSamples);
    }
    void processWithSidechain(float* left,
                              float* right,
                              const float* keyLeft,
                              const float* keyRight,
                              int numSamples) noexcept override
    {
        if (isBypassed())
            return;
        const float threshold = parameter(thresholdDb);
        const float r = parameter(ratio);
        const float knee = parameter(kneeDb);
        const float makeup = dbToGain(parameter(makeupDb));
        const auto coefficient = [this](float ms)
        { return std::exp(-1.0f / (static_cast<float>(sampleRate_) * ms * 0.001f)); };
        const float attack = coefficient(parameter(attackMs));
        const float release = coefficient(parameter(releaseMs));

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = std::max(std::abs(keyLeft[i]), std::abs(keyRight[i]));
            const float inputDb = gainToDb(input);
            const float a = inputDb > envelopeDb_ ? attack : release;
            envelopeDb_ = a * envelopeDb_ + (1.0f - a) * inputDb;

            // Soft-knee gain computer.
            const float over = envelopeDb_ - threshold;
            float reductionDb = 0.0f;
            if (knee > 0.0f && std::abs(over) <= knee * 0.5f)
            {
                const float x = over + knee * 0.5f;
                reductionDb = (1.0f / r - 1.0f) * x * x / (2.0f * knee);
            }
            else if (over > 0.0f)
                reductionDb = (1.0f / r - 1.0f) * over;

            const float g = dbToGain(reductionDb) * makeup;
            left[i] *= g;
            right[i] *= g;
        }
        reduction_.store(
            std::max(0.0f, -(envelopeDb_ > threshold ? (1.0f / r - 1.0f) * (envelopeDb_ - threshold) : 0.0f)),
            std::memory_order_relaxed);
    }

private:
    float envelopeDb_ = minusInfinityDb;
    std::atomic<float> reduction_{0.0f};
};

// ----- limiter ----------------------------------------------------------------------------------

// Brickwall by instantaneous gain computation with a fast release: no lookahead, so
// it adds no latency (delay compensation arrives in Phase 5). Transients that
// exceed the ceiling within one sample are clipped to it.
class Limiter final : public Base
{
public:
    enum Param
    {
        ceilingDb,
        releaseMs,
        lookaheadMs,
    };

    static constexpr float maxLookaheadMs = 10.0f;

    Limiter()
        : Base(types::limiter,
               {{"Ceiling", "dB", -24.0f, 0.0f, -0.3f, false},
                {"Release", "ms", 1.0f, 1000.0f, 50.0f, true},
                {"Lookahead", "ms", 0.0f, maxLookaheadMs, 1.5f, false}})
    {
    }

    // Lookahead: the signal is delayed by L samples while the gain the next L
    // samples need is already known, so a peak is attenuated before it arrives and
    // nothing crosses the ceiling. The gain applied to the sample leaving the delay
    // at time t is the mean over the last L samples of the running minimum of the
    // needed gain over [t, t + L] (released), which is never above what sample t
    // needs and ramps smoothly instead of stepping.
    void prepare(double sampleRate, int) override
    {
        sampleRate_ = sampleRate;
        capacity_ = static_cast<int>(std::ceil(sampleRate * maxLookaheadMs * 0.001)) + 2;
        delayL_.assign(static_cast<size_t>(capacity_), 0.0f);
        delayR_.assign(static_cast<size_t>(capacity_), 0.0f);
        needed_.assign(static_cast<size_t>(capacity_), 1.0f);
        minima_.assign(static_cast<size_t>(capacity_), 0);
        smoothed_.assign(static_cast<size_t>(capacity_), 1.0f);
        reset(lookaheadSamples());
    }

    int latencySamples() const noexcept override { return lookaheadSamples(); }

    void process(float* left, float* right, int numSamples) noexcept override
    {
        if (isBypassed())
            return;
        const float ceiling = dbToGain(parameter(ceilingDb));
        const float release =
            std::exp(-1.0f / (static_cast<float>(sampleRate_) * parameter(releaseMs) * 0.001f));
        const int lookahead = lookaheadSamples();
        if (lookahead != length_)
            reset(lookahead); // a changed lookahead restarts the pipeline (no allocation)

        if (length_ == 0)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float peak = std::max(std::abs(left[i]), std::abs(right[i]));
                const float needed = peak > ceiling ? ceiling / peak : 1.0f;
                gain_ = needed < gain_ ? needed : release * gain_ + (1.0f - release);
                if (gain_ > 1.0f)
                    gain_ = 1.0f;
                left[i] = std::clamp(left[i] * gain_, -ceiling, ceiling);
                right[i] = std::clamp(right[i] * gain_, -ceiling, ceiling);
            }
            return;
        }

        const int L = length_;
        const int window = L + 1; // the sample itself and the next L
        for (int i = 0; i < numSamples; ++i)
        {
            // 1. The gain this input sample needs, into the window ring (modulo capacity).
            const float peak = std::max(std::abs(left[i]), std::abs(right[i]));
            const float needed = peak > ceiling ? ceiling / peak : 1.0f;
            needed_[static_cast<size_t>(head_)] = needed;
            // A monotonic deque of ring indices with increasing needed gain: the
            // front is the window minimum. Drop what left the window, then every
            // candidate this sample beats.
            while (dequeCount_ > 0 && age(minima_[static_cast<size_t>(dequeFront_)]) >= window)
                popFront();
            while (dequeCount_ > 0 &&
                   needed_[static_cast<size_t>(minima_[static_cast<size_t>(back())])] >= needed)
                --dequeCount_;
            minima_[static_cast<size_t>((dequeFront_ + dequeCount_) % capacity_)] = head_;
            ++dequeCount_;

            // 2. The window minimum applies to the sample leaving the delay; release
            //    lets it recover between peaks.
            const float minimum = needed_[static_cast<size_t>(minima_[static_cast<size_t>(dequeFront_)])];
            released_ = minimum < released_ ? minimum : release * released_ + (1.0f - release);
            if (released_ > 1.0f)
                released_ = 1.0f;

            // 3. Smooth: the mean of the last L released values (double sum, no drift).
            const auto slot = static_cast<size_t>(pos_); // rings of length L
            sum_ += static_cast<double>(released_) - static_cast<double>(smoothed_[slot]);
            smoothed_[slot] = released_;
            const float gain = std::min(1.0f, static_cast<float>(sum_ / L));

            // 4. The sample written L samples ago leaves through the gain; this one enters.
            const float outL = delayL_[slot] * gain;
            const float outR = delayR_[slot] * gain;
            delayL_[slot] = left[i];
            delayR_[slot] = right[i];
            left[i] = std::clamp(outL, -ceiling, ceiling);
            right[i] = std::clamp(outR, -ceiling, ceiling);
            if (++pos_ == L)
                pos_ = 0;
            if (++head_ == capacity_)
                head_ = 0;
        }
    }

private:
    int lookaheadSamples() const noexcept
    {
        const auto ms = std::clamp(parameter(lookaheadMs), 0.0f, maxLookaheadMs);
        return static_cast<int>(std::lround(sampleRate_ * ms * 0.001));
    }

    void reset(int length) noexcept
    {
        length_ = std::clamp(length, 0, capacity_ - 2);
        head_ = 0;
        pos_ = 0;
        dequeFront_ = 0;
        dequeCount_ = 0;
        released_ = 1.0f;
        sum_ = static_cast<double>(std::max(1, length_)); // L ones
        gain_ = 1.0f;
        std::fill(delayL_.begin(), delayL_.end(), 0.0f);
        std::fill(delayR_.begin(), delayR_.end(), 0.0f);
        std::fill(needed_.begin(), needed_.end(), 1.0f);
        std::fill(smoothed_.begin(), smoothed_.end(), 1.0f);
    }

    int back() const noexcept { return (dequeFront_ + dequeCount_ - 1 + capacity_) % capacity_; }
    void popFront() noexcept
    {
        if (++dequeFront_ == capacity_)
            dequeFront_ = 0;
        --dequeCount_;
    }
    // How many samples ago the needed_ ring index was written (0 = this sample).
    int age(int index) const noexcept
    {
        int a = head_ - index;
        if (a < 0)
            a += capacity_;
        return a;
    }

    int capacity_ = 0;
    int length_ = 0;
    int head_ = 0; // needed_ / minima_ ring position (modulo capacity_)
    int pos_ = 0;  // delay and smoothing ring position (modulo length_)
    int dequeFront_ = 0, dequeCount_ = 0;
    float released_ = 1.0f;
    double sum_ = 1.0;
    float gain_ = 1.0f;
    std::vector<float> delayL_, delayR_, needed_, smoothed_;
    std::vector<int> minima_;
};
} // namespace

std::vector<std::string> BuiltInFactory::typeNames() const
{
    return {types::gain, types::eq, types::compressor, types::limiter};
}

std::unique_ptr<engine::InsertProcessor> BuiltInFactory::create(const std::string& typeName) const
{
    AURAL_ASSERT_NOT_REALTIME("BuiltInFactory::create");
    if (typeName == types::gain)
        return std::make_unique<Gain>();
    if (typeName == types::eq)
        return std::make_unique<Eq>();
    if (typeName == types::compressor)
        return std::make_unique<Compressor>();
    if (typeName == types::limiter)
        return std::make_unique<Limiter>();
    return nullptr;
}

std::string BuiltInFactory::displayName(const std::string& typeName)
{
    if (typeName == types::gain)
        return "Gain";
    if (typeName == types::eq)
        return "EQ";
    if (typeName == types::compressor)
        return "Compressor";
    if (typeName == types::limiter)
        return "Limiter";
    return typeName;
}

std::vector<float> defaultParameters(const std::string& typeName)
{
    const auto processor = BuiltInFactory().create(typeName);
    std::vector<float> values;
    if (processor != nullptr)
        for (const auto& p : processor->parameters())
            values.push_back(p.defaultValue);
    return values;
}

} // namespace aural::plugins
