#include "engine/ExportDsp.h"

#include "engine/Gain.h"
#include "engine/Loudness.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace northlight::engine
{

namespace
{
// The zeroth-order modified Bessel function, for the Kaiser window.
double besselI0(double x) noexcept
{
    double sum = 1.0, term = 1.0;
    const double half = 0.5 * x;
    for (int k = 1; k < 64; ++k)
    {
        term *= (half / k) * (half / k);
        sum += term;
        if (term < sum * 1e-17)
            break;
    }
    return sum;
}

double kaiser(double x, double halfWidth, double beta) noexcept
{
    const double r = std::abs(x) / halfWidth;
    if (r >= 1.0)
        return 0.0;
    return besselI0(beta * std::sqrt(1.0 - r * r)) / besselI0(beta);
}

double sinc(double x) noexcept
{
    if (std::abs(x) < 1e-12)
        return 1.0;
    const double px = std::numbers::pi * x;
    return std::sin(px) / px;
}

// The windowed sinc sampled finely over [0, zeroCrossings], read with linear
// interpolation: the converter evaluates the kernel at arbitrary offsets.
class SincTable
{
public:
    static constexpr int zeroCrossings = 48;
    static constexpr int steps = 1024; // per zero crossing
    static constexpr double beta = 9.0;

    SincTable()
    {
        table_.resize(static_cast<size_t>(zeroCrossings * steps + 2), 0.0f);
        for (int i = 0; i <= zeroCrossings * steps; ++i)
        {
            const double u = static_cast<double>(i) / steps;
            table_[static_cast<size_t>(i)] = static_cast<float>(sinc(u) * kaiser(u, zeroCrossings, beta));
        }
    }

    // The kernel at |u| zero crossings from its centre; 0 outside.
    float at(double u) const noexcept
    {
        const double index = u * steps;
        const auto i = static_cast<size_t>(index);
        if (i + 1 >= table_.size())
            return 0.0f;
        const auto f = static_cast<float>(index - static_cast<double>(i));
        return table_[i] + f * (table_[i + 1] - table_[i]);
    }

    static const SincTable& instance()
    {
        static const SincTable table;
        return table;
    }

private:
    std::vector<float> table_;
};

// The 4x oversampling interpolator for true peak: three fractional phases
// (1/4, 2/4, 3/4 between samples), 16 taps each side of a Kaiser-windowed sinc.
struct TruePeakFilter
{
    static constexpr int half = 16;
    static constexpr int phases = 4;
    float taps[phases][2 * half] = {};

    TruePeakFilter()
    {
        for (int m = 1; m < phases; ++m)
        {
            const double offset = static_cast<double>(m) / phases;
            for (int t = 0; t < 2 * half; ++t)
            {
                // Input sample index n - half + 1 + t contributes at distance d.
                const double d = static_cast<double>(t - half + 1) - offset;
                taps[m][t] = static_cast<float>(sinc(d) * kaiser(d, half + 1.0, 7.0));
            }
        }
    }

    static const TruePeakFilter& instance()
    {
        static const TruePeakFilter filter;
        return filter;
    }
};
} // namespace

double gainToDb(double gain) noexcept
{
    return gainToDb(gain, measurementFloorDb);
}

double dbToGain(double db) noexcept
{
    return dbToGain(db, measurementFloorDb);
}

// ----- sample-rate conversion ------------------------------------------------------------

std::size_t resampledLength(std::size_t length, double fromRate, double toRate) noexcept
{
    if (fromRate <= 0.0 || toRate <= 0.0)
        return length;
    return static_cast<std::size_t>(std::llround(static_cast<double>(length) * toRate / fromRate));
}

namespace
{
// Whether a long offline pass should end now: checked once per this many samples.
constexpr std::size_t stopCheckInterval = 4096;
bool stopped(const std::atomic<bool>* stop) noexcept
{
    return stop != nullptr && stop->load(std::memory_order_acquire);
}
} // namespace

std::vector<float> resample(
    const float* input, std::size_t length, double fromRate, double toRate, const std::atomic<bool>* stop)
{
    if (fromRate == toRate || fromRate <= 0.0 || toRate <= 0.0)
        return std::vector<float>(input, input + length);
    const auto& table = SincTable::instance();
    const std::size_t outLength = resampledLength(length, fromRate, toRate);
    std::vector<float> output(outLength);
    const double ratio = fromRate / toRate; // input samples per output sample
    // Kernel zero crossings per input sample: 1 when upsampling; below 1 when
    // downsampling, which widens the kernel and lowers its cut-off to the new Nyquist.
    const double step = std::min(1.0, toRate / fromRate) * 0.92;
    const auto reach = static_cast<std::int64_t>(std::ceil(SincTable::zeroCrossings / step));
    const auto last = static_cast<std::int64_t>(length) - 1;
    for (std::size_t j = 0; j < outLength; ++j)
    {
        if (j % stopCheckInterval == 0 && stopped(stop))
            return {};
        const double p = static_cast<double>(j) * ratio;
        const auto centre = static_cast<std::int64_t>(std::floor(p));
        const auto from = std::max<std::int64_t>(0, centre - reach + 1);
        const auto to = std::min<std::int64_t>(last, centre + reach);
        double sum = 0.0;
        for (auto k = from; k <= to; ++k)
            sum += static_cast<double>(input[k]) * table.at(std::abs(p - static_cast<double>(k)) * step);
        output[j] = static_cast<float>(sum * step);
    }
    return output;
}

// ----- measurement -----------------------------------------------------------------------

float samplePeak(const float* signal, std::size_t length) noexcept
{
    float peak = 0.0f;
    for (std::size_t i = 0; i < length; ++i)
        peak = std::max(peak, std::abs(signal[i]));
    return peak;
}

float truePeak(const float* signal, std::size_t length, const std::atomic<bool>* stop) noexcept
{
    const auto& filter = TruePeakFilter::instance();
    constexpr int half = TruePeakFilter::half;
    float peak = samplePeak(signal, length); // phase 0 is the samples themselves
    const auto n = static_cast<std::int64_t>(length);
    for (std::int64_t i = 0; i < n; ++i)
    {
        if (static_cast<std::size_t>(i) % stopCheckInterval == 0 && stopped(stop))
            break;
        // Interpolated values between sample i and i + 1.
        const std::int64_t first = i - half + 1;
        for (int m = 1; m < TruePeakFilter::phases; ++m)
        {
            float sum = 0.0f;
            for (int t = 0; t < 2 * half; ++t)
            {
                const auto k = first + t;
                if (k >= 0 && k < n)
                    sum += signal[k] * filter.taps[m][t];
            }
            peak = std::max(peak, std::abs(sum));
        }
    }
    return peak;
}

double integratedLoudness(const std::vector<std::vector<float>>& channels,
                          double sampleRate,
                          const std::atomic<bool>* stop)
{
    if (channels.empty() || channels[0].empty())
        return -100.0;
    LoudnessMeter meter;
    meter.prepare(sampleRate);
    const auto length = channels[0].size();
    const bool stereo = channels.size() > 1;
    std::vector<float> silence(stereo ? 0 : 4096, 0.0f);
    constexpr std::size_t chunk = 4096;
    for (std::size_t at = 0; at < length && !stopped(stop); at += chunk)
    {
        const auto n = static_cast<int>(std::min(chunk, length - at));
        meter.processBlock(channels[0].data() + at, stereo ? channels[1].data() + at : silence.data(), n);
    }
    return meter.integratedLufs();
}

ExportAnalysis analyse(const std::vector<std::vector<float>>& channels,
                       double sampleRate,
                       bool withTruePeak,
                       const std::atomic<bool>* stop)
{
    ExportAnalysis a;
    a.integratedLufs = integratedLoudness(channels, sampleRate, stop);
    float sample = 0.0f, trueP = 0.0f;
    for (const auto& c : channels)
    {
        sample = std::max(sample, samplePeak(c.data(), c.size()));
        if (withTruePeak)
            trueP = std::max(trueP, truePeak(c.data(), c.size(), stop));
    }
    a.samplePeakDb = gainToDb(sample);
    a.truePeakDb = withTruePeak ? gainToDb(trueP) : a.samplePeakDb;
    return a;
}

// ----- normalisation ---------------------------------------------------------------------

Normalisation
normalisationGain(double measuredLufs, double peakDb, double targetLufs, double ceilingDb) noexcept
{
    Normalisation n;
    if (measuredLufs <= -70.0)
        return n;
    n.gainDb = targetLufs - measuredLufs;
    if (peakDb + n.gainDb > ceilingDb)
    {
        n.gainDb = ceilingDb - peakDb;
        n.limitedByCeiling = true;
    }
    return n;
}

void applyGain(std::vector<std::vector<float>>& channels, float gain) noexcept
{
    if (gain == 1.0f)
        return;
    for (auto& c : channels)
        for (auto& v : c)
            v *= gain;
}

// ----- quantisation ----------------------------------------------------------------------

Quantiser::Quantiser(int bits, DitherMode mode, std::uint64_t seed) noexcept
    : bits_(std::clamp(bits, 8, 24)), mode_(mode), state_(seed != 0 ? seed : 1)
{
    scale_ = std::ldexp(1.0, bits_ - 1);
    min_ = -scale_;
    max_ = scale_ - 1.0;
}

double Quantiser::uniform() noexcept
{
    // xorshift64*: fast, and plenty for dither.
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    const std::uint64_t r = state_ * 0x2545f4914f6cdd1dull;
    return static_cast<double>(r >> 11) * (1.0 / 9007199254740992.0);
}

std::int32_t Quantiser::process(float sample) noexcept
{
    double v = static_cast<double>(sample) * scale_;
    if (mode_ == DitherMode::shaped)
        v -= shaping[0] * e1_ + shaping[1] * e2_ + shaping[2] * e3_;
    const double dither = mode_ == DitherMode::off ? 0.0 : uniform() - uniform();
    const double q = std::clamp(std::floor(v + dither + 0.5), min_, max_);
    if (mode_ == DitherMode::shaped)
    {
        // A clipped sample would feed back a large error and ring; bound it.
        e3_ = e2_;
        e2_ = e1_;
        e1_ = std::clamp(q - v, -4.0, 4.0);
    }
    return static_cast<std::int32_t>(q);
}

} // namespace northlight::engine
