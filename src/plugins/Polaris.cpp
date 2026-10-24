#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/Pedals.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace northlight::plugins::polaris
{

namespace
{
constexpr double analysisRate = 8000.0; // what the input is decimated to, at least
constexpr int windowSize = 1024;        // samples at the analysis rate: 128 ms at 8 kHz
constexpr int ringSize = 2048;
constexpr int hop = 256; // samples at the analysis rate between analyses
constexpr double antiAliasHz = 1800.0;
// YIN's threshold on the normalised difference, and the most it accepts when
// nothing falls under the threshold.
constexpr float yinThreshold = 0.15f;
constexpr float yinFallback = 0.35f;
// Quieter than this (peak) and there is nothing to tune.
constexpr float silencePeak = 0.002f;
constexpr float muteRampSeconds = 0.005f;

// The squared difference d(tau) of a window with itself shifted, over the first
// `span` samples, for tau in [from, to).
void difference(const float* x, int span, int from, int to, float* d) noexcept
{
    for (int tau = from; tau < to; ++tau)
    {
        float sum = 0.0f;
        const float* a = x;
        const float* b = x + tau;
        for (int j = 0; j < span; ++j)
        {
            const float diff = a[j] - b[j];
            sum += diff * diff;
        }
        d[tau] = sum;
    }
}

// YIN's pick from the differences raw[0..tauMax]: the cumulative mean normalised
// difference (into `d`), the first dip under the threshold (or the deepest under
// the fallback), refined between samples on the raw differences (YIN's step 5:
// the normalised curve is skewed between lags). The period in samples (0 for none).
float pick(const float* raw, float* d, int tauMin, int tauMax, float* clarity) noexcept
{
    float running = 0.0f;
    d[0] = 1.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        running += raw[tau];
        d[tau] = running > 0.0f ? raw[tau] * static_cast<float>(tau) / running : 1.0f;
    }
    int best = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
        if (d[tau] < yinThreshold)
        {
            while (tau + 1 <= tauMax && d[tau + 1] < d[tau])
                ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        int deepest = tauMin;
        for (int tau = tauMin; tau <= tauMax; ++tau)
            if (d[tau] < d[deepest])
                deepest = tau;
        if (d[deepest] < yinFallback)
            best = deepest;
    }
    if (best < 0)
    {
        if (clarity != nullptr)
            *clarity = 0.0f;
        return 0.0f;
    }
    if (clarity != nullptr)
        *clarity = std::clamp(1.0f - d[best], 0.0f, 1.0f);
    // A high note's period is a few samples: measured over as many periods as the
    // lags reach and divided back, its sub-sample error shrinks with them.
    const int periods = std::max(1, tauMax / best - 1);
    int at = best * periods;
    for (int tau = std::max(1, at - periods); tau <= std::min(tauMax - 1, at + periods); ++tau)
        if (raw[tau] < raw[at])
            at = tau;
    float period = static_cast<float>(at);
    if (at > 1 && at < tauMax)
    {
        const float a = raw[at - 1], b = raw[at], c = raw[at + 1];
        const float denominator = a - 2.0f * b + c;
        if (std::abs(denominator) > 1.0e-12f)
            period += std::clamp(0.5f * (a - c) / denominator, -0.5f, 0.5f);
    }
    return period / static_cast<float>(periods);
}

struct Lags
{
    int tauMin = 2, tauMax = 2, span = 0;
};

Lags lagsFor(double rate, int count) noexcept
{
    Lags l;
    l.tauMin = std::max(2, static_cast<int>(std::floor(rate / highestHz)));
    l.tauMax = std::min(static_cast<int>(std::ceil(rate / lowestHz)), count / 2);
    l.span = count - l.tauMax;
    return l;
}
} // namespace

float detectPitch(const float* samples, int count, double sampleRate, float* clarity)
{
    if (clarity != nullptr)
        *clarity = 0.0f;
    if (samples == nullptr || count < 16)
        return 0.0f;
    float peak = 0.0f;
    for (int i = 0; i < count; ++i)
        peak = std::max(peak, std::abs(samples[i]));
    if (peak < silencePeak)
        return 0.0f;
    const auto lags = lagsFor(sampleRate, count);
    if (lags.tauMax <= lags.tauMin + 2)
        return 0.0f;
    std::vector<float> raw(static_cast<size_t>(lags.tauMax + 2), 0.0f), d(raw.size(), 0.0f);
    difference(samples, lags.span, 1, lags.tauMax + 1, raw.data());
    const float period = pick(raw.data(), d.data(), lags.tauMin, lags.tauMax, clarity);
    return period > 0.0f ? static_cast<float>(sampleRate / period) : 0.0f;
}

Note noteFor(float hz, float referenceHz) noexcept
{
    Note n;
    if (!(hz > 0.0f) || !(referenceHz > 0.0f))
        return n;
    const float exact = 69.0f + 12.0f * std::log2(hz / referenceHz);
    n.midi = static_cast<int>(std::lround(exact));
    n.cents = std::clamp((exact - static_cast<float>(n.midi)) * 100.0f, -50.0f, 50.0f);
    return n;
}

const char* noteName(int midi) noexcept
{
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[((midi % 12) + 12) % 12];
}

int octaveOf(int midi) noexcept
{
    return static_cast<int>(std::floor(static_cast<double>(midi) / 12.0)) - 1;
}

namespace
{
class Polaris final : public MeteredBuiltIn
{
public:
    Polaris()
        : MeteredBuiltIn(type,
                         {{"Reference", "Hz", minReferenceHz, maxReferenceHz, 440.0f, false},
                          {"Mute", "", 0.0f, 1.0f, 0.0f, false, {"Off", "On"}}})
    {
    }

    Reading reading() const noexcept
    {
        return {hz_.load(std::memory_order_relaxed),
                clarity_.load(std::memory_order_relaxed),
                level_.load(std::memory_order_relaxed)};
    }

private:
    void prepareBuiltIn(double sampleRate, int) override
    {
        decimation_ = std::max(1, static_cast<int>(std::floor(sampleRate / analysisRate)));
        rate_ = sampleRate / decimation_;
        for (auto& f : antiAlias_)
        {
            f.setLowPass(sampleRate, std::min(antiAliasHz, 0.4 * rate_));
            f.reset();
        }
        ring_.assign(ringSize, 0.0f);
        window_.assign(windowSize, 0.0f);
        diff_.assign(windowSize + 2, 0.0f);
        normalised_.assign(windowSize + 2, 0.0f);
        lags_ = lagsFor(rate_, windowSize);
        written_ = 0;
        phase_ = 0;
        sinceAnalysis_ = 0;
        nextLag_ = -1;
        windowPeak_ = 0.0f;
        primed_ = false; // the first block takes the footswitch as it is then
        muteStep_ = 1.0f / static_cast<float>(std::max(1.0, sampleRate * muteRampSeconds));
        hz_.store(0.0f, std::memory_order_relaxed);
        clarity_.store(0.0f, std::memory_order_relaxed);
        level_.store(0.0f, std::memory_order_relaxed);
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        if (ring_.empty())
            return;
        float peak = 0.0f;
        // Decimate the mono input into the ring.
        for (int i = 0; i < numSamples; ++i)
        {
            const float mono = 0.5f * (left[i] + right[i]);
            peak = std::max(peak, std::abs(mono));
            const float filtered = antiAlias_[1].process(antiAlias_[0].process(mono));
            if (++phase_ >= decimation_)
            {
                phase_ = 0;
                ring_[static_cast<size_t>(written_ % ringSize)] = filtered;
                ++written_;
                ++sinceAnalysis_;
            }
        }
        level_.store(peak, std::memory_order_relaxed);
        blockExtra_ = peak;
        analyse(numSamples);

        // The footswitch: the output faded out and back in.
        const float target = parameter(mute) >= 0.5f ? 0.0f : 1.0f;
        if (!primed_)
        {
            muteGain_ = target;
            primed_ = true;
        }
        if (muteGain_ != target || target == 0.0f)
            for (int i = 0; i < numSamples; ++i)
            {
                muteGain_ = target > muteGain_ ? std::min(target, muteGain_ + muteStep_)
                                               : std::max(target, muteGain_ - muteStep_);
                left[i] *= muteGain_;
                right[i] *= muteGain_;
            }
        for (auto& f : antiAlias_)
        {
            if (std::abs(f.z1) < 1.0e-20f)
                f.z1 = 0.0f;
            if (std::abs(f.z2) < 1.0e-20f)
                f.z2 = 0.0f;
        }
    }

    // A window's analysis, a slice of its lags per block.
    void analyse(int numSamples) noexcept
    {
        if (nextLag_ < 0)
        {
            if (sinceAnalysis_ < hop || written_ < windowSize)
                return;
            sinceAnalysis_ = 0;
            windowPeak_ = 0.0f;
            const auto start = written_ - windowSize;
            for (int i = 0; i < windowSize; ++i)
            {
                const float v = ring_[static_cast<size_t>((start + i) % ringSize)];
                window_[static_cast<size_t>(i)] = v;
                windowPeak_ = std::max(windowPeak_, std::abs(v));
            }
            if (windowPeak_ < silencePeak)
            {
                hz_.store(0.0f, std::memory_order_relaxed);
                clarity_.store(0.0f, std::memory_order_relaxed);
                return;
            }
            nextLag_ = 1;
        }
        // Enough lags each block to finish a window well within a hop.
        const int lags = lags_.tauMax + 1 - 1;
        const int perBlock =
            std::max(16,
                     static_cast<int>(std::ceil(lags * static_cast<double>(numSamples) /
                                                (decimation_ * static_cast<double>(hop) * 0.7))));
        const int to = std::min(lags_.tauMax + 1, nextLag_ + perBlock);
        difference(window_.data(), lags_.span, nextLag_, to, diff_.data());
        nextLag_ = to;
        if (nextLag_ <= lags_.tauMax)
            return;
        nextLag_ = -1;
        float clarity = 0.0f;
        const float period = pick(diff_.data(), normalised_.data(), lags_.tauMin, lags_.tauMax, &clarity);
        hz_.store(period > 0.0f ? static_cast<float>(rate_ / period) : 0.0f, std::memory_order_relaxed);
        clarity_.store(clarity, std::memory_order_relaxed);
    }

    int decimation_ = 1;
    double rate_ = analysisRate;
    std::array<Biquad, 2> antiAlias_;
    std::vector<float> ring_, window_, diff_, normalised_;
    Lags lags_;
    long long written_ = 0;
    int phase_ = 0, sinceAnalysis_ = 0, nextLag_ = -1;
    float windowPeak_ = 0.0f;
    float muteGain_ = 1.0f, muteStep_ = 0.0f;
    bool primed_ = false;
    std::atomic<float> hz_{0.0f}, clarity_{0.0f}, level_{0.0f};
};
} // namespace

bool readingOf(const engine::InsertProcessor* processor, Reading& out) noexcept
{
    const auto* p = dynamic_cast<const Polaris*>(processor);
    if (p == nullptr)
        return false;
    out = p->reading();
    return true;
}

std::unique_ptr<engine::InsertProcessor> create()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("polaris::create");
    return std::make_unique<Polaris>();
}

} // namespace northlight::plugins::polaris
