#include "plugins/ChannelStrip.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/ProcessorBase.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace aural::plugins::strip
{

const std::vector<std::string>& moduleTypes()
{
    static const std::vector<std::string> list{
        types::gate, types::eq, types::compressor, types::deesser, types::saturation, types::limiter};
    return list;
}

bool isStripType(const std::string& type)
{
    return type.rfind("aural.strip.", 0) == 0;
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
    return type;
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
    double db = 20.0 * std::log10(std::max(1.0e-9, magnitude));
    if (band < 0)
        db += params[output];
    return db;
}
} // namespace eq

namespace compressor
{
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

double logCosh(double x) noexcept
{
    const double a = std::abs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - std::numbers::ln2;
}

double shapeD(int type, double x) noexcept
{
    switch (type)
    {
        case tube:
            return std::tanh(x + tubeBias) - std::tanh(tubeBias);
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
            return logCosh(x + tubeBias) - x * std::tanh(tubeBias);
        case clip:
            return std::abs(x) <= 1.0 ? 1.5 * (x * x / 2.0 - x * x * x * x / 12.0) : std::abs(x) - 0.375;
        default:
            return logCosh(x);
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
    return std::exp(-1.0f / (static_cast<float>(sampleRate) * std::max(0.001f, ms) * 0.001f));
}

float peakOf(const float* left, const float* right, int numSamples) noexcept
{
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
    return peak;
}

engine::ParameterInfo choice(std::string name, std::vector<std::string> choices, float defaultValue)
{
    engine::ParameterInfo info;
    info.name = std::move(name);
    info.minimum = 0.0f;
    info.maximum = static_cast<float>(choices.size() - 1);
    info.defaultValue = defaultValue;
    info.choices = std::move(choices);
    return info;
}

// What every module shares: the feedback block and, while bypassed, a
// measurement of the input for an editor that is watching (so the threshold
// can be set against the signal before the module is switched on).
class StripProcessor : public ProcessorBase
{
public:
    StripProcessor(std::string typeName, std::vector<engine::ParameterInfo> infos, bool taps = false)
        : ProcessorBase(std::move(typeName), std::move(infos)), feedback_(taps)
    {
    }

    engine::ProcessorFeedback* feedback() noexcept override { return &feedback_; }

protected:
    void prepareAudio(double sampleRate, int maxBlockSize) final
    {
        feedback_.prepare(sampleRate);
        prepareModule(sampleRate, maxBlockSize);
    }
    virtual void prepareModule(double sampleRate, int maxBlockSize) = 0;

    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        feedback_.addBypassed(left, right, numSamples);
    }

    engine::ProcessorFeedback feedback_;
};

// ----- gate -----------------------------------------------------------------------------------

// A key-filtered peak detector opens the gate above the threshold; it closes
// after the hold time once the key falls `hysteresisDb` below it. The gain
// moves between unity and -range at the attack and release rates.
class Gate final : public StripProcessor
{
public:
    Gate()
        : StripProcessor(types::gate,
                         {{"Threshold", "dB", -80.0f, 0.0f, -40.0f, false},
                          {"Range", "dB", 0.0f, 80.0f, 40.0f, false},
                          {"Attack", "ms", 0.05f, 50.0f, 0.5f, true},
                          {"Hold", "ms", 0.0f, 500.0f, 30.0f, false},
                          {"Release", "ms", 5.0f, 2000.0f, 120.0f, true},
                          {"Key HP", "Hz", 20.0f, 2000.0f, 20.0f, true},
                          {"Key LP", "Hz", 1000.0f, 20000.0f, 20000.0f, true}})
    {
    }

private:
    void prepareModule(double, int) override
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
            const float levelDb = gainToDb(envelope_);
            if (levelDb > threshold)
            {
                open_ = true;
                holdLeft_ = hold;
            }
            else if (open_ && levelDb < threshold - gate::hysteresisDb)
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
class Eq final : public StripProcessor
{
public:
    Eq() : StripProcessor(types::eq, eqParameters(), true) {}

private:
    void prepareModule(double, int) override
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
        feedback_.tap(0, left, right, numSamples);
        const float inPeak = peakOf(left, right, numSamples);
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
        feedback_.addBlock(inPeak, peakOf(left, right, numSamples), 0.0f, 0.0f, numSamples);
        feedback_.tap(1, left, right, numSamples);
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

// Feed-forward, stereo-linked: a peak or RMS detector, the soft-knee curve on
// its level, the reduction smoothed with attack and release (smoothing the
// curve's output rather than the level keeps a waveform's troughs from
// dragging the envelope down), make-up (manual plus auto) and an internal
// wet/dry for parallel compression. The one compressor: the channel strip's
// module, and (keyed) the "aural.compressor" insert, whose detector listens to
// a sidechain key when one is chosen.
class Compressor final : public StripProcessor
{
public:
    explicit Compressor(const std::string& type = types::compressor, bool keyed = false)
        : StripProcessor(type,
                         {{"Threshold", "dB", -60.0f, 0.0f, -18.0f, false},
                          {"Ratio", ":1", 1.0f, 20.0f, 3.0f, true},
                          {"Attack", "ms", 0.1f, 100.0f, 10.0f, true},
                          {"Release", "ms", 10.0f, 2000.0f, 120.0f, true},
                          {"Knee", "dB", 0.0f, 24.0f, 6.0f, false},
                          {"Makeup", "dB", 0.0f, 24.0f, 0.0f, false},
                          choice("Auto makeup", {"Off", "On"}, 0.0f),
                          choice("Detector", {"Peak", "RMS"}, 0.0f),
                          {"Mix", "%", 0.0f, 100.0f, 100.0f, false}}),
          keyed_(keyed)
    {
    }

    bool acceptsSidechain() const noexcept override { return keyed_; }

private:
    void prepareModule(double, int) override
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
            feedback_.addSample(std::max(std::abs(inL), std::abs(inR)),
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

// ----- de-esser -------------------------------------------------------------------------------

// A 24 dB Linkwitz-Riley crossover splits the signal at the frequency (the two
// bands sum back flat). The high band's envelope above the threshold sets the
// reduction, applied to the high band alone (split) or the whole signal
// (wide). Listen plays the band.
class DeEsser final : public StripProcessor
{
public:
    DeEsser()
        : StripProcessor(types::deesser,
                         {{"Frequency", "Hz", 2000.0f, 16000.0f, 6000.0f, true},
                          {"Threshold", "dB", -60.0f, 0.0f, -30.0f, false},
                          {"Range", "dB", 0.0f, 24.0f, 8.0f, false},
                          choice("Mode", {"Split", "Wide"}, 0.0f),
                          choice("Listen", {"Off", "On"}, 0.0f)})
    {
    }

private:
    void prepareModule(double, int) override
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

// Drive into a static curve with first-order antiderivative anti-aliasing (no
// oversampling, so no latency), a DC blocker for the biased curve, a tone
// shelf, output trim and an internal wet/dry.
class Saturation final : public StripProcessor
{
public:
    Saturation()
        : StripProcessor(types::saturation,
                         {{"Drive", "dB", 0.0f, 36.0f, 6.0f, false},
                          choice("Type", {"Tape", "Tube", "Clip"}, 0.0f),
                          {"Tone", "", -1.0f, 1.0f, 0.0f, false},
                          {"Output", "dB", -24.0f, 12.0f, 0.0f, false},
                          {"Mix", "%", 0.0f, 100.0f, 100.0f, false}})
    {
    }

private:
    void prepareModule(double, int) override
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
                s.previous = 0.0; // the antiderivative changed: restart from silence
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
                double y = std::abs(dx) > 1.0e-5 ? (saturation::antiderivative(type_, x) -
                                                    saturation::antiderivative(type_, s.previous)) /
                                                       dx
                                                 : saturation::shapeD(type_, 0.5 * (x + s.previous));
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
        feedback_.addBlock(inPeak, outPeak, 0.0f, drivenPeak, numSamples);
    }

    struct Channel
    {
        double previous = 0.0;
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

// Zero-latency peak limiter: the gain drops at once to what the sample needs
// and recovers at the release rate; the output is clamped to the ceiling. No
// lookahead, so a strip that is switched off adds no delay to the channel.
class Limiter final : public StripProcessor
{
public:
    Limiter()
        : StripProcessor(types::limiter,
                         {{"Input", "dB", 0.0f, 24.0f, 0.0f, false},
                          {"Ceiling", "dB", -12.0f, 0.0f, -0.3f, false},
                          {"Release", "ms", 1.0f, 1000.0f, 60.0f, true}})
    {
    }

private:
    void prepareModule(double, int) override
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
    AURAL_ASSERT_NOT_REALTIME("strip::createKeyedCompressor");
    return std::make_unique<Compressor>(type, true);
}

std::unique_ptr<engine::InsertProcessor> create(const std::string& type)
{
    AURAL_ASSERT_NOT_REALTIME("strip::create");
    if (type == types::gate)
        return std::make_unique<Gate>();
    if (type == types::eq)
        return std::make_unique<Eq>();
    if (type == types::compressor)
        return std::make_unique<Compressor>();
    if (type == types::deesser)
        return std::make_unique<DeEsser>();
    if (type == types::saturation)
        return std::make_unique<Saturation>();
    if (type == types::limiter)
        return std::make_unique<Limiter>();
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
            const bool present = std::any_of(channel.strip.begin(),
                                             channel.strip.end(),
                                             [&](const core::Insert& m) { return m.type == module.type; });
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

} // namespace aural::plugins::strip
