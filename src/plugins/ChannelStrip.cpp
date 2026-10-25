#include "plugins/ChannelStrip.h"

#include "engine/Gain.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/ParamHelpers.h"
#include "plugins/ProcessorBase.h"
#include "plugins/dsp/Shapers.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace northlight::plugins::strip
{

const std::vector<std::string>& moduleTypes()
{
    static const std::vector<std::string> list{
        types::gate, types::eq, types::compressor, types::deesser, types::saturation, types::limiter};
    return list;
}

bool isStripType(const std::string& type)
{
    return type.rfind("northlight.strip.", 0) == 0;
}

std::string displayName(const std::string& type)
{
    if (type == types::gate)
        return "Gate";
    if (type == types::eq)
        return "EQ";
    if (type == types::compressor)
        return "Compressor";
    if (type == types::deesser)
        return "De-esser";
    if (type == types::saturation)
        return "Saturation";
    if (type == types::limiter)
        return "Limiter";
    if (type == types::opto)
        return "Opto compressor";
    return type;
}

std::string slotOf(const std::string& type)
{
    return type == types::opto ? types::compressor : type;
}

std::vector<std::string> alternatives(const std::string& type)
{
    if (slotOf(type) == types::compressor)
        return {types::compressor, types::opto};
    return {type};
}

std::string modelName(const std::string& type)
{
    if (type == types::compressor)
        return "Clean";
    if (type == types::opto)
        return "Opto";
    return displayName(type);
}

// ----- curves shared with the editors ---------------------------------------------------------

namespace eq
{
bool isCut(int band) noexcept
{
    return band == lowCut || band == highCut;
}

int paramIndex(int band, Field field) noexcept
{
    if (band < 0 || band >= bandCount)
        return -1;
    if (band == lowCut)
        return field <= frequency ? static_cast<int>(field) : -1;
    if (band == highCut)
        return field <= frequency ? 23 + static_cast<int>(field) : -1;
    return 3 + (band - band1) * 5 + static_cast<int>(field);
}

namespace
{
// Butterworth section Qs for an even order (pairs of poles), and for an odd
// order the second-order sections after the first-order one.
void cutQs(int slopeDb, bool& firstOrder, double* qs, int& sections) noexcept
{
    firstOrder = false;
    sections = 0;
    switch (slopeDb)
    {
        case 6:
            firstOrder = true;
            break;
        case 12:
            qs[sections++] = 0.70711;
            break;
        case 18:
            firstOrder = true;
            qs[sections++] = 1.0;
            break;
        case 24:
            qs[sections++] = 0.54120;
            qs[sections++] = 1.30656;
            break;
        case 36:
            qs[sections++] = 0.51764;
            qs[sections++] = 0.70711;
            qs[sections++] = 1.93185;
            break;
        default: // 48
            qs[sections++] = 0.50980;
            qs[sections++] = 0.60134;
            qs[sections++] = 0.89998;
            qs[sections++] = 2.56292;
            break;
    }
}

// The fixed slot layout: four for each cut, one for each parametric band.
constexpr int slotOf(int band) noexcept
{
    return band == lowCut ? 0 : band == highCut ? 8 : 4 + (band - band1);
}
} // namespace

int design(const float* params, double sampleRate, Biquad* out, int band) noexcept
{
    for (int s = 0; s < maxSections; ++s)
        out[s].bypass();
    int active = 0;
    for (int b = 0; b < bandCount; ++b)
    {
        if (band >= 0 && b != band)
            continue;
        if (params[paramIndex(b, on)] < 0.5f)
            continue;
        const double hz = params[paramIndex(b, frequency)];
        auto* slot = out + slotOf(b);
        if (isCut(b))
        {
            const int choice = std::clamp(static_cast<int>(std::lround(params[paramIndex(b, type)])),
                                          0,
                                          static_cast<int>(slopes.size()) - 1);
            bool firstOrder = false;
            double qs[4]{};
            int sections = 0;
            cutQs(slopes[static_cast<size_t>(choice)], firstOrder, qs, sections);
            int k = 0;
            if (firstOrder)
            {
                if (b == lowCut)
                    slot[k++].setHighPass1(sampleRate, hz);
                else
                    slot[k++].setLowPass1(sampleRate, hz);
            }
            for (int i = 0; i < sections; ++i)
            {
                if (b == lowCut)
                    slot[k++].setHighPass(sampleRate, hz, qs[i]);
                else
                    slot[k++].setLowPass(sampleRate, hz, qs[i]);
            }
            active += k;
            continue;
        }
        const double gainDb = params[paramIndex(b, gain)];
        const double q = params[paramIndex(b, Field::q)];
        switch (static_cast<int>(std::lround(params[paramIndex(b, type)])))
        {
            case lowShelf:
                slot->setLowShelf(sampleRate, hz, gainDb, q);
                break;
            case highShelf:
                slot->setHighShelf(sampleRate, hz, gainDb, q);
                break;
            case notch:
                slot->setNotch(sampleRate, hz, q);
                break;
            default:
                slot->setPeaking(sampleRate, hz, q, gainDb);
                break;
        }
        if (!slot->isIdentity())
            ++active;
    }
    return active;
}

double responseDb(const float* params, double sampleRate, double hz, int band) noexcept
{
    Biquad sections[maxSections];
    design(params, sampleRate, sections, band);
    double magnitude = 1.0;
    for (const auto& s : sections)
        if (!s.isIdentity())
            magnitude *= s.magnitude(hz, sampleRate);
    double db = engine::gainToDb(magnitude, engine::magnitudeFloorDb);
    if (band < 0)
        db += params[output];
    return db;
}
} // namespace eq

namespace compressor
{
// Not dsp::softKneeReductionDb (Glue's and Gravity's): the knee here includes
// its top edge, where the quadratic and the straight part agree only to within
// rounding, so sharing it would move the output there; and a knee of 0 is hard.
float gainReductionDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept
{
    const float slope = 1.0f - 1.0f / std::max(1.0f, ratio);
    const float over = levelDb - thresholdDb;
    if (kneeDb > 0.0f && std::abs(over) <= kneeDb * 0.5f)
    {
        const float x = over + kneeDb * 0.5f;
        return slope * x * x / (2.0f * kneeDb);
    }
    return over > 0.0f ? slope * over : 0.0f;
}

float autoMakeupDb(float thresholdDb, float ratio) noexcept
{
    // Half the reduction a full-scale signal gets: loud passages stay about as
    // loud, quiet ones come up.
    return 0.5f * gainReductionDb(0.0f, thresholdDb, ratio, 0.0f);
}
} // namespace compressor

namespace opto
{
namespace
{
constexpr float kneeDb = 2.5f; // the softplus knee's width

float softplus(float x) noexcept
{
    const float t = x / kneeDb;
    return t > 20.0f ? x : kneeDb * std::log1p(std::exp(t));
}
} // namespace

float sidechainDriveDb(float peakReduction) noexcept
{
    // 0% hears the output 5 dB down (nothing a mix reaches compresses); 100%
    // lifts it 45 dB (peaks around -45 dBFS start to compress).
    return 0.5f * std::clamp(peakReduction, 0.0f, 100.0f) - 5.0f;
}

float loopGain(int mode) noexcept
{
    // About 3:1 compressing. Limiting, the loop settles in the knee, so a loop
    // gain of 12 gives about 10:1.
    return mode == 1 ? 12.0f : 2.0f;
}

float cellTargetDb(float detectorDb, float peakReduction, int mode) noexcept
{
    return loopGain(mode) * softplus(detectorDb + sidechainDriveDb(peakReduction));
}

float steadyReductionDb(float inputDb, float peakReduction, int mode) noexcept
{
    // The fixed point of r = target(input - r): r - target(input - r) rises
    // with r, so bisect it.
    float lo = 0.0f, hi = 96.0f;
    for (int i = 0; i < 40; ++i)
    {
        const float r = 0.5f * (lo + hi);
        (r - cellTargetDb(inputDb - r, peakReduction, mode) > 0.0f ? hi : lo) = r;
    }
    return 0.5f * (lo + hi);
}

float slowShare(float memory) noexcept
{
    return 0.35f + 0.4f * std::clamp(memory, 0.0f, 1.0f);
}

float slowReleaseSeconds(float memory) noexcept
{
    return 0.4f + 3.6f * std::clamp(memory, 0.0f, 1.0f);
}
} // namespace opto

namespace deesser
{
float reductionDb(float bandDb, float thresholdDb, float rangeDb) noexcept
{
    // 5:1 above the threshold, capped at the range.
    return std::clamp((bandDb - thresholdDb) * 0.8f, 0.0f, rangeDb);
}
} // namespace deesser

namespace saturation
{
namespace
{
constexpr double tubeBias = 0.25;
const double tanhTubeBias = std::tanh(tubeBias); // the curve's offset, computed once

double shapeD(int type, double x) noexcept
{
    switch (type)
    {
        case tube:
            return dsp::tube(x, tubeBias, tanhTubeBias);
        case clip:
            return std::abs(x) <= 1.0 ? 1.5 * (x - x * x * x / 3.0) : (x > 0.0 ? 1.0 : -1.0);
        default:
            return std::tanh(x);
    }
}

// The antiderivative of shapeD, for first-order antiderivative anti-aliasing.
double antiderivative(int type, double x) noexcept
{
    switch (type)
    {
        case tube:
            return dsp::tubeIntegral(x, tubeBias, tanhTubeBias);
        case clip:
            return std::abs(x) <= 1.0 ? 1.5 * (x * x / 2.0 - x * x * x * x / 12.0) : std::abs(x) - 0.375;
        default:
            return dsp::logCosh(x);
    }
}
} // namespace

float shape(int type, float x) noexcept
{
    return static_cast<float>(shapeD(type, x));
}

float driveCompensationDb(float driveDb) noexcept
{
    return -0.5f * driveDb;
}
} // namespace saturation

// ----- processors -----------------------------------------------------------------------------

namespace
{
float coefficient(double sampleRate, float ms) noexcept
{
    return dsp::poleForMs(std::max(0.001f, ms), static_cast<float>(sampleRate));
}

// Every module is a FeedbackBuiltIn (plugins/MeteredBuiltIn.h): bypassed, a
// watched module measures its input, so the threshold can be set against the
// signal before it is switched on.

// ----- gate -----------------------------------------------------------------------------------

std::vector<engine::ParameterInfo> gateParameters()
{
    std::vector<engine::ParameterInfo> p(gate::count);
    p[gate::threshold] = {"Threshold", "dB", -80.0f, 0.0f, -40.0f, false};
    p[gate::range] = {"Range", "dB", 0.0f, 80.0f, 40.0f, false};
    p[gate::attack] = {"Attack", "ms", 0.05f, 50.0f, 0.5f, true};
    p[gate::hold] = {"Hold", "ms", 0.0f, 500.0f, 30.0f, false};
    p[gate::release] = {"Release", "ms", 5.0f, 2000.0f, 120.0f, true};
    p[gate::keyHighPass] = {"Key HP", "Hz", 20.0f, 2000.0f, 20.0f, true};
    p[gate::keyLowPass] = {"Key LP", "Hz", 1000.0f, 20000.0f, 20000.0f, true};
    return p;
}

// A key-filtered peak detector opens the gate above the threshold; it closes
// after the hold time once the key falls `hysteresisDb` below it. The gain
// moves between unity and -range at the attack and release rates.
class Gate final : public FeedbackBuiltIn
{
public:
    Gate() : FeedbackBuiltIn(types::gate, gateParameters()) {}

private:
    void prepareBuiltIn(double, int) override
    {
        for (auto& f : filters_)
            f.reset();
        envelope_ = 0.0f;
        gain_ = 1.0f;
        open_ = true;
        holdLeft_ = 0;
        lastHighPass_ = lastLowPass_ = -1.0f;
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float threshold = parameter(gate::threshold);
        // The detector is compared as a level, not in dB: no log per sample.
        const float openLevel = dbToGain(threshold);
        const float closeLevel = dbToGain(threshold - gate::hysteresisDb);
        const float closedGain = dbToGain(-parameter(gate::range));
        const float attack = 1.0f - coefficient(sampleRate_, parameter(gate::attack));
        const float release = 1.0f - coefficient(sampleRate_, parameter(gate::release));
        const int hold = static_cast<int>(sampleRate_ * parameter(gate::hold) * 0.001);
        const float detectorFall = coefficient(sampleRate_, 10.0f);
        const float highPass = parameter(gate::keyHighPass);
        const float lowPass = parameter(gate::keyLowPass);
        const bool useHighPass = highPass > 20.5f;
        const bool useLowPass = lowPass < 19999.0f;
        if (highPass != lastHighPass_ || lowPass != lastLowPass_)
        {
            lastHighPass_ = highPass;
            lastLowPass_ = lowPass;
            for (int ch = 0; ch < 2; ++ch)
            {
                filters_[static_cast<size_t>(ch * 2)].setHighPass(sampleRate_, highPass);
                filters_[static_cast<size_t>(ch * 2 + 1)].setLowPass(sampleRate_, lowPass);
            }
        }
        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = left[i], inR = right[i];
            float keyL = inL, keyR = inR;
            if (useHighPass)
            {
                keyL = filters_[0].process(keyL);
                keyR = filters_[2].process(keyR);
            }
            if (useLowPass)
            {
                keyL = filters_[1].process(keyL);
                keyR = filters_[3].process(keyR);
            }
            const float key = std::max(std::abs(keyL), std::abs(keyR));
            envelope_ = std::max(key, envelope_ * detectorFall);
            if (envelope_ > openLevel)
            {
                open_ = true;
                holdLeft_ = hold;
            }
            else if (open_ && envelope_ < closeLevel)
            {
                if (holdLeft_ > 0)
                    --holdLeft_;
                else
                    open_ = false;
            }
            const float target = open_ ? 1.0f : closedGain;
            gain_ += (target - gain_) * (target > gain_ ? attack : release);
            left[i] = inL * gain_;
            right[i] = inR * gain_;
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
                feedback_.addSample(std::max(std::abs(inL), std::abs(inR)),
                                    std::max(std::abs(left[i]), std::abs(right[i])),
                                    std::max(0.0f, -gainToDb(gain_)),
                                    key);
        }
    }

    std::array<Biquad, 4> filters_; // high-pass and low-pass per channel
    float envelope_ = 0.0f, gain_ = 1.0f;
    bool open_ = true;
    int holdLeft_ = 0;
    float lastHighPass_ = -1.0f, lastLowPass_ = -1.0f;
};

// ----- EQ -------------------------------------------------------------------------------------

std::vector<engine::ParameterInfo> eqParameters()
{
    std::vector<engine::ParameterInfo> infos;
    const std::vector<std::string> slopeNames{"6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB"};
    const std::vector<std::string> shapes{"Peak", "Low shelf", "High shelf", "Notch"};
    const std::vector<std::string> offOn{"Off", "On"};
    infos.push_back(choice("Low cut", offOn, 0.0f));
    infos.push_back(choice("Low cut slope", slopeNames, 1.0f));
    infos.push_back({"Low cut freq", "Hz", 20.0f, 2000.0f, 80.0f, true});
    const float freqs[4] = {100.0f, 400.0f, 2500.0f, 8000.0f};
    const float shapeDefaults[4] = {1.0f, 0.0f, 0.0f, 2.0f};
    const float qs[4] = {0.71f, 1.0f, 1.0f, 0.71f};
    for (int b = 0; b < 4; ++b)
    {
        const auto n = "Band " + std::to_string(b + 1);
        infos.push_back(choice(n, offOn, 1.0f));
        infos.push_back(choice(n + " shape", shapes, shapeDefaults[b]));
        infos.push_back({n + " freq", "Hz", 20.0f, 20000.0f, freqs[b], true});
        infos.push_back({n + " gain", "dB", -24.0f, 24.0f, 0.0f, false});
        infos.push_back({n + " Q", "", 0.1f, 18.0f, qs[b], true});
    }
    infos.push_back(choice("High cut", offOn, 0.0f));
    infos.push_back(choice("High cut slope", slopeNames, 1.0f));
    infos.push_back({"High cut freq", "Hz", 1000.0f, 20000.0f, 16000.0f, true});
    infos.push_back({"Output", "dB", -24.0f, 24.0f, 0.0f, false});
    return infos;
}

// Six bands in a fixed layout of twelve sections (identity where a band is
// off, skipped). A parameter change ramps the coefficients across the block.
class SixBandEq final : public FeedbackBuiltIn
{
public:
    SixBandEq() : FeedbackBuiltIn(types::eq, eqParameters(), true) {}

private:
    void prepareBuiltIn(double, int) override
    {
        for (auto& channel : live_)
            for (auto& s : channel)
            {
                s.bypass();
                s.reset();
            }
        last_.fill(std::nanf(""));
        outputGain_ = dbToGain(parameter(eq::output));
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const bool watched = feedback_.watched();
        if (watched)
            feedback_.tap(0, left, right, numSamples);
        const float inPeak = watched ? peakOf(left, right, numSamples) : 0.0f;
        const bool ramp = updateCoefficients();
        bool active[eq::maxSections];
        for (int s = 0; s < eq::maxSections; ++s)
            active[s] = !live_[0][static_cast<size_t>(s)].isIdentity() ||
                        (ramp && !(previous_[static_cast<size_t>(s)].isIdentity() &&
                                   targets_[static_cast<size_t>(s)].isIdentity()));
        const float targetGain = dbToGain(parameter(eq::output));
        const float gainStep = (targetGain - outputGain_) / static_cast<float>(std::max(1, numSamples));
        float g = outputGain_;
        for (int i = 0; i < numSamples; ++i)
        {
            if (ramp)
            {
                const float t = static_cast<float>(i + 1) / static_cast<float>(numSamples);
                for (size_t s = 0; s < eq::maxSections; ++s)
                    if (active[s])
                    {
                        live_[0][s].blend(previous_[s], targets_[s], t);
                        live_[1][s].blend(previous_[s], targets_[s], t);
                    }
            }
            float l = left[i], r = right[i];
            for (size_t s = 0; s < eq::maxSections; ++s)
                if (active[s])
                {
                    l = live_[0][s].process(l);
                    r = live_[1][s].process(r);
                }
            g += gainStep;
            left[i] = l * g;
            right[i] = r * g;
        }
        outputGain_ = targetGain;
        if (ramp) // land exactly on the targets, so a band switched off reads as identity again
            for (auto& channel : live_)
                for (size_t s = 0; s < eq::maxSections; ++s)
                    channel[s].blend(targets_[s], targets_[s], 1.0f);
        if (watched)
        {
            feedback_.addBlock(inPeak, peakOf(left, right, numSamples), 0.0f, 0.0f, numSamples);
            feedback_.tap(1, left, right, numSamples);
        }
    }

    // Recomputes the targets when a parameter changed; true when this block
    // should ramp to them.
    bool updateCoefficients() noexcept
    {
        std::array<float, eq::count> params{};
        bool changed = false;
        for (int i = 0; i < eq::count; ++i)
        {
            params[static_cast<size_t>(i)] = parameter(i);
            changed = changed || params[static_cast<size_t>(i)] != last_[static_cast<size_t>(i)];
        }
        if (!changed)
            return false;
        const bool first = std::isnan(last_[0]);
        last_ = params;
        previous_ = live_[0];
        eq::design(params.data(), sampleRate_, targets_.data());
        for (size_t s = 0; s < eq::maxSections; ++s)
        {
            // A section switching on or off starts from (or ends at) its memory
            // cleared, so an old state never rings out of a re-enabled band.
            if (previous_[s].isIdentity() && !targets_[s].isIdentity())
            {
                live_[0][s].reset();
                live_[1][s].reset();
            }
        }
        if (first)
        {
            for (auto& channel : live_)
                for (size_t s = 0; s < eq::maxSections; ++s)
                    channel[s].blend(targets_[s], targets_[s], 1.0f);
            return false;
        }
        return true;
    }

    std::array<std::array<Biquad, eq::maxSections>, 2> live_;
    std::array<Biquad, eq::maxSections> previous_, targets_;
    std::array<float, eq::count> last_{};
    float outputGain_ = 1.0f;
};

// ----- compressor -----------------------------------------------------------------------------

std::vector<engine::ParameterInfo> compressorParameters()
{
    std::vector<engine::ParameterInfo> p(compressor::count);
    p[compressor::threshold] = {"Threshold", "dB", -60.0f, 0.0f, -18.0f, false};
    p[compressor::ratio] = {"Ratio", ":1", 1.0f, 20.0f, 3.0f, true};
    p[compressor::attack] = {"Attack", "ms", 0.1f, 100.0f, 10.0f, true};
    p[compressor::release] = {"Release", "ms", 10.0f, 2000.0f, 120.0f, true};
    p[compressor::knee] = {"Knee", "dB", 0.0f, 24.0f, 6.0f, false};
    p[compressor::makeup] = {"Makeup", "dB", 0.0f, 24.0f, 0.0f, false};
    p[compressor::autoMakeup] = choice("Auto makeup", {"Off", "On"}, 0.0f);
    p[compressor::detector] = choice("Detector", {"Peak", "RMS"}, 0.0f);
    p[compressor::mix] = {"Mix", "%", 0.0f, 100.0f, 100.0f, false};
    return p;
}

// Feed-forward, stereo-linked: a peak or RMS detector, the soft-knee curve on
// its level, the reduction smoothed with attack and release (smoothing the
// curve's output rather than the level keeps a waveform's troughs from
// dragging the envelope down), make-up (manual plus auto) and an internal
// wet/dry for parallel compression. The one compressor: the channel strip's
// module, and (keyed) the "northlight.compressor" insert, whose detector listens to
// a sidechain key when one is chosen.
class Compressor final : public FeedbackBuiltIn
{
public:
    explicit Compressor(const std::string& type = types::compressor, bool keyed = false)
        : FeedbackBuiltIn(type, compressorParameters()), keyed_(keyed)
    {
    }

    bool acceptsSidechain() const noexcept override { return keyed_; }

private:
    void prepareBuiltIn(double, int) override
    {
        reductionDb_ = 0.0f;
        displayLevel_ = 0.0f;
        meanSquare_ = 0.0f;
        makeup_ = currentMakeup();
    }

    float currentMakeup() const noexcept
    {
        float db = parameter(compressor::makeup);
        if (parameter(compressor::autoMakeup) >= 0.5f)
            db += compressor::autoMakeupDb(parameter(compressor::threshold), parameter(compressor::ratio));
        return dbToGain(db);
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float threshold = parameter(compressor::threshold);
        const float ratio = parameter(compressor::ratio);
        const float knee = parameter(compressor::knee);
        const float attack = coefficient(sampleRate_, parameter(compressor::attack));
        const float release = coefficient(sampleRate_, parameter(compressor::release));
        const bool rms = parameter(compressor::detector) >= 0.5f;
        const float rmsCoefficient = coefficient(sampleRate_, 10.0f);
        const float wet = parameter(compressor::mix) * 0.01f;
        const float targetMakeup = currentMakeup();
        const float makeupStep = (targetMakeup - makeup_) / static_cast<float>(std::max(1, numSamples));
        // The detector listens to the key when there is one, else to the audio.
        const float* detectL = keyed_ && keyLeft_ != nullptr ? keyLeft_ : left;
        const float* detectR = keyed_ && keyRight_ != nullptr ? keyRight_ : right;
        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = left[i], inR = right[i];
            const float keyL = detectL[i], keyR = detectR[i];
            float level = std::max(std::abs(keyL), std::abs(keyR));
            if (rms)
            {
                // Scaled so a sine reads its peak, like the peak detector.
                meanSquare_ = rmsCoefficient * meanSquare_ +
                              (1.0f - rmsCoefficient) * (keyL * keyL + keyR * keyR) * 0.5f;
                level = std::sqrt(2.0f * meanSquare_);
            }
            const float target = compressor::gainReductionDb(gainToDb(level), threshold, ratio, knee);
            const float a = target > reductionDb_ ? attack : release;
            reductionDb_ = a * reductionDb_ + (1.0f - a) * target;
            const float reduction = reductionDb_;
            // The level the editor puts on the curve: a peak envelope with the release.
            displayLevel_ = level > displayLevel_ ? level : level + (displayLevel_ - level) * release;
            makeup_ += makeupStep;
            const float g = dbToGain(-reduction) * makeup_;
            const float mixGain = (1.0f - wet) + wet * g;
            left[i] = inL * mixGain;
            right[i] = inR * mixGain;
            // The input meter shows what the detector hears: the key when there
            // is one (the threshold acts on it), else the audio.
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
                feedback_.addSample(std::max(std::abs(keyL), std::abs(keyR)),
                                    std::max(std::abs(left[i]), std::abs(right[i])),
                                    reduction,
                                    displayLevel_);
        }
        makeup_ = targetMakeup;
    }

    float reductionDb_ = 0.0f;
    float displayLevel_ = 0.0f;
    float meanSquare_ = 0.0f;
    float makeup_ = 1.0f;
    bool keyed_ = false;
};

// ----- opto compressor ------------------------------------------------------------------------

std::vector<engine::ParameterInfo> optoParameters()
{
    std::vector<engine::ParameterInfo> p(opto::count);
    p[opto::peakReduction] = {"Peak reduction", "%", 0.0f, 100.0f, 40.0f, false};
    p[opto::gain] = {"Gain", "dB", -12.0f, 24.0f, 0.0f, false};
    p[opto::mode] = choice("Mode", {"Compress", "Limit"}, 0.0f);
    p[opto::emphasis] = {"Emphasis", "%", 0.0f, 100.0f, 0.0f, false};
    p[opto::colour] = {"Colour", "%", 0.0f, 100.0f, 30.0f, false};
    p[opto::mix] = {"Mix", "%", 0.0f, 100.0f, 100.0f, false};
    return p;
}

// A model of an LA-2A style optical compressor, stereo-linked:
//  - Feedback: the detector hears the previous output sample (after the cell,
//    before the make-up), through an optional high shelf (Emphasis). A peak
//    envelope of that, lifted by Peak reduction, goes through a softplus knee
//    times the loop gain; the loop settles at a soft ratio of 1 + loop gain.
//  - The cell: a fast stage follows the target (attack about 30 ms in the
//    loop, which feedback shortens to about 10, release 60 ms). A slow stage
//    charges from it over 200 ms toward a share of it and releases over 0.4 to
//    4 s. Both the share and the slow release grow with the cell's memory,
//    which fills over seconds of heavy compression and drains slower still.
//    The reduction is the larger of the two.
//  - Make-up, then the output valve stage (Colour): the biased tanh with
//    first-order antiderivative anti-aliasing, unity gain for small signals,
//    and a DC blocker. Then the internal wet/dry.
// No lookahead, no latency.
class Opto final : public FeedbackBuiltIn
{
public:
    explicit Opto(const std::string& type = types::opto) : FeedbackBuiltIn(type, optoParameters()) {}

private:
    static constexpr double valveBias = 0.25; // saturation's tube curve
    static inline const double valveSlope = 1.0 - std::tanh(valveBias) * std::tanh(valveBias);
    static float valveDrive(float colourPercent) noexcept { return 0.25f + 0.0175f * colourPercent; }

    void prepareBuiltIn(double, int) override
    {
        envelope_ = fast_ = slow_ = memory_ = 0.0f;
        feedbackL_ = feedbackR_ = 0.0f;
        for (auto& s : shelf_)
            s.reset();
        lastEmphasis_ = std::nanf("");
        for (auto& v : valve_)
            v = {};
        makeup_ = dbToGain(parameter(opto::gain));
        drive_ = valveDrive(parameter(opto::colour));
        dcCoefficient_ = 1.0f - static_cast<float>(2.0 * std::numbers::pi * 10.0 / sampleRate_);
        const float sr = static_cast<float>(sampleRate_);
        const auto step = [sr](float seconds) { return dsp::stepForSeconds(seconds, sr); };
        envelopeFall_ = coefficient(sampleRate_, 20.0f);
        attackStep_ = step(0.030f);
        fastReleaseStep_ = step(opto::fastReleaseMs * 0.001f);
        slowAttackStep_ = step(0.2f);
        memoryFillStep_ = step(2.0f);
        memoryDrainStep_ = step(6.0f);
    }

    // The valve stage on one channel's sample at drive `d` (ADAA1 over the
    // step from the previous sample at the same drive).
    float valve(size_t ch, float in, float d) noexcept
    {
        auto& v = valve_[ch];
        const double x = static_cast<double>(in) * d;
        const double previous = v.previousIn * d;
        const double dx = x - previous;
        double shaped = 0.0;
        if (std::abs(dx) > 1.0e-5)
        {
            // The antiderivative at the previous sample is the one worked out then,
            // unless the drive moved.
            const double fPrevious =
                v.primedDrive == d ? v.previousF : saturation::antiderivative(saturation::tube, previous);
            const double fx = saturation::antiderivative(saturation::tube, x);
            shaped = (fx - fPrevious) / dx;
            v.previousF = fx;
            v.primedDrive = d;
        }
        else
        {
            shaped = saturation::shapeD(saturation::tube, 0.5 * (x + previous));
            v.primedDrive = -1.0f; // F(x) not worked out: the next step does both ends
        }
        v.previousIn = in;
        // Unity gain for small signals: the curve's slope at rest is sech^2(bias).
        const double y = shaped / (static_cast<double>(d) * valveSlope);
        const double blocked = y - v.dcIn + dcCoefficient_ * v.dcOut;
        v.dcIn = y;
        v.dcOut = blocked;
        return static_cast<float>(blocked);
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float peakReduction = parameter(opto::peakReduction);
        const int mode = parameter(opto::mode) >= 0.5f ? 1 : 0;
        const float emphasis = parameter(opto::emphasis);
        const float colour = parameter(opto::colour);
        const float wet = parameter(opto::mix) * 0.01f;
        if (emphasis != lastEmphasis_)
        {
            lastEmphasis_ = emphasis;
            for (auto& s : shelf_)
                s.setHighShelf(sampleRate_, 1500.0, emphasis * 0.12); // up to +12 dB
        }
        const bool emphasise = emphasis > 0.05f;
        const bool coloured = colour > 0.05f || drive_ > valveDrive(0.05f); // or ramping down to clean
        // The slow stage's release follows the memory (slow enough to set per block).
        const float slowReleaseStep =
            dsp::stepForSeconds(opto::slowReleaseSeconds(memory_), static_cast<float>(sampleRate_));
        const float n = static_cast<float>(std::max(1, numSamples));
        const float targetMakeup = dbToGain(parameter(opto::gain));
        const float makeupStep = (targetMakeup - makeup_) / n;
        const float targetDrive = valveDrive(colour);
        const float driveStep = (targetDrive - drive_) / n;
        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = left[i], inR = right[i];
            // The detector hears the last output of the cell.
            float keyL = feedbackL_, keyR = feedbackR_;
            if (emphasise)
            {
                keyL = shelf_[0].process(keyL);
                keyR = shelf_[1].process(keyR);
            }
            const float rectified = std::max(std::abs(keyL), std::abs(keyR));
            envelope_ =
                rectified > envelope_ ? rectified : rectified + (envelope_ - rectified) * envelopeFall_;
            const float target = opto::cellTargetDb(gainToDb(envelope_), peakReduction, mode);
            fast_ += (target - fast_) * (target > fast_ ? attackStep_ : fastReleaseStep_);
            const float fill = std::clamp(fast_ / 12.0f, 0.0f, 1.0f);
            memory_ += (fill - memory_) * (fill > memory_ ? memoryFillStep_ : memoryDrainStep_);
            const float slowTarget = fast_ * opto::slowShare(memory_);
            slow_ += (slowTarget - slow_) * (slowTarget > slow_ ? slowAttackStep_ : slowReleaseStep);
            const float reduction = std::max(fast_, slow_);
            const float g = dbToGain(-reduction);
            feedbackL_ = inL * g;
            feedbackR_ = inR * g;
            makeup_ += makeupStep;
            drive_ += driveStep;
            float outL = feedbackL_ * makeup_, outR = feedbackR_ * makeup_;
            if (coloured)
            {
                outL = valve(0, outL, drive_);
                outR = valve(1, outR, drive_);
            }
            else
            {
                // Clean: keep the valve's history current, so switching it on
                // does not start from a stale sample.
                valve_[0].previousIn = outL;
                valve_[1].previousIn = outR;
                valve_[0].primedDrive = valve_[1].primedDrive = -1.0f;
            }
            left[i] = inL * (1.0f - wet) + outL * wet;
            right[i] = inR * (1.0f - wet) + outR * wet;
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
                feedback_.addSample(std::max(std::abs(inL), std::abs(inR)),
                                    std::max(std::abs(left[i]), std::abs(right[i])),
                                    reduction,
                                    memory_);
        }
        makeup_ = targetMakeup;
        drive_ = targetDrive;
        // Denormals: a long silence leaves the stages creeping toward zero.
        if (fast_ < 1.0e-6f)
            fast_ = 0.0f;
        if (slow_ < 1.0e-6f)
            slow_ = 0.0f;
    }

    struct Valve
    {
        double previousIn = 0.0;
        double previousF = 0.0;    // the antiderivative at previousIn * primedDrive
        float primedDrive = -1.0f; // the drive previousF was worked out at (-1: none)
        double dcIn = 0.0, dcOut = 0.0;
    };
    std::array<Valve, 2> valve_;
    std::array<Biquad, 2> shelf_;
    float lastEmphasis_ = 0.0f;
    float envelope_ = 0.0f, fast_ = 0.0f, slow_ = 0.0f, memory_ = 0.0f;
    float feedbackL_ = 0.0f, feedbackR_ = 0.0f;
    float makeup_ = 1.0f, drive_ = 1.0f;
    float dcCoefficient_ = 0.999f;
    float envelopeFall_ = 0.0f, attackStep_ = 0.0f, fastReleaseStep_ = 0.0f, slowAttackStep_ = 0.0f;
    float memoryFillStep_ = 0.0f, memoryDrainStep_ = 0.0f;
};

// ----- de-esser -------------------------------------------------------------------------------

std::vector<engine::ParameterInfo> deesserParameters()
{
    std::vector<engine::ParameterInfo> p(deesser::count);
    p[deesser::frequency] = {"Frequency", "Hz", 2000.0f, 16000.0f, 6000.0f, true};
    p[deesser::threshold] = {"Threshold", "dB", -60.0f, 0.0f, -30.0f, false};
    p[deesser::range] = {"Range", "dB", 0.0f, 24.0f, 8.0f, false};
    p[deesser::mode] = choice("Mode", {"Split", "Wide"}, 0.0f);
    p[deesser::listen] = choice("Listen", {"Off", "On"}, 0.0f);
    return p;
}

// A 24 dB Linkwitz-Riley crossover splits the signal at the frequency (the two
// bands sum back flat). The high band's envelope above the threshold sets the
// reduction, applied to the high band alone (split) or the whole signal
// (wide). Listen plays the band.
class DeEsser final : public FeedbackBuiltIn
{
public:
    DeEsser() : FeedbackBuiltIn(types::deesser, deesserParameters()) {}

private:
    void prepareBuiltIn(double, int) override
    {
        for (auto& f : filters_)
            f.reset();
        envelope_ = 0.0f;
        lastFrequency_ = -1.0f;
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float frequency = parameter(deesser::frequency);
        if (frequency != lastFrequency_)
        {
            lastFrequency_ = frequency;
            // LR4: each band is two Butterworth sections in series.
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* f = filters_.data() + ch * 4;
                f[0].setLowPass(sampleRate_, frequency);
                f[1].setLowPass(sampleRate_, frequency);
                f[2].setHighPass(sampleRate_, frequency);
                f[3].setHighPass(sampleRate_, frequency);
            }
        }
        const float threshold = parameter(deesser::threshold);
        const float range = parameter(deesser::range);
        const bool wide = parameter(deesser::mode) >= 0.5f;
        const bool listen = parameter(deesser::listen) >= 0.5f;
        const float attack = coefficient(sampleRate_, 0.5f);
        const float release = coefficient(sampleRate_, 60.0f);
        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = left[i], inR = right[i];
            const float lowL = filters_[1].process(filters_[0].process(inL));
            const float lowR = filters_[5].process(filters_[4].process(inR));
            const float bandL = filters_[3].process(filters_[2].process(inL));
            const float bandR = filters_[7].process(filters_[6].process(inR));
            const float detect = std::max(std::abs(bandL), std::abs(bandR));
            const float a = detect > envelope_ ? attack : release;
            envelope_ = detect + (envelope_ - detect) * a;
            const float reduction = deesser::reductionDb(gainToDb(envelope_), threshold, range);
            const float g = dbToGain(-reduction);
            if (listen)
            {
                left[i] = bandL;
                right[i] = bandR;
            }
            else if (wide)
            {
                left[i] = inL * g;
                right[i] = inR * g;
            }
            else
            {
                left[i] = lowL + bandL * g;
                right[i] = lowR + bandR * g;
            }
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
                feedback_.addSample(std::max(std::abs(inL), std::abs(inR)),
                                    std::max(std::abs(left[i]), std::abs(right[i])),
                                    reduction,
                                    envelope_);
        }
    }

    std::array<Biquad, 8> filters_; // per channel: low-pass x2, high-pass x2
    float envelope_ = 0.0f;
    float lastFrequency_ = -1.0f;
};

// ----- saturation -----------------------------------------------------------------------------

std::vector<engine::ParameterInfo> saturationParameters()
{
    std::vector<engine::ParameterInfo> p(saturation::count);
    p[saturation::drive] = {"Drive", "dB", 0.0f, 36.0f, 6.0f, false};
    p[saturation::type] = choice("Type", {"Tape", "Tube", "Clip"}, 0.0f);
    p[saturation::tone] = {"Tone", "", -1.0f, 1.0f, 0.0f, false};
    p[saturation::output] = {"Output", "dB", -24.0f, 12.0f, 0.0f, false};
    p[saturation::mix] = {"Mix", "%", 0.0f, 100.0f, 100.0f, false};
    return p;
}

// Drive into a static curve with first-order antiderivative anti-aliasing (no
// oversampling, so no latency), a DC blocker for the biased curve, a tone
// shelf, output trim and an internal wet/dry.
class Saturation final : public FeedbackBuiltIn
{
public:
    Saturation() : FeedbackBuiltIn(types::saturation, saturationParameters()) {}

private:
    void prepareBuiltIn(double, int) override
    {
        for (auto& s : state_)
            s = {};
        lastTone_ = std::nanf("");
        drive_ = dbToGain(parameter(saturation::drive));
        trim_ = currentTrim();
        dcCoefficient_ = 1.0f - static_cast<float>(2.0 * std::numbers::pi * 10.0 / sampleRate_);
    }

    float currentTrim() const noexcept
    {
        const float drive = parameter(saturation::drive);
        return dbToGain(saturation::driveCompensationDb(drive) + parameter(saturation::output));
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const int type = std::clamp(static_cast<int>(std::lround(parameter(saturation::type))), 0, 2);
        if (type != type_)
        {
            type_ = type;
            for (auto& s : state_)
            {
                s.previous = 0.0; // the antiderivative changed: restart from silence
                s.primed = false;
            }
        }
        const float tone = parameter(saturation::tone);
        const bool ramp = tone != lastTone_;
        Biquad from = state_[0].shelf;
        Biquad to;
        to.setHighShelf(sampleRate_, 3000.0, tone * 6.0);
        if (std::isnan(lastTone_))
            for (auto& s : state_)
                s.shelf.blend(to, to, 1.0f);
        const bool rampNow = ramp && !std::isnan(lastTone_);
        lastTone_ = tone;
        const float wet = parameter(saturation::mix) * 0.01f;
        const float targetDrive = dbToGain(parameter(saturation::drive));
        const float targetTrim = currentTrim();
        const float n = static_cast<float>(std::max(1, numSamples));
        const float driveStep = (targetDrive - drive_) / n;
        const float trimStep = (targetTrim - trim_) / n;
        float inPeak = 0.0f, outPeak = 0.0f, drivenPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            drive_ += driveStep;
            trim_ += trimStep;
            if (rampNow)
            {
                const float t = static_cast<float>(i + 1) / n;
                for (auto& s : state_)
                    s.shelf.blend(from, to, t);
            }
            float* samples[2] = {left + i, right + i};
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto& s = state_[ch];
                const float in = *samples[ch];
                const double x = static_cast<double>(in) * drive_;
                drivenPeak = std::max(drivenPeak, static_cast<float>(std::abs(x)));
                // ADAA1: the mean of the curve over the step from the previous input.
                const double dx = x - s.previous;
                double y = 0.0;
                if (std::abs(dx) > 1.0e-5)
                {
                    // F at the previous sample was worked out then (x already holds the drive).
                    const double fPrevious =
                        s.primed ? s.previousF : saturation::antiderivative(type_, s.previous);
                    const double fx = saturation::antiderivative(type_, x);
                    y = (fx - fPrevious) / dx;
                    s.previousF = fx;
                    s.primed = true;
                }
                else
                {
                    y = saturation::shapeD(type_, 0.5 * (x + s.previous));
                    s.primed = false;
                }
                s.previous = x;
                // DC blocker (the tube curve is asymmetric).
                const double blocked = y - s.dcIn + dcCoefficient_ * s.dcOut;
                s.dcIn = y;
                s.dcOut = blocked;
                y = s.shelf.process(static_cast<float>(blocked)) * trim_;
                const float out = in * (1.0f - wet) + static_cast<float>(y) * wet;
                inPeak = std::max(inPeak, std::abs(in));
                outPeak = std::max(outPeak, std::abs(out));
                *samples[ch] = out;
            }
        }
        drive_ = targetDrive;
        trim_ = targetTrim;
        if (feedback_.watched())
            feedback_.addBlock(inPeak, outPeak, 0.0f, drivenPeak, numSamples);
    }

    struct Channel
    {
        double previous = 0.0;
        double previousF = 0.0; // the antiderivative at `previous` (valid when primed)
        bool primed = false;
        double dcIn = 0.0, dcOut = 0.0;
        Biquad shelf;
    };
    std::array<Channel, 2> state_;
    int type_ = 0;
    float lastTone_ = 0.0f;
    float drive_ = 1.0f, trim_ = 1.0f;
    float dcCoefficient_ = 0.999f;
};

// ----- limiter --------------------------------------------------------------------------------

std::vector<engine::ParameterInfo> limiterParameters()
{
    std::vector<engine::ParameterInfo> p(limiter::count);
    p[limiter::input] = {"Input", "dB", 0.0f, 24.0f, 0.0f, false};
    p[limiter::ceiling] = {"Ceiling", "dB", -12.0f, 0.0f, -0.3f, false};
    p[limiter::release] = {"Release", "ms", 1.0f, 1000.0f, 60.0f, true};
    return p;
}

// Zero-latency peak limiter: the gain drops at once to what the sample needs
// and recovers at the release rate; the output is clamped to the ceiling. No
// lookahead, so a strip that is switched off adds no delay to the channel.
class PeakLimiter final : public FeedbackBuiltIn
{
public:
    PeakLimiter() : FeedbackBuiltIn(types::limiter, limiterParameters()) {}

private:
    void prepareBuiltIn(double, int) override
    {
        gain_ = 1.0f;
        input_ = dbToGain(parameter(limiter::input));
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const float ceiling = dbToGain(parameter(limiter::ceiling));
        const float release = coefficient(sampleRate_, parameter(limiter::release));
        const float targetInput = dbToGain(parameter(limiter::input));
        const float inputStep = (targetInput - input_) / static_cast<float>(std::max(1, numSamples));
        for (int i = 0; i < numSamples; ++i)
        {
            input_ += inputStep;
            const float l = left[i] * input_, r = right[i] * input_;
            const float peak = std::max(std::abs(l), std::abs(r));
            const float needed = peak > ceiling ? ceiling / peak : 1.0f;
            gain_ = needed < gain_ ? needed : std::min(1.0f, release * gain_ + (1.0f - release));
            left[i] = std::clamp(l * gain_, -ceiling, ceiling);
            right[i] = std::clamp(r * gain_, -ceiling, ceiling);
            if (feedback_.watched()) // the meters cost nothing while no editor shows them
                feedback_.addSample(peak,
                                    std::max(std::abs(left[i]), std::abs(right[i])),
                                    std::max(0.0f, -gainToDb(gain_)),
                                    0.0f);
        }
        input_ = targetInput;
    }

    float gain_ = 1.0f;
    float input_ = 1.0f;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> createKeyedCompressor(const std::string& type)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("strip::createKeyedCompressor");
    return std::make_unique<Compressor>(type, true);
}

std::unique_ptr<engine::InsertProcessor> createOpto(const std::string& type)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("strip::createOpto");
    return std::make_unique<Opto>(type);
}

std::unique_ptr<engine::InsertProcessor> create(const std::string& type)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("strip::create");
    if (type == types::opto)
        return std::make_unique<Opto>();
    if (type == types::gate)
        return std::make_unique<Gate>();
    if (type == types::eq)
        return std::make_unique<SixBandEq>();
    if (type == types::compressor)
        return std::make_unique<Compressor>();
    if (type == types::deesser)
        return std::make_unique<DeEsser>();
    if (type == types::saturation)
        return std::make_unique<Saturation>();
    if (type == types::limiter)
        return std::make_unique<PeakLimiter>();
    return nullptr;
}

std::vector<core::Insert> defaultStrip()
{
    std::vector<core::Insert> modules;
    for (const auto& type : moduleTypes())
    {
        core::Insert module;
        module.type = type;
        module.bypass = true; // off until switched on
        for (const auto& p : parameterInfo(type))
            module.params.push_back(p.defaultValue);
        modules.push_back(std::move(module));
    }
    return modules;
}

bool ensureChannelStrip(core::Project& project)
{
    const auto defaults = defaultStrip();
    bool changed = false;
    const auto ensure = [&](core::Channel& channel)
    {
        for (const auto& module : defaults)
        {
            // A slot counts as present whichever model runs in it.
            const bool present =
                std::any_of(channel.strip.begin(),
                            channel.strip.end(),
                            [&](const core::Insert& m) { return slotOf(m.type) == module.type; });
            if (present)
                continue;
            auto added = module;
            added.id = project.allocateId();
            channel.strip.push_back(std::move(added));
            changed = true;
        }
    };
    for (auto& t : project.tracks)
        if (t.hasAudio()) // a MIDI track makes no audio to process
            ensure(t);
    for (auto& b : project.buses)
        ensure(b);
    return changed;
}

} // namespace northlight::plugins::strip
