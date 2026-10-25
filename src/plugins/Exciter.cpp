#include "plugins/Exciter.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/Oversampler.h"
#include "plugins/ParamHelpers.h"
#include "plugins/dsp/Denormals.h"
#include "plugins/dsp/Shapers.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace northlight::plugins::exciter
{

namespace
{
constexpr double tubeBias = 0.3;
constexpr double crispSoftness = 0.1;
// The bass harmonics' curve: a softer rectifier for the even series under a
// tanh for the odd, at a fixed drive (the bass section has no Drive of its own).
constexpr int bassCurve = characterCount;
constexpr double bassSoftness = 0.05;
constexpr float bassDriveDb = 9.0f;

double softAbs(double x, double e) noexcept
{
    return std::sqrt(x * x + e * e) - e;
}

double softAbsIntegral(double x, double e) noexcept
{
    const double s = std::sqrt(x * x + e * e);
    return 0.5 * (x * s + e * e * std::asinh(x / e)) - e * x;
}
} // namespace

float amountGain(float percent) noexcept
{
    const float a = std::clamp(percent * 0.01f, 0.0f, 1.0f);
    return 2.0f * a * a;
}

float bassGain(float percent) noexcept
{
    const float a = std::clamp(percent * 0.01f, 0.0f, 1.0f);
    return a * a;
}

double curve(int character, double x) noexcept
{
    switch (character)
    {
        case tube:
            return dsp::tube(x, tubeBias);
        case tape:
            return std::tanh(x);
        case air:
            return x * x / (1.0 + std::abs(x));
        case crisp:
            return softAbs(x, crispSoftness);
        case bassCurve:
            return 0.6 * softAbs(x, bassSoftness) + 0.4 * std::tanh(x);
        default:
            return 0.0;
    }
}

double curveIntegral(int character, double x) noexcept
{
    switch (character)
    {
        case tube:
            return dsp::tubeIntegral(x, tubeBias);
        case tape:
            return dsp::logCosh(x);
        case air:
        {
            const double a = std::abs(x);
            const double v = a * a * 0.5 - a + std::log1p(a);
            return x < 0.0 ? -v : v;
        }
        case crisp:
            return softAbsIntegral(x, crispSoftness);
        case bassCurve:
            return 0.6 * softAbsIntegral(x, bassSoftness) + 0.4 * dsp::logCosh(x);
        default:
            return 0.0;
    }
}

namespace
{
// ----- calibration --------------------------------------------------------------------------
//
// For each curve and each dB of drive: the curve's gain at the fundamental for a
// sine at the normalised level (removed, so only what the curve adds is mixed
// in, not a louder or quieter copy of the band) and the RMS of what remains
// (divided out, so every character and drive adds the same level for the same
// Amount). Computed once, off the audio thread.
struct Calibration
{
    float linear = 0.0f; // the fundamental's gain through the curve
    float level = 1.0f;  // 1 / RMS of the harmonics, for a sine at referenceRms
};
constexpr int driveSteps = static_cast<int>(maxDriveDb) + 1;
using CalibrationTable = std::array<std::array<Calibration, driveSteps>, characterCount + 1>;

CalibrationTable buildCalibration()
{
    CalibrationTable table{};
    constexpr int n = 1024;
    for (int c = 0; c <= characterCount; ++c)
        for (int d = 0; d < driveSteps; ++d)
        {
            const double amplitude = std::numbers::sqrt2 * referenceRms * std::pow(10.0, d / 20.0);
            std::array<double, n> y{};
            double fundamental = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double s = std::sin(2.0 * std::numbers::pi * (i + 0.5) / n);
                y[static_cast<size_t>(i)] = curve(c, amplitude * s);
                fundamental += y[static_cast<size_t>(i)] * s;
            }
            const double linear = 2.0 * fundamental / n / amplitude;
            double mean = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double s = std::sin(2.0 * std::numbers::pi * (i + 0.5) / n);
                y[static_cast<size_t>(i)] -= linear * amplitude * s;
                mean += y[static_cast<size_t>(i)];
            }
            mean /= n;
            double square = 0.0;
            for (const double v : y)
                square += (v - mean) * (v - mean);
            const double rms = std::sqrt(square / n);
            auto& entry = table[static_cast<size_t>(c)][static_cast<size_t>(d)];
            entry.linear = static_cast<float>(linear);
            entry.level = static_cast<float>(rms > 1.0e-9 ? 1.0 / rms : 0.0);
        }
    return table;
}

const CalibrationTable calibrationTable = buildCalibration();

Calibration calibrationAt(int curveIndex, float driveDb) noexcept
{
    const float at = std::clamp(driveDb, 0.0f, maxDriveDb);
    const int i = std::min(driveSteps - 2, static_cast<int>(at));
    const float t = at - static_cast<float>(i);
    const auto& row =
        calibrationTable[static_cast<size_t>(std::clamp(curveIndex, 0, static_cast<int>(bassCurve)))];
    const auto& a = row[static_cast<size_t>(i)];
    const auto& b = row[static_cast<size_t>(i + 1)];
    return {a.linear + (b.linear - a.linear) * t, a.level + (b.level - a.level) * t};
}

// ----- 2x oversampling ----------------------------------------------------------------------
//
// The shared polyphase IIR half-band (plugins/Oversampler.h): 8 coefficients, a
// transition band of 0.04 of the doubled rate, so it passes flat to 20 kHz at
// 48 kHz and stops the images and the folded harmonics by 100 dB. Only the
// harmonics go through it; its small phase delay does not touch the dry signal.
const HalfBandDesign halfBand = designHalfBand(8, 0.04);

// ----- the stereo decorrelator ----------------------------------------------------------------

// A Schroeder all-pass: a flat response with a phase that wanders across the
// band, so three in a row make a copy of the harmonics that is uncorrelated with
// them but just as bright.
struct Diffuser
{
    std::vector<float> buffer;
    size_t at = 0;
    static constexpr float gain = 0.6f;

    void prepare(double sampleRate, double ms)
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("exciter Diffuser::prepare");
        buffer.assign(static_cast<size_t>(std::max(1.0, std::round(sampleRate * ms * 0.001))), 0.0f);
        at = 0;
    }

    float process(float in) noexcept
    {
        const float delayed = buffer[at];
        const float out = -gain * in + delayed;
        buffer[at] = in + gain * out;
        if (++at == buffer.size())
            at = 0;
        return out;
    }
};

float smoothstep(float t) noexcept
{
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::vector<engine::ParameterInfo> parameterInfos()
{
    std::vector<engine::ParameterInfo> p(count);
    p[frequency] = {"Frequency", "Hz", minFrequency, maxFrequency, 3500.0f, true};
    p[drive] = {"Drive", "dB", 0.0f, maxDriveDb, 8.0f, false};
    p[character] = choice("Character", {"Tube", "Tape", "Air", "Crisp"}, static_cast<float>(tube));
    p[amount] = {"Amount", "%", 0.0f, 100.0f, 35.0f, false};
    p[focus] = {"Focus", "%", -100.0f, 100.0f, 0.0f, false};
    p[dynamics] = {"Dynamics", "%", -100.0f, 100.0f, 0.0f, false};
    p[transients] = {"Transients", "%", 0.0f, 100.0f, 0.0f, false};
    p[stereo] = {"Stereo", "%", 0.0f, 100.0f, 0.0f, false};
    p[bassFrequency] = {"Bass Freq", "Hz", minBassFrequency, maxBassFrequency, 90.0f, true};
    p[bass] = {"Bass", "%", 0.0f, 100.0f, 0.0f, false};
    p[mix] = {"Mix", "%", 0.0f, 100.0f, 100.0f, false};
    p[output] = {"Output", "dB", -18.0f, 12.0f, 0.0f, false};
    p[autoGain] = choice("Auto Gain", {"Off", "On"}, 1.0f);
    return p;
}

// ----- the processor ------------------------------------------------------------------------
//
// Per sample: the band above the crossover (a 24 dB/oct Linkwitz-Riley
// high-pass), normalised by its own level (an 8 ms mean square guarded by the
// peak, the two sides linked) and driven into the curve at twice the rate with ADAA1; the curve's
// linear part is taken out, a high-pass at the crossover (an octave above for
// Air) clears the DC and the low intermodulation, a tilt sets the Focus, and the
// band's envelope, the Amount, the dynamics and the gate put the harmonics back
// at their level under the band. Bass: the same from the mono sum under the bass
// frequency, at the base rate, its harmonics kept between the bass frequency and
// eight times it. The control values (envelope ratios, coefficients, smoothed
// parameters) are worked out every 16 samples and ramp across them.
class Exciter final : public FeedbackBuiltIn
{
public:
    Exciter() : FeedbackBuiltIn(type, parameterInfos(), true) {}

private:
    static constexpr int controlInterval = 16;
    static constexpr float smoothingSeconds = 0.03f;
    // The bands' levels: an unbiased mean square over these (a follower with a
    // faster attack than release would read a steady tone high, and the curve's
    // linear part would no longer cancel), guarded by the peak (peakShare).
    static constexpr float bandSeconds = 0.008f, lowSeconds = 0.03f;
    static constexpr float peakShare = 0.4f;

    struct Channel
    {
        std::array<Biquad, 2> band; // the harmonics band
        Biquad post, tilt;          // after the curve
        HalfBandChains up, down;
        double previous = 0.0, previousIntegral = 0.0; // ADAA, at the doubled rate
        std::array<Biquad, 2> weightIn, weightWet;     // K-weighting for the auto gain
    };

    void prepareBuiltIn(double sampleRate, int maxBlockSize) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Exciter::prepareBuiltIn");
        const auto size = static_cast<size_t>(std::max(1, maxBlockSize));
        for (auto* v : {&inLeft_, &inRight_, &addLeft_, &addRight_, &scratchLeft_, &scratchRight_})
            v->assign(size, 0.0f);
        const std::array<double, 3> diffuserMs{2.3, 3.7, 5.3};
        for (size_t i = 0; i < diffusers_.size(); ++i)
            diffusers_[i].prepare(sampleRate, diffuserMs[i]);
        for (auto& ch : channels_)
        {
            ch = {};
            ch.weightIn[0].setHighShelf(sampleRate, 1681.0, 4.0);
            ch.weightIn[1].setHighPass(sampleRate, 38.0, 0.5);
            ch.weightWet = ch.weightIn;
        }
        for (auto* chain : {&bassLow_, &bassPost_})
            for (auto& b : *chain)
                b = {};
        bassTop_ = {};
        bassPrevious_ = bassPreviousIntegral_ = 0.0;

        const auto step = [&](float seconds)
        { return dsp::stepForSeconds(seconds, static_cast<float>(sampleRate)); };
        bandStep_ = step(bandSeconds);
        bandPeakFall_ = step(0.06f);
        lowStep_ = step(lowSeconds);
        lowPeakFall_ = step(0.1f);
        fastAttack_ = step(0.0003f);
        fastRelease_ = step(0.025f);
        slowAttack_ = step(0.012f);
        slowRelease_ = step(0.12f);
        weightStep_ = step(0.8f);
        rmsStep_ = step(rmsSeconds);

        bandMs_ = slowMs_ = lowMs_ = bandPeak_ = lowPeak_ = 0.0f;
        fastPeak_ = slowPeak_ = 0.0f;
        inWeightedMs_ = wetWeightedMs_ = 0.0f;
        inMs_ = addMs_ = 0.0f;
        compensationDb_ = 0.0f;

        // Start at the current settings, without ramps.
        frequency_ = parameter(frequency);
        bassFrequency_ = parameter(bassFrequency);
        drive_ = parameter(drive);
        focus_ = parameter(focus);
        dynamics_ = parameter(dynamics);
        transients_ = parameter(transients);
        stereo_ = parameter(stereo) * 0.01f;
        amount_ = amountGain(parameter(amount));
        bass_ = bassGain(parameter(bass));
        mix_ = parameter(mix) * 0.01f;
        output_ = parameter(output);
        character_ = currentCharacter();
        fade_ = 1.0f;
        inGain_ = harmonicScale_ = bassInGain_ = bassScale_ = width_ = 0.0f;
        outGain_ = dbToGain(output_);
        designedFrequency_ = designedBass_ = designedFocus_ = -1.0f;
        designedCharacter_ = -1;
        designFilters();
    }

    int currentCharacter() const noexcept
    {
        return std::clamp(static_cast<int>(std::lround(parameter(character))), 0, characterCount - 1);
    }

    // The filters for the smoothed frequencies, focus and character (only when they moved).
    void designFilters() noexcept
    {
        const double nyquistGuard = sampleRate_ * 0.45;
        if (std::abs(frequency_ - designedFrequency_) > 1.0e-4f * frequency_ ||
            character_ != designedCharacter_ || std::abs(focus_ - designedFocus_) > 1.0e-4f)
        {
            designedFrequency_ = frequency_;
            designedCharacter_ = character_;
            designedFocus_ = focus_;
            const double hz = std::min<double>(frequency_, nyquistGuard);
            const double above = character_ == air ? 2.0 : 1.0;
            Biquad hp, post, tilt;
            hp.setHighPass(sampleRate_, hz);
            post.setHighPass(sampleRate_, std::min(hz * above, nyquistGuard));
            tilt.setHighShelf(sampleRate_,
                              std::clamp(3.0 * hz * above, 1000.0, sampleRate_ * 0.3),
                              static_cast<double>(focus_) * 0.08);
            for (auto& ch : channels_)
            {
                for (auto& b : ch.band)
                    b.blend(hp, hp, 1.0f);
                ch.post.blend(post, post, 1.0f);
                ch.tilt.blend(tilt, tilt, 1.0f);
            }
        }
        if (std::abs(bassFrequency_ - designedBass_) > 1.0e-4f * bassFrequency_)
        {
            designedBass_ = bassFrequency_;
            Biquad lp, hp, top;
            lp.setLowPass(sampleRate_, bassFrequency_);
            hp.setHighPass(sampleRate_, bassFrequency_);
            top.setLowPass(sampleRate_, std::min(8.0 * bassFrequency_, 4000.0));
            for (auto& b : bassLow_)
                b.blend(lp, lp, 1.0f);
            for (auto& b : bassPost_)
                b.blend(hp, hp, 1.0f);
            bassTop_.blend(top, top, 1.0f);
        }
    }

    // The control values for the next `n` samples: smoothed parameters, then
    // the targets the per-sample gains ramp to.
    struct Targets
    {
        float inGain, harmonicScale, bassInGain, bassScale, width, outGain;
    };

    Targets control(int n) noexcept
    {
        const float fs = static_cast<float>(sampleRate_);
        const float k = dsp::blockStepForSeconds(smoothingSeconds, fs, static_cast<float>(n));
        // Close enough lands exactly, so a control at zero adds exactly nothing.
        const auto smooth = [k](float& value, float target)
        {
            value += (target - value) * k;
            if (std::abs(target - value) <= 1.0e-4f * std::max(1.0f, std::abs(target)))
                value = target;
        };
        // Frequencies glide on a log scale.
        frequency_ *= std::pow(parameter(frequency) / frequency_, k);
        bassFrequency_ *= std::pow(parameter(bassFrequency) / bassFrequency_, k);
        smooth(drive_, parameter(drive));
        smooth(focus_, parameter(focus));
        smooth(dynamics_, parameter(dynamics));
        smooth(transients_, parameter(transients));
        smooth(stereo_, parameter(stereo) * 0.01f);
        smooth(amount_, amountGain(parameter(amount)));
        smooth(bass_, bassGain(parameter(bass)));
        smooth(mix_, parameter(mix) * 0.01f);
        smooth(output_, parameter(output));

        // A new character: the harmonics fade out over 5 ms, the curve changes,
        // and they fade back in.
        const float fadeStep = static_cast<float>(n) / (fs * 0.005f);
        const int wanted = currentCharacter();
        if (wanted != character_)
        {
            fade_ = std::max(0.0f, fade_ - fadeStep);
            if (fade_ == 0.0f)
            {
                character_ = wanted;
                for (auto& ch : channels_)
                    ch.previousIntegral = curveIntegral(character_, ch.previous);
            }
        }
        else
            fade_ = std::min(1.0f, fade_ + fadeStep);
        designFilters();

        // The band's level against its 600 ms average, for the dynamics.
        slowMs_ += (bandMs_ - slowMs_) * dsp::blockStepForSeconds(0.6f, fs, static_cast<float>(n));

        const auto cal = calibrationAt(character_, drive_);
        linear_ = cal.linear;
        const float floor = referenceRms * dbToGain(-maxNormaliseDb);
        // The level the band is normalised by: its RMS, but never under a share
        // of its recent peak, so an onset does not overdrive the curve while the
        // RMS catches up.
        const float bandRms = std::max(std::sqrt(bandMs_), peakShare * bandPeak_);
        const float bandDb = 10.0f * std::log10(bandMs_ + 1.0e-30f);
        const float gate = smoothstep((bandDb - gateFloorDb) / gateRangeDb);
        const float awayDb = std::clamp(bandDb - 10.0f * std::log10(slowMs_ + 1.0e-30f), -24.0f, 12.0f);
        const float dynamicDb =
            std::clamp(dynamics_ * 0.01f * dynamicsSlope * awayDb, dynamicsMinDb, dynamicsMaxDb);
        const float onsetDb = std::clamp(
            20.0f * std::log10((fastPeak_ + 1.0e-9f) / (slowPeak_ + 1.0e-9f)), 0.0f, transientMaxDb);
        const float transientDb = transients_ * 0.01f * onsetDb;
        float harmonics = 0.0f;
        if (amount_ > 0.0f)
            harmonics =
                amount_ * dbToGain(std::min(dynamicDb + transientDb, maxHarmonicsDb - gainToDb(amount_)));
        // The tilt's shelf lifts above its corner; half of it comes off everywhere
        // so Focus tilts the harmonics about their middle rather than making them louder.
        const float tiltTrim = dbToGain(-focus_ * 0.04f);

        Targets t{};
        drivenRms_ = referenceRms * dbToGain(drive_);
        bassDrivenRms_ = referenceRms * dbToGain(bassDriveDb);
        t.inGain = drivenRms_ / std::max(bandRms, floor);
        t.harmonicScale = cal.level * bandRms * harmonics * gate * fade_ * mix_ * tiltTrim;

        const auto bassCal = calibrationAt(bassCurve, bassDriveDb);
        bassLinear_ = bassCal.linear;
        const float lowRms = std::max(std::sqrt(lowMs_), peakShare * lowPeak_);
        const float lowDb = 10.0f * std::log10(lowMs_ + 1.0e-30f);
        t.bassInGain = bassDrivenRms_ / std::max(lowRms, floor);
        t.bassScale = bassCal.level * lowRms * bass_ * smoothstep((lowDb - gateFloorDb) / gateRangeDb) * mix_;
        t.width = stereo_ * stereoDepth;

        // Auto gain: the K-weighted loudness of the output toward the input's, only
        // ever down, held through silence.
        float target = 0.0f;
        if (parameter(autoGain) >= 0.5f)
        {
            target = heldCompensationDb_;
            if (inWeightedMs_ > 1.0e-7f)
                target = std::clamp(10.0f * std::log10(inWeightedMs_ / std::max(wetWeightedMs_, 1.0e-30f)),
                                    -autoGainRangeDb,
                                    0.0f);
            heldCompensationDb_ = target;
        }
        compensationDb_ +=
            (target - compensationDb_) * dsp::blockStepForSeconds(0.25f, fs, static_cast<float>(n));
        t.outGain = dbToGain(compensationDb_ + output_);
        return t;
    }

    // One step of a curve with first-order antiderivative anti-aliasing, the
    // curve's linear part taken out.
    static float
    shape(int curveIndex, float linear, double x, double& previous, double& previousIntegral) noexcept
    {
        const double integral = curveIntegral(curveIndex, x);
        const double middle = 0.5 * (x + previous);
        const double y = dsp::adaa1(x,
                                    previous,
                                    integral,
                                    previousIntegral,
                                    1.0e-6,
                                    [curveIndex](double m) { return curve(curveIndex, m); });
        previous = x;
        previousIntegral = integral;
        return static_cast<float>(y - static_cast<double>(linear) * middle);
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        float* io[2] = {left, right};
        for (int start = 0; start < numSamples; start += controlInterval)
        {
            const int n = std::min(controlInterval, numSamples - start);
            const auto t = control(n);
            const float steps = static_cast<float>(n);
            const float inGainStep = (t.inGain - inGain_) / steps;
            const float harmonicStep = (t.harmonicScale - harmonicScale_) / steps;
            const float bassInStep = (t.bassInGain - bassInGain_) / steps;
            const float bassStep = (t.bassScale - bassScale_) / steps;
            const float widthStep = (t.width - width_) / steps;
            const float outStep = (t.outGain - outGain_) / steps;
            // Silent harmonics skip the curve (the band and its envelope still run).
            const bool harmonicsOn = t.harmonicScale > 0.0f || harmonicScale_ > 0.0f;
            const bool bassOn = t.bassScale > 0.0f || bassScale_ > 0.0f;
            for (int j = 0; j < n; ++j)
            {
                const auto i = static_cast<size_t>(start + j);
                inGain_ += inGainStep;
                harmonicScale_ += harmonicStep;
                bassInGain_ += bassInStep;
                bassScale_ += bassStep;
                width_ += widthStep;
                outGain_ += outStep;

                const float x[2] = {left[i], right[i]};
                inLeft_[i] = x[0];
                inRight_[i] = x[1];
                float band[2], h[2] = {0.0f, 0.0f};
                for (size_t c = 0; c < 2; ++c)
                {
                    auto& ch = channels_[c];
                    band[c] = ch.band[1].process(ch.band[0].process(x[c]));
                }
                // The envelopes: the band's level (linked), the input's onsets.
                const float square = 0.5f * (band[0] * band[0] + band[1] * band[1]);
                bandMs_ += (square - bandMs_) * bandStep_;
                bandPeak_ = std::max(std::max(std::abs(band[0]), std::abs(band[1])),
                                     bandPeak_ - bandPeak_ * bandPeakFall_);
                const float peak = std::max(std::abs(x[0]), std::abs(x[1]));
                fastPeak_ += (peak - fastPeak_) * (peak > fastPeak_ ? fastAttack_ : fastRelease_);
                slowPeak_ += (peak - slowPeak_) * (peak > slowPeak_ ? slowAttack_ : slowRelease_);
                if (harmonicsOn)
                {
                    // The peak bounds the normalisation at every sample, so the curve
                    // never sees more than a fixed multiple of its calibrated level,
                    // however fast a sound starts.
                    const float gain =
                        std::min(inGain_, drivenRms_ / std::max(peakShare * bandPeak_, 1.0e-12f));
                    for (size_t c = 0; c < 2; ++c)
                    {
                        auto& ch = channels_[c];
                        // Up: the two outputs in time order. Down: the later sample
                        // on the first path.
                        float first = band[c] * gain, second = first;
                        ch.up.run(halfBand, first, second);
                        float a = shape(character_, linear_, first, ch.previous, ch.previousIntegral);
                        float b = shape(character_, linear_, second, ch.previous, ch.previousIntegral);
                        ch.down.run(halfBand, b, a);
                        const float y = 0.5f * (a + b);
                        h[c] = ch.tilt.process(ch.post.process(y)) * harmonicScale_;
                    }
                }

                // Stereo: a decorrelated copy of the harmonics' middle, added to the
                // left and taken from the right, so the mono sum never changes.
                float diffused = 0.5f * (h[0] + h[1]);
                for (auto& d : diffusers_)
                    diffused = d.process(diffused);
                h[0] += width_ * diffused;
                h[1] -= width_ * diffused;

                // Bass: harmonics of the mono low band, the same on both sides.
                const float low = bassLow_[1].process(bassLow_[0].process(0.5f * (x[0] + x[1])));
                lowMs_ += (low * low - lowMs_) * lowStep_;
                lowPeak_ = std::max(std::abs(low), lowPeak_ - lowPeak_ * lowPeakFall_);
                float b = 0.0f;
                if (bassOn)
                {
                    const float gain =
                        std::min(bassInGain_, bassDrivenRms_ / std::max(peakShare * lowPeak_, 1.0e-12f));
                    const float shaped =
                        shape(bassCurve, bassLinear_, low * gain, bassPrevious_, bassPreviousIntegral_);
                    b = bassTop_.process(bassPost_[1].process(bassPost_[0].process(shaped))) * bassScale_;
                }

                float added[2], out[2];
                float inWeighted = 0.0f, wetWeighted = 0.0f;
                for (size_t c = 0; c < 2; ++c)
                {
                    added[c] = h[c] + b;
                    const float wet = x[c] + added[c];
                    auto& ch = channels_[c];
                    const float wi = ch.weightIn[1].process(ch.weightIn[0].process(x[c]));
                    const float ww = ch.weightWet[1].process(ch.weightWet[0].process(wet));
                    inWeighted += 0.5f * wi * wi;
                    wetWeighted += 0.5f * ww * ww;
                    out[c] = wet * outGain_;
                    io[c][i] = out[c];
                }
                inWeightedMs_ += (inWeighted - inWeightedMs_) * weightStep_;
                wetWeightedMs_ += (wetWeighted - wetWeightedMs_) * weightStep_;
                addLeft_[i] = added[0];
                addRight_[i] = added[1];

                const float inSquare = 0.5f * (x[0] * x[0] + x[1] * x[1]);
                const float addSquare = 0.5f * (added[0] * added[0] + added[1] * added[1]);
                inMs_ += (inSquare - inMs_) * rmsStep_;
                addMs_ += (addSquare - addMs_) * rmsStep_;
                feedback_.addSample(peak,
                                    std::max(std::abs(out[0]), std::abs(out[1])),
                                    std::max(0.0f, -compensationDb_),
                                    std::sqrt(inMs_),
                                    std::sqrt(addMs_));
            }
            inGain_ = t.inGain;
            harmonicScale_ = t.harmonicScale;
            bassInGain_ = t.bassInGain;
            bassScale_ = t.bassScale;
            width_ = t.width;
            outGain_ = t.outGain;
        }
        feedback_.tap(0, inLeft_.data(), inRight_.data(), numSamples);
        feedback_.tap(1, addLeft_.data(), addRight_.data(), numSamples);
        denormalGuard();
    }

    void denormalGuard() noexcept
    {
        for (auto* v : {&bandMs_,
                        &slowMs_,
                        &lowMs_,
                        &bandPeak_,
                        &lowPeak_,
                        &fastPeak_,
                        &slowPeak_,
                        &inWeightedMs_,
                        &wetWeightedMs_,
                        &inMs_,
                        &addMs_})
            if (*v < 1.0e-25f)
                *v = 0.0f;
        for (auto& ch : channels_)
        {
            for (auto& b : ch.band)
                b.flushDenormals();
            ch.post.flushDenormals();
            ch.tilt.flushDenormals();
            for (auto& b : ch.weightIn)
                b.flushDenormals();
            for (auto& b : ch.weightWet)
                b.flushDenormals();
            ch.up.flush();
            ch.down.flush();
            if (std::abs(ch.previous) < 1.0e-20)
            {
                ch.previous = 0.0;
                ch.previousIntegral = curveIntegral(character_, 0.0);
            }
        }
        for (auto* chain : {&bassLow_, &bassPost_})
            for (auto& b : *chain)
                b.flushDenormals();
        bassTop_.flushDenormals();
        for (auto& d : diffusers_)
            for (auto& s : d.buffer)
                s = dsp::flush(s, 1.0e-20f);
    }

    // Bypassed, the processor keeps measuring while an editor is watching, so
    // the display shows what it would add; the audio passes untouched.
    bool bypassedBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        if (!feedback_.watched())
            return true; // nothing to measure for
        std::copy(left, left + numSamples, scratchLeft_.data());
        std::copy(right, right + numSamples, scratchRight_.data());
        processAudio(scratchLeft_.data(), scratchRight_.data(), numSamples);
        return true;
    }

    std::array<Channel, 2> channels_;
    std::array<Biquad, 2> bassLow_, bassPost_;
    Biquad bassTop_;
    double bassPrevious_ = 0.0, bassPreviousIntegral_ = 0.0;
    std::array<Diffuser, 3> diffusers_;
    std::vector<float> inLeft_, inRight_, addLeft_, addRight_, scratchLeft_, scratchRight_;

    // Envelopes.
    float bandMs_ = 0.0f, slowMs_ = 0.0f, lowMs_ = 0.0f, fastPeak_ = 0.0f, slowPeak_ = 0.0f;
    float bandPeak_ = 0.0f, lowPeak_ = 0.0f;
    float inWeightedMs_ = 0.0f, wetWeightedMs_ = 0.0f, inMs_ = 0.0f, addMs_ = 0.0f;
    float bandStep_ = 0.0f, bandPeakFall_ = 0.0f, lowStep_ = 0.0f, lowPeakFall_ = 0.0f;
    float fastAttack_ = 0.0f, fastRelease_ = 0.0f, slowAttack_ = 0.0f, slowRelease_ = 0.0f;
    float weightStep_ = 0.0f, rmsStep_ = 0.0f;
    float compensationDb_ = 0.0f, heldCompensationDb_ = 0.0f;

    // Smoothed parameters and the per-sample gains ramping between control steps.
    float frequency_ = 3500.0f, bassFrequency_ = 90.0f, drive_ = 0.0f, focus_ = 0.0f, dynamics_ = 0.0f,
          transients_ = 0.0f, stereo_ = 0.0f, amount_ = 0.0f, bass_ = 0.0f, mix_ = 1.0f, output_ = 0.0f;
    int character_ = tube;
    float fade_ = 1.0f, linear_ = 0.0f, bassLinear_ = 0.0f;
    float drivenRms_ = referenceRms, bassDrivenRms_ = referenceRms; // what the curves are driven to
    float inGain_ = 0.0f, harmonicScale_ = 0.0f, bassInGain_ = 0.0f, bassScale_ = 0.0f, width_ = 0.0f,
          outGain_ = 1.0f;
    float designedFrequency_ = -1.0f, designedBass_ = -1.0f, designedFocus_ = -1.0f;
    int designedCharacter_ = -1;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Exciter>();
}

} // namespace northlight::plugins::exciter
