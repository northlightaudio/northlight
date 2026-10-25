#include "plugins/FeedbackNetwork.h"

#include "engine/Gain.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/dsp/Smoothing.h"

#include <complex>
#include <numbers>

namespace northlight::plugins::fdn
{

namespace
{
constexpr float pi = std::numbers::pi_v<float>;
// Below this the shimmer's heads add in phase (a gain above one), so the shifted path is cut there.
constexpr double shiftLowCutHz = 60.0;
// Each line's modulation rate, as a multiple of the set rate, so the lines never beat together.
constexpr std::array<float, lineCount> lfoSpread{1.0f, 1.13f, 0.87f, 1.29f, 0.71f, 1.07f, 0.93f, 1.21f};

// sin(pi x) for x in 0..1 (Bhaskara's approximation, within 0.2%): the shimmer's
// grain window, whose two heads half a grain apart sum to constant power.
inline float grainWindow(float x) noexcept
{
    const float p = x * (1.0f - x);
    return 16.0f * p / (5.0f - 4.0f * p);
}
} // namespace

float meanLineSeconds(const Tuning& tuning) noexcept
{
    float sum = 0.0f;
    for (const float ms : tuning.lineMs)
        sum += ms;
    return sum * 0.001f / static_cast<float>(lineCount);
}

float meanAllpassSeconds(const Tuning& tuning) noexcept
{
    float sum = 0.0f;
    for (const float ms : tuning.allpassMs)
        sum += ms;
    return sum * 0.001f / static_cast<float>(lineCount);
}

float onePole(float hz, double sampleRate) noexcept
{
    return dsp::stepForHz(hz, static_cast<float>(sampleRate));
}

int powerOfTwoAtLeast(double n) noexcept
{
    int size = 1;
    while (size < n)
        size <<= 1;
    return size;
}

float decayAt(
    const Tuning& tuning, float hz, float decaySeconds, float scale, float dampingHz, float lowCutHz) noexcept
{
    constexpr double rate = 48000.0;
    const double length = meanLineSeconds(tuning) * scale + meanAllpassSeconds(tuning);
    const double rt = std::clamp(static_cast<double>(decaySeconds), 0.01, 1.0e6);
    const double g = std::pow(10.0, -3.0 * length / rt);
    const double k = std::pow(g, tuning.bandDecayRatio - 1.0);
    const std::complex<double> z1 = std::polar(1.0, -2.0 * std::numbers::pi * hz / rate);
    const auto lowPass = [&](float corner)
    {
        const double a = onePole(corner, rate);
        return a / (1.0 - (1.0 - a) * z1);
    };
    const auto high = lowPass(dampingHz);
    const auto low = lowPass(lowCutHz);
    const double shelves = std::abs(high + k * (1.0 - high)) * std::abs((1.0 - low) + k * low);
    const double passDb = engine::gainToDb(g * shelves, engine::magnitudeFloorDb);
    return static_cast<float>(passDb < -1.0e-9 ? -60.0 * length / passDb : rt);
}

void FeedbackNetwork::prepare(const Tuning& tuning, double sampleRate)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("FeedbackNetwork::prepare");
    tuning_ = tuning;
    sampleRate_ = sampleRate;

    // The lines: the longest line at the largest scale, the deepest modulation
    // and the shimmer's window.
    const double lineSeconds = tuning.lineMs.back() * 0.001 * tuning.maxScale + 2.0 * tuning.maxModSeconds +
                               tuning.shimmerWindowSeconds + 0.002;
    const int lineSize = powerOfTwoAtLeast(lineSeconds * sampleRate + 8.0);
    for (auto& line : lines_)
    {
        line.buffer.assign(static_cast<size_t>(lineSize), 0.0f);
        line.high = line.low = 0.0f;
        line.guard.reset();
        line.shiftLowCut.setHighPass(sampleRate, shiftLowCutHz);
        line.shiftLowCut.reset();
    }
    guardRatios_.fill(0.0f); // designed again at this rate
    lineMask_ = lineSize - 1;
    lineWrite_ = 0;
    for (int i = 0; i < lineCount; ++i)
    {
        auto& line = lines_[static_cast<size_t>(i)];
        line.allpass.prepare(tuning.allpassMs[static_cast<size_t>(i)] * 0.001f, sampleRate);
        // Each line's modulation at its own rate and phase, its grain at its own phase.
        line.lfoCos = std::cos(2.0f * pi * static_cast<float>(i) / lineCount);
        line.lfoSin = std::sin(2.0f * pi * static_cast<float>(i) / lineCount);
        line.grain = static_cast<float>(i) / lineCount;
    }
    for (size_t i = 0; i < diffusers_[0].size(); ++i)
    {
        diffusers_[0][i].prepare(tuning.diffuserLeftMs[i] * 0.001f, sampleRate);
        diffusers_[1][i].prepare(tuning.diffuserRightMs[i] * 0.001f, sampleRate);
    }
    energy_ = 0.0f;
    governor_ = 1.0f;

    const auto rate = static_cast<float>(sampleRate);
    const auto smoothing = [rate](float seconds) { return dsp::stepForSeconds(seconds, rate); };
    energyStep_ = smoothing(0.02f); // the governor's measure
    governorDown_ = smoothing(0.005f);
    governorUp_ = smoothing(0.3f);
    windowSamples_ = std::max(1.0f, tuning.shimmerWindowSeconds * rate);
}

void FeedbackNetwork::clear() noexcept
{
    for (auto& line : lines_)
    {
        std::fill(line.buffer.begin(), line.buffer.end(), 0.0f);
        line.allpass.clear();
        line.guard.reset();
        line.shiftLowCut.reset();
        line.high = line.low = 0.0f;
    }
    for (auto& channel : diffusers_)
        for (auto& d : channel)
            d.clear();
    energy_ = 0.0f;
    governor_ = 1.0f;
}

void FeedbackNetwork::beginBlock(const BlockSettings& settings) noexcept
{
    const auto rate = static_cast<float>(sampleRate_);
    diffuserGain_ = 0.7f * settings.diffusion;
    loopAllpassGain_ = 0.5f * settings.diffusion;
    dampStep_ = onePole(settings.dampingHz, sampleRate_);
    lowStep_ = onePole(settings.lowCutHz, sampleRate_);
    const bool pitchMoved = settings.pitchRatios != guardRatios_;
    guardRatios_ = settings.pitchRatios;
    for (int i = 0; i < lineCount; ++i)
    {
        const auto ii = static_cast<size_t>(i);
        const float ratio = settings.pitchRatios[ii];
        up_[ii] = ratio >= 1.0f;
        // What a line holds is band-limited below where shifting it up would
        // fold it back down (and pile up at the top for ever).
        if (pitchMoved)
        {
            auto& guard = lines_[ii].guard;
            if (up_[ii])
                guard.setLowPass(sampleRate_, std::min(18000.0, 0.4 * sampleRate_ / ratio));
            else
                guard.setLowPass(sampleRate_, std::min(18000.0, 0.4 * sampleRate_));
        }
        grainStep_[ii] = std::abs(ratio - 1.0f) / windowSamples_;
        const float w = 2.0f * pi * settings.lfoHz * lfoSpread[ii] / rate;
        lfoRotCos_[ii] = std::cos(w);
        lfoRotSin_[ii] = std::sin(w);
        baseSamples_[ii] = tuning_.lineMs[ii] * 0.001f * rate;
        // The line's gain for the decay at the scale this block ends near (the
        // scale glides, so the lengths do too).
        const float allpassSamples = tuning_.allpassMs[ii] * 0.001f * rate;
        const float length = baseSamples_[ii] * settings.endScale + allpassSamples + settings.modSamples;
        lineGain_[ii] = std::pow(10.0f, -3.0f * length / (rate * settings.decaySeconds));
        bandGain_[ii] = std::pow(lineGain_[ii], tuning_.bandDecayRatio - 1.0f);
    }
}

void FeedbackNetwork::diffuse(float& left, float& right) noexcept
{
    for (auto& d : diffusers_[0])
        left = d.process(left, diffuserGain_);
    for (auto& d : diffusers_[1])
        right = d.process(right, diffuserGain_);
}

void FeedbackNetwork::process(
    const Frame& frame, float sendLeft, float sendRight, float& tailLeft, float& tailRight) noexcept
{
    float outs[lineCount];
    float energy = 0.0f, sum = 0.0f;
    for (int i = 0; i < lineCount; ++i)
    {
        const auto ii = static_cast<size_t>(i);
        auto& line = lines_[ii];
        // The modulation: a sine per line, kept on the unit circle.
        const float c = line.lfoCos * lfoRotCos_[ii] - line.lfoSin * lfoRotSin_[ii];
        line.lfoSin = line.lfoSin * lfoRotCos_[ii] + line.lfoCos * lfoRotSin_[ii];
        line.lfoCos = c;
        const float delay =
            std::max(4.0f, baseSamples_[ii] * frame.scale + frame.sweep * (1.0f + line.lfoSin));
        const float* buffer = line.buffer.data();
        float v = readHermite(buffer, lineMask_, lineWrite_, delay);
        if (frame.shimmering)
        {
            // Two heads sweeping the grain window, half a grain apart: the line
            // read faster (up) or slower (down) than it is written.
            line.grain += grainStep_[ii];
            if (line.grain >= 1.0f)
                line.grain -= 1.0f;
            const float a = line.grain;
            const float b = a < 0.5f ? a + 0.5f : a - 0.5f;
            const float offsetA = up_[ii] ? windowSamples_ * (1.0f - a) : windowSamples_ * a;
            const float offsetB = up_[ii] ? windowSamples_ * (1.0f - b) : windowSamples_ * b;
            // The lowest frequencies come out of the two heads in phase, louder
            // than they went in, and shifting never moves them on: they would
            // grow for ever, so they stay out of the shifted path.
            const float shifted = line.shiftLowCut.process(
                grainWindow(a) * readHermite(buffer, lineMask_, lineWrite_, delay + offsetA) +
                grainWindow(b) * readHermite(buffer, lineMask_, lineWrite_, delay + offsetB));
            v = v * frame.keep + shifted * frame.shift;
        }
        // Damping and low cut: shelves whose depth follows the line's gain (none
        // while frozen).
        const float gain = (lineGain_[ii] + (1.0f - lineGain_[ii]) * frame.freeze) * governor_;
        const float band = bandGain_[ii] + (1.0f - bandGain_[ii]) * frame.freeze;
        line.high += dampStep_ * (v - line.high);
        v = line.high + band * (v - line.high);
        line.low += lowStep_ * (v - line.low);
        v = (v - line.low) + band * line.low;
        v = line.allpass.process(v * gain, loopAllpassGain_);
        outs[i] = v;
        sum += v;
        energy += v * v;
    }
    // Householder feedback, the input into alternate lines (and into the tail at
    // once, as its first, diffused reflections).
    const float householder = sum * (2.0f / lineCount);
    float tailL = 0.0f, tailR = 0.0f;
    for (int i = 0; i < lineCount; ++i)
    {
        auto& line = lines_[static_cast<size_t>(i)];
        const float sign = (i / 2) % 2 == 0 ? 1.0f : -1.0f;
        const float in = (i % 2 == 0 ? sendLeft : sendRight) * sign;
        float write = outs[i] - householder + in;
        if (frame.shimmering)
            write += (line.guard.process(write) - write) * frame.shimmer;
        line.buffer[static_cast<size_t>(lineWrite_)] = std::clamp(write, -8.0f, 8.0f);
        (i % 2 == 0 ? tailL : tailR) += outs[i] * sign;
    }
    lineWrite_ = (lineWrite_ + 1) & lineMask_;
    tailL += tuning_.earlyGain * sendLeft;
    tailR += tuning_.earlyGain * sendRight;

    // The governor: above the ceiling, the loop's gain comes down until the
    // energy is back under it.
    energy_ += (energy * (1.0f / lineCount) - energy_) * energyStep_;
    const float rms = std::sqrt(energy_);
    const float target = rms > tuning_.energyCeiling ? tuning_.energyCeiling / rms : 1.0f;
    governor_ += (target - governor_) * (target < governor_ ? governorDown_ : governorUp_);

    tailLeft = tailL * 0.5f;
    tailRight = tailR * 0.5f;
}

} // namespace northlight::plugins::fdn
