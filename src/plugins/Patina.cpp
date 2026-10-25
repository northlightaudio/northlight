#include "plugins/Patina.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/ParamHelpers.h"
#include "plugins/ProcessorBase.h"
#include "plugins/dsp/DelayLine.h"
#include "plugins/dsp/Denormals.h"
#include "plugins/dsp/Random.h"
#include "plugins/dsp/Shapers.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace northlight::plugins::patina
{

namespace
{
constexpr float pi = std::numbers::pi_v<float>;
constexpr float twoPi = 2.0f * pi;
constexpr float cleanRate = 48000.0f, lowestRate = 1000.0f;
constexpr float cleanBits = 16.0f, fewestBits = 2.0f;
} // namespace

// ----- the shared mappings ------------------------------------------------------------------------

int onParam(int stage) noexcept
{
    static constexpr int on[] = {heatOn, chamberOn, driftOn, grainOn, wearOn, crushOn};
    return on[std::clamp(stage, 0, stageCount - 1)];
}

int breathParam(int stage) noexcept
{
    static constexpr int breath[] = {
        heatBreath, chamberBreath, driftBreath, grainBreath, wearBreath, crushBreath};
    return breath[std::clamp(stage, 0, stageCount - 1)];
}

float aged(float amount, float ageFraction) noexcept
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    ageFraction = std::clamp(ageFraction, 0.0f, 1.0f);
    if (ageFraction <= 0.5f)
        return amount * ageFraction * 2.0f;
    return amount + (1.0f - amount) * amount * (ageFraction - 0.5f) * 2.0f;
}

float heatDriveDb(float amount) noexcept
{
    return -12.0f + 39.0f * std::clamp(amount, 0.0f, 1.0f);
}

float chamberDecaySeconds(int space, float decay) noexcept
{
    decay = std::clamp(decay, 0.0f, 1.0f);
    switch (space)
    {
        case spring:
            return 0.8f + 3.2f * decay;
        case plate:
            return 0.9f + 3.6f * decay;
        default:
            return 0.3f + 1.2f * decay;
    }
}

float wearCutoffHz(float loss) noexcept
{
    return 20000.0f * std::pow(0.12f, std::clamp(loss, 0.0f, 1.0f));
}

float crushBitsFor(float amount) noexcept
{
    return cleanBits - (cleanBits - fewestBits) * std::clamp(amount, 0.0f, 1.0f);
}

float crushRateFor(float amount) noexcept
{
    return cleanRate * std::pow(lowestRate / cleanRate, std::clamp(amount, 0.0f, 1.0f));
}

float crushAmountOfBits(float bits) noexcept
{
    return std::clamp((cleanBits - bits) / (cleanBits - fewestBits), 0.0f, 1.0f);
}

float crushAmountOfRate(float hz) noexcept
{
    return std::clamp(
        std::log(cleanRate / std::max(1.0f, hz)) / std::log(cleanRate / lowestRate), 0.0f, 1.0f);
}

// ----- Heat's curves ------------------------------------------------------------------------------

namespace
{
constexpr double tubeBias = 0.3;
constexpr double fuzzPositiveClip = 0.7, fuzzNegativeClip = 1.0;
constexpr double heatReference = 0.25; // the level (-12 dBFS) the compensation holds

double fuzzDeadZone(double amount) noexcept
{
    return 0.02 + 0.1 * amount;
}

// The fuzz curve's half for a magnitude s >= 0: the dead zone t, then unity
// slope to the clip c.
double fuzzHalf(double s, double t, double c) noexcept
{
    return s < t ? 0.0 : std::min(s - t, c);
}
double fuzzHalfIntegral(double s, double t, double c) noexcept
{
    if (s < t)
        return 0.0;
    if (s < t + c)
        return 0.5 * (s - t) * (s - t);
    return 0.5 * c * c + c * (s - t - c);
}

double curve(int character, double u, double deadZone) noexcept
{
    switch (character)
    {
        case tube:
            return dsp::tube(u, tubeBias);
        case iron:
            // A cubic with unity slope at zero that flattens at 1.5.
            return std::abs(u) <= 1.5 ? u - (4.0 / 27.0) * u * u * u : (u > 0.0 ? 1.0 : -1.0);
        case fuzz:
            return u >= 0.0 ? fuzzHalf(u, deadZone, fuzzPositiveClip)
                            : -fuzzHalf(-u, deadZone, fuzzNegativeClip);
        default:
            return std::tanh(u);
    }
}

// The antiderivative of curve(), for first-order antiderivative anti-aliasing.
double curveIntegral(int character, double u, double deadZone) noexcept
{
    switch (character)
    {
        case tube:
            return dsp::tubeIntegral(u, tubeBias);
        case iron:
            return std::abs(u) <= 1.5 ? 0.5 * u * u - u * u * u * u / 27.0 : std::abs(u) - 0.5625;
        case fuzz:
            return u >= 0.0 ? fuzzHalfIntegral(u, deadZone, fuzzPositiveClip)
                            : fuzzHalfIntegral(-u, deadZone, fuzzNegativeClip);
        default:
            return dsp::logCosh(u);
    }
}

// The output trim for a drive: between what holds small signals level and what
// holds a -12 dBFS peak level (weighted toward the latter), so more drive mostly
// adds harmonics and density rather than loudness.
double heatCompensation(int character, double drive, double deadZone) noexcept
{
    const double atReference = std::max(1.0e-3, std::abs(curve(character, drive * heatReference, deadZone)));
    return std::pow(1.0 / drive, 0.35) * std::pow(heatReference / atReference, 0.65);
}
} // namespace

float heatTransfer(int character, float amount, float x) noexcept
{
    const double drive = std::pow(10.0, heatDriveDb(amount) / 20.0);
    const double deadZone = fuzzDeadZone(amount);
    return static_cast<float>(heatCompensation(character, drive, deadZone) *
                              curve(character, drive * static_cast<double>(x), deadZone));
}

// ----- telemetry ----------------------------------------------------------------------------------

Telemetry::Telemetry()
    : scopeIn_(new std::atomic<float>[scopeLength]()), scopeOut_(new std::atomic<float>[scopeLength]())
{
}

void Telemetry::prepare(double sampleRate) noexcept
{
    sampleRate_.store(sampleRate, std::memory_order_relaxed);
}

void Telemetry::scope(float in, float out) noexcept
{
    const auto w = scopeWritten_.load(std::memory_order_relaxed);
    scopeIn_[w % scopeLength].store(in, std::memory_order_relaxed);
    scopeOut_[w % scopeLength].store(out, std::memory_order_relaxed);
    scopeWritten_.store(w + 1, std::memory_order_release);
}

bool Telemetry::readScope(float* in, float* out, int n) const noexcept
{
    if (n > scopeLength / 2)
        return false;
    const auto end = scopeWritten_.load(std::memory_order_acquire);
    if (end < static_cast<std::uint64_t>(n))
        return false;
    auto at = end - static_cast<std::uint64_t>(n);
    for (int i = 0; i < n; ++i, ++at)
    {
        in[i] = scopeIn_[at % scopeLength].load(std::memory_order_relaxed);
        out[i] = scopeOut_[at % scopeLength].load(std::memory_order_relaxed);
    }
    return true;
}

// ----- building blocks ----------------------------------------------------------------------------

namespace
{
// A smooth random contour, 0..1: a new random target about every 1/hz seconds,
// followed through two one-pole stages, so it wanders rather than steps.
struct Contour
{
    float a = 0.0f, b = 0.0f, target = 0.0f, timer = 0.0f;
    float step(float seconds, float hz, dsp::XorShift32& rng) noexcept
    {
        timer -= seconds * hz;
        if (timer <= 0.0f)
        {
            timer += 0.5f + rng.uniform();
            target = rng.uniform();
        }
        const float k = 1.0f - std::exp(-twoPi * hz * 0.8f * seconds);
        a += (target - a) * k;
        b += (a - b) * k;
        return b;
    }
    float bipolar(float seconds, float hz, dsp::XorShift32& rng) noexcept
    {
        return 2.0f * step(seconds, hz, rng) - 1.0f;
    }
};

// A Schroeder allpass of a fixed length; `extra` modulates its delay.
struct Allpass
{
    dsp::DelayLine line;
    int length = 1;
    void prepare(int samples, int modulation = 0)
    {
        length = std::max(1, samples);
        line.prepare(length + modulation + 2);
    }
    float process(float x, float g, float extra = 0.0f) noexcept
    {
        const float delayed =
            extra > 0.0f ? line.linear(static_cast<float>(length - 1) + extra) : line.at(length - 1);
        const float w = x + g * delayed;
        line.push(w);
        return delayed - g * w;
    }
};

float onePoleCoefficient(double sampleRate, double hz) noexcept
{
    return 1.0f - static_cast<float>(dsp::poleForHz(hz, sampleRate));
}

float smoothingCoefficient(double rate, float seconds) noexcept
{
    return dsp::stepForSeconds(std::max(1.0e-4f, seconds), static_cast<float>(rate));
}

// Pink noise from white (Paul Kellet's economy filter), about unit RMS for
// uniform white of RMS 0.577.
struct Pink
{
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    float process(float white) noexcept
    {
        b0 = 0.99765f * b0 + white * 0.0990460f;
        b1 = 0.96300f * b1 + white * 0.2965164f;
        b2 = 0.57000f * b2 + white * 1.0526913f;
        return (b0 + b1 + b2 + white * 0.1848f) * 0.55f;
    }
};

// Patina's floor for a decaying state.
constexpr float denormalFloor = 1.0e-15f;

// ----- the processor ------------------------------------------------------------------------------

std::vector<engine::ParameterInfo> parameterInfos()
{
    std::vector<engine::ParameterInfo> p(count);
    p[age] = percent("Age", 50.0f);
    p[pace] = percent("Pace", 35.0f);
    p[heatOn] = onOff("Heat", true);
    p[heatCharacter] = choice("Heat character", {"Tube", "Tape", "Iron", "Fuzz"}, 0.0f);
    p[heatDrive] = percent("Heat drive", 35.0f);
    p[heatTone] = percent("Heat tone", 0.0f, -100.0f, 100.0f);
    p[heatBreath] = percent("Heat breath", 0.0f);
    p[chamberOn] = onOff("Chamber", false);
    p[chamberType] = choice("Chamber space", {"Room", "Spring", "Plate"}, 1.0f);
    p[chamberMix] = percent("Chamber mix", 30.0f);
    p[chamberDecay] = percent("Chamber decay", 40.0f);
    p[chamberTone] = percent("Chamber tone", -20.0f, -100.0f, 100.0f);
    p[chamberBreath] = percent("Chamber breath", 0.0f);
    p[driftOn] = onOff("Drift", true);
    p[driftMachine] = choice("Drift machine", {"Reel", "Deck", "Disc"}, 0.0f);
    p[driftWow] = percent("Drift wow", 25.0f);
    p[driftFlutter] = percent("Drift flutter", 20.0f);
    p[driftRate] = percent("Drift rate", 50.0f);
    p[driftStereo] = percent("Drift stereo", 30.0f);
    p[driftBreath] = percent("Drift breath", 20.0f);
    p[grainOn] = onOff("Grain", true);
    p[grainSource] =
        choice("Grain source", {"Vinyl", "Tape hiss", "Hum 50 Hz", "Hum 60 Hz", "Room air"}, 0.0f);
    p[grainAmount] = percent("Grain amount", 30.0f);
    p[grainTexture] = percent("Grain texture", 35.0f);
    p[grainFollow] = percent("Grain follow", 0.0f, -100.0f, 100.0f);
    p[grainBreath] = percent("Grain breath", 0.0f);
    p[wearOn] = onOff("Wear", true);
    p[wearLoss] = percent("Wear loss", 30.0f);
    p[wearDropouts] = percent("Wear dropouts", 10.0f);
    p[wearSkew] = percent("Wear skew", 20.0f);
    p[wearBreath] = percent("Wear breath", 0.0f);
    p[crushOn] = onOff("Crush", false);
    p[crushFlavour] = choice("Crush flavour", {"Soft", "Hard", "Mu-law", "Jitter"}, 1.0f);
    p[crushBits] = {"Crush bits", "bits", fewestBits, cleanBits, 12.0f, false};
    p[crushRate] = {"Crush rate", "Hz", lowestRate, cleanRate, 22050.0f, true};
    p[crushBreath] = percent("Crush breath", 0.0f);
    p[tone] = percent("Tone", 0.0f, -100.0f, 100.0f);
    p[lowCut] = {"Low cut", "Hz", 20.0f, 600.0f, 20.0f, true};
    p[highCut] = {"High cut", "Hz", 2000.0f, 20000.0f, 20000.0f, true};
    p[width] = percent("Width", 100.0f, 0.0f, 200.0f);
    p[output] = {"Output", "dB", -24.0f, 12.0f, 0.0f, false};
    return p;
}

constexpr int subBlock = 32;
constexpr float stageFadeSeconds = 0.01f;

// A stage's switch and choice: the stage fades out before a new choice takes
// effect and in again after, so neither a switch nor a choice clicks.
struct StageGate
{
    float fade = 0.0f;
    int choice = 0, wanted = 0;
    bool on = false;
    bool needsReset = true;

    // The fade at the end of a sub-block of `samples`.
    float advance(int samples, float step) noexcept
    {
        const float target = on && choice == wanted ? 1.0f : 0.0f;
        const float travel = step * static_cast<float>(samples);
        fade = target > fade ? std::min(target, fade + travel) : std::max(target, fade - travel);
        return fade;
    }
    bool idle(float from) const noexcept { return from == 0.0f && fade == 0.0f; }
};

class Patina final : public ProcessorBase
{
public:
    Patina() : ProcessorBase(type, parameterInfos()) {}

    engine::ProcessorFeedback* feedback() noexcept override { return &feedback_; }
    const Telemetry& telemetry() const noexcept { return telemetry_; }

private:
    // ----- preparing ----------------------------------------------------------------------------
    void prepareAudio(double sampleRate, int) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Patina::prepareAudio");
        fs_ = sampleRate;
        const float rate = static_cast<float>(sampleRate);
        feedback_.prepare(sampleRate);
        telemetry_.prepare(sampleRate);
        frameInterval_ = std::max(1, static_cast<int>(sampleRate * Telemetry::frameSeconds));
        frameCount_ = 0;
        acc_ = {};
        fadeStep_ = 1.0f / (rate * stageFadeSeconds);
        smooth_ = smoothingCoefficient(sampleRate / subBlock, 0.04f);
        slowSmooth_ = smoothingCoefficient(sampleRate / subBlock, 0.3f);

        // Drift: up to 2% wow at a quarter of its slowest rate needs about 30 ms.
        for (auto& line : driftLine_)
            line.prepare(static_cast<int>(0.12 * sampleRate));
        // Wear's skew: up to 0.35 ms either way.
        for (auto& line : skewLine_)
            line.prepare(static_cast<int>(0.002 * sampleRate) + 8);
        prepareChamber();

        for (auto& g : gate_)
        {
            g = {};
            g.needsReset = true;
        }
        for (int s = 0; s < stageCount; ++s)
        {
            amount_[static_cast<size_t>(s)] = 0.0f;
            breath_[static_cast<size_t>(s)] = {};
        }
        first_ = true;
        resetHeat();
        resetDrift();
        resetGrain();
        resetWear();
        resetCrush();
        for (auto& f : tiltLow_)
            f.reset();
        for (auto& f : tiltHigh_)
            f.reset();
        for (auto& f : outLowCut_)
            f.reset();
        for (auto& f : outHighCut_)
            f.reset();
        lastTone_ = lastLowCut_ = lastHighCut_ = -1.0e9f;
    }

    // ----- the block ----------------------------------------------------------------------------
    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        readParameters();
        for (int start = 0; start < numSamples; start += subBlock)
        {
            const int n = std::min(subBlock, numSamples - start);
            controls(n);
            run(left + start, right + start, n);
            first_ = false;
        }
        for (auto* v : {&inEnv_, &driftScrape_[0], &driftScrape_[1]})
            *v = dsp::flush(*v, denormalFloor);
    }

    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        feedback_.addBypassed(left, right, numSamples);
    }

    struct Params
    {
        float age = 0.5f, pace = 0.35f;
        std::array<bool, stageCount> on{};
        std::array<float, stageCount> breath{};
        int heatCharacter = 0, chamberSpace = 0, driftMachine = 0, grainSource = 0, crushFlavour = 0;
        float heatDrive = 0, heatTone = 0;
        float chamberMix = 0, chamberDecay = 0, chamberTone = 0;
        float wow = 0, flutter = 0, driftRate = 0.5f, driftStereo = 0;
        float grainAmount = 0, grainTexture = 0, grainFollow = 0;
        float wearLoss = 0, wearDropouts = 0, wearSkew = 0;
        float crushBits = 0, crushRate = 0;
        float tone = 0, lowCut = 20, highCut = 20000, width = 1, output = 1;
    };

    static int choiceOf(float v) noexcept { return static_cast<int>(std::lround(v)); }

    void readParameters() noexcept
    {
        auto& p = p_;
        p.age = parameter(age) * 0.01f;
        p.pace = parameter(pace) * 0.01f;
        for (int s = 0; s < stageCount; ++s)
        {
            p.on[static_cast<size_t>(s)] = parameter(onParam(s)) >= 0.5f;
            p.breath[static_cast<size_t>(s)] = parameter(breathParam(s)) * 0.01f;
        }
        p.heatCharacter = choiceOf(parameter(heatCharacter));
        p.chamberSpace = choiceOf(parameter(chamberType));
        p.driftMachine = choiceOf(parameter(driftMachine));
        p.grainSource = choiceOf(parameter(grainSource));
        p.crushFlavour = choiceOf(parameter(crushFlavour));
        p.heatDrive = parameter(heatDrive) * 0.01f;
        p.heatTone = parameter(heatTone) * 0.01f;
        p.chamberMix = parameter(chamberMix) * 0.01f;
        p.chamberDecay = parameter(chamberDecay) * 0.01f;
        p.chamberTone = parameter(chamberTone) * 0.01f;
        p.wow = parameter(driftWow) * 0.01f;
        p.flutter = parameter(driftFlutter) * 0.01f;
        p.driftRate = parameter(driftRate) * 0.01f;
        p.driftStereo = parameter(driftStereo) * 0.01f;
        p.grainAmount = parameter(grainAmount) * 0.01f;
        p.grainTexture = parameter(grainTexture) * 0.01f;
        p.grainFollow = parameter(grainFollow) * 0.01f;
        p.wearLoss = parameter(wearLoss) * 0.01f;
        p.wearDropouts = parameter(wearDropouts) * 0.01f;
        p.wearSkew = parameter(wearSkew) * 0.01f;
        p.crushBits = crushAmountOfBits(parameter(crushBits));
        p.crushRate = crushAmountOfRate(parameter(crushRate));
        p.tone = parameter(tone) * 0.01f;
        p.lowCut = parameter(lowCut);
        p.highCut = parameter(highCut);
        p.width = parameter(width) * 0.01f;
        p.output = dbToGain(parameter(output));

        const std::array<int, stageCount> choices{
            p.heatCharacter, p.chamberSpace, p.driftMachine, p.grainSource, 0, p.crushFlavour};
        for (int s = 0; s < stageCount; ++s)
        {
            auto& g = gate_[static_cast<size_t>(s)];
            g.on = p.on[static_cast<size_t>(s)];
            g.wanted = choices[static_cast<size_t>(s)];
            if (first_)
                g.choice = g.wanted;
        }
    }

    // A smoothed value: at once on the first block, else toward the target.
    void follow(float& value, float target, float k) const noexcept
    {
        value = first_ ? target : value + (target - value) * k;
    }

    // Per sub-block: breath, the amounts with Age, filters and modulators.
    void controls(int n) noexcept
    {
        const auto& p = p_;
        const float seconds = static_cast<float>(n) / static_cast<float>(fs_);
        const float paceHz = 0.05f * std::pow(30.0f, p.pace); // 0.05 .. 1.5 Hz
        for (int s = 0; s < stageCount; ++s)
        {
            const auto i = static_cast<size_t>(s);
            const float contour =
                breath_[i].step(seconds, paceHz * (0.8f + 0.1f * static_cast<float>(s)), breathRng_);
            breathGain_[i] = 1.0f - p.breath[i] * contour;
        }
        const auto amount = [&](int stage, float value)
        { return aged(value, p.age) * breathGain_[static_cast<size_t>(stage)]; };
        follow(heatAmount_, amount(heat, p.heatDrive), smooth_);
        follow(heatTone_, p.heatTone, smooth_);
        follow(chamberMix_, amount(chamber, p.chamberMix), smooth_);
        follow(chamberTone_, p.chamberTone, smooth_);
        follow(chamberDecay_, p.chamberDecay, smooth_);
        follow(wow_, amount(drift, p.wow), smooth_);
        follow(flutter_, amount(drift, p.flutter), smooth_);
        follow(driftRate_, p.driftRate, slowSmooth_);
        follow(driftStereo_, p.driftStereo, smooth_);
        follow(grainAmount_, amount(grain, p.grainAmount), smooth_);
        follow(grainTexture_, p.grainTexture, smooth_);
        follow(grainFollow_, p.grainFollow, smooth_);
        follow(wearLoss_, amount(wear, p.wearLoss), smooth_);
        follow(wearDropouts_, amount(wear, p.wearDropouts), smooth_);
        follow(wearSkew_, amount(wear, p.wearSkew), smooth_);
        follow(crushBitsAmount_, amount(crush, p.crushBits), smooth_);
        follow(crushRateAmount_, amount(crush, p.crushRate), smooth_);
        follow(tone_, p.tone, smooth_);
        follow(width_, p.width, smooth_);
        follow(output_, p.output, smooth_);
        follow(lowCut_, p.lowCut, smooth_);
        follow(highCut_, p.highCut, smooth_);

        amount_ = {heatAmount_,
                   chamberMix_,
                   std::max(wow_, flutter_),
                   grainAmount_,
                   std::max({wearLoss_, wearDropouts_, wearSkew_}),
                   std::max(crushBitsAmount_, crushRateAmount_)};

        for (int s = 0; s < stageCount; ++s)
        {
            auto& g = gate_[static_cast<size_t>(s)];
            fadeFrom_[static_cast<size_t>(s)] = g.fade;
            g.advance(n, fadeStep_);
            // Faded out: a new choice takes effect, and a stage switched off
            // starts clean when it comes back.
            if (g.fade == 0.0f)
            {
                if (g.choice != g.wanted)
                {
                    g.choice = g.wanted;
                    g.needsReset = true;
                }
                if (!g.on)
                    g.needsReset = true;
            }
            else if (g.needsReset && fadeFrom_[static_cast<size_t>(s)] == 0.0f)
            {
                resetStage(s);
                g.needsReset = false;
            }
        }
        heatControls();
        chamberControls();
        driftControls(n);
        grainControls(seconds);
        wearControls(n, seconds);
        crushControls();
        outputControls();
    }

    void resetStage(int stage) noexcept
    {
        switch (stage)
        {
            case heat:
                resetHeat();
                break;
            case chamber:
                resetChamber();
                break;
            case drift:
                break; // its line keeps running, so it comes back seamlessly
            case grain:
                resetGrain();
                break;
            case wear:
                resetWear();
                break;
            default:
                resetCrush();
                break;
        }
    }

    // ----- the samples --------------------------------------------------------------------------
    void run(float* left, float* right, int n) noexcept
    {
        const auto fadeOf = [&](int stage, int i)
        {
            const auto s = static_cast<size_t>(stage);
            const float t = static_cast<float>(i + 1) / static_cast<float>(n);
            return fadeFrom_[s] + (gate_[s].fade - fadeFrom_[s]) * t;
        };
        const auto active = [&](int stage)
        { return !gate_[static_cast<size_t>(stage)].idle(fadeFrom_[static_cast<size_t>(stage)]); };
        const bool heatActive = active(heat), chamberActive = active(chamber), driftActive = active(drift),
                   grainActive = active(grain), wearActive = active(wear), crushActive = active(crush);
        const bool watched = feedback_.watched();
        const float env = smoothingCoefficient(fs_, 0.005f), envRelease = smoothingCoefficient(fs_, 0.25f);

        for (int i = 0; i < n; ++i)
        {
            float l = left[i], r = right[i];
            const float inPeak = std::max(std::abs(l), std::abs(r));
            const float t = static_cast<float>(i + 1) / static_cast<float>(n);

            // Heat
            if (heatActive)
            {
                acc_.heatLevel = std::max(acc_.heatLevel, inPeak);
                const float f = fadeOf(heat, i);
                const float hl = heatSample(0, l, t), hr = heatSample(1, r, t);
                l = l * (1.0f - f) + hl * f;
                r = r * (1.0f - f) + hr * f;
            }
            // Chamber
            if (chamberActive)
            {
                float wl = 0.0f, wr = 0.0f;
                chamberSample(l, r, wl, wr);
                const float g = chamberMix_ * fadeOf(chamber, i) * chamberWetGain();
                acc_.chamberWet = std::max(acc_.chamberWet, std::max(std::abs(wl), std::abs(wr)) * g);
                l += wl * g;
                r += wr * g;
            }
            // Drift: the line always runs, so switching it on is seamless.
            {
                driftLine_[0].push(l);
                driftLine_[1].push(r);
                if (driftActive)
                {
                    const float f = fadeOf(drift, i);
                    const float dl = driftFrom_[0] + (driftTo_[0] - driftFrom_[0]) * t;
                    const float dr = driftFrom_[1] + (driftTo_[1] - driftFrom_[1]) * t;
                    const float ol = driftLine_[0].hermite(dl), orr = driftLine_[1].hermite(dr);
                    l = l * (1.0f - f) + ol * f;
                    r = r * (1.0f - f) + orr * f;
                }
            }
            // Grain: the medium's noise, riding or ducking under what plays.
            {
                const float level = std::max(std::abs(l), std::abs(r));
                inEnv_ += (level - inEnv_) * (level > inEnv_ ? env : envRelease);
                if (grainActive)
                {
                    acc_.grainSignal = std::max(acc_.grainSignal, level);
                    float nl = 0.0f, nr = 0.0f;
                    grainSample(nl, nr);
                    const float g = grainGain_ * followGain() * fadeOf(grain, i);
                    nl *= g;
                    nr *= g;
                    acc_.grainSquare += 0.5f * (nl * nl + nr * nr);
                    ++acc_.grainCount;
                    l += nl;
                    r += nr;
                }
            }
            // Wear: the skew lines always run.
            {
                skewLine_[0].push(l);
                skewLine_[1].push(r);
                if (wearActive)
                {
                    const float f = fadeOf(wear, i);
                    // Under a third of a millisecond: linear interpolation
                    // is enough, and the side it dulls is the lagging one.
                    float wl = skewLine_[0].linear(skewFrom_[0] + (skewTo_[0] - skewFrom_[0]) * t);
                    float wr = skewLine_[1].linear(skewFrom_[1] + (skewTo_[1] - skewFrom_[1]) * t);
                    const float gain = dropFrom_ + (dropTo_ - dropFrom_) * t;
                    wl = wearFilter(0, wl) * gain * skewGain_[0];
                    wr = wearFilter(1, wr) * gain * skewGain_[1];
                    l = l * (1.0f - f) + wl * f;
                    r = r * (1.0f - f) + wr * f;
                }
            }
            // Crush
            if (crushActive)
            {
                const float f = fadeOf(crush, i);
                const float cl = crushSample(0, l), cr = crushSample(1, r);
                if (watched)
                    telemetry_.scope(0.5f * (l + r), 0.5f * (cl + cr));
                l = l * (1.0f - f) + cl * f;
                r = r * (1.0f - f) + cr * f;
            }
            // Output: tilt, cuts, width, gain.
            {
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    float& x = ch == 0 ? l : r;
                    x = tiltHigh_[ch].process(tiltLow_[ch].process(x));
                    if (lowCutOn_)
                        x = outLowCut_[ch].process(x);
                    if (highCutOn_)
                        x = outHighCut_[ch].process(x);
                }
                const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width_;
                l = (mid + side) * output_;
                r = (mid - side) * output_;
            }
            left[i] = l;
            right[i] = r;
            feedback_.addSample(inPeak, std::max(std::abs(l), std::abs(r)), 0.0f);
        }
        publishTelemetry(n);
    }

    // ----- Heat ---------------------------------------------------------------------------------
    void resetHeat() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            heatPre_[ch].reset();
            heatPost_[ch].reset();
            heatBump_[ch].reset();
            heatTiltLow_[ch].reset();
            heatTiltHigh_[ch].reset();
            heatPrevious_[ch] = 0.0;
            heatPreviousF_[ch].character = -1;
            heatDc_[ch] = heatDcIn_[ch] = 0.0f;
        }
        heatDesigned_ = -1;
    }

    void heatControls() noexcept
    {
        const int character = gate_[heat].choice;
        if (character != heatDesigned_)
        {
            heatDesigned_ = character;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                switch (character)
                {
                    case tape:
                        heatPre_[ch].setHighShelf(fs_, 3000.0, 6.0);
                        heatPost_[ch].setHighShelf(fs_, 3000.0, -6.0);
                        break;
                    case iron:
                        heatPre_[ch].setLowShelf(fs_, 160.0, 9.0);
                        heatPost_[ch].setLowShelf(fs_, 160.0, -9.0);
                        break;
                    default:
                        heatPre_[ch].bypass();
                        heatPost_[ch].bypass();
                        break;
                }
            }
        }
        // Drive and its trim glide across the sub-block.
        heatDriveFrom_ = heatDriveTo_;
        heatCompFrom_ = heatCompTo_;
        const double drive = std::pow(10.0, static_cast<double>(heatDriveDb(heatAmount_)) / 20.0);
        heatDeadZone_ = fuzzDeadZone(heatAmount_);
        heatDriveTo_ = static_cast<float>(drive);
        heatCompTo_ = static_cast<float>(heatCompensation(character, drive, heatDeadZone_));
        if (first_ || heatDriveFrom_ <= 0.0f)
        {
            heatDriveFrom_ = heatDriveTo_;
            heatCompFrom_ = heatCompTo_;
        }
        // Tape's head bump and the fuzz's top end follow the amount; the
        // tone is a tilt about 900 Hz.
        if (std::abs(heatAmount_ - heatShaped_) > 0.002f || std::abs(heatTone_ - heatToneShaped_) > 0.002f ||
            first_)
        {
            heatShaped_ = heatAmount_;
            heatToneShaped_ = heatTone_;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                if (character == tape)
                    heatBump_[ch].setPeaking(fs_, 90.0, 1.0, 2.5 * heatAmount_);
                else if (character == fuzz)
                    heatBump_[ch].setLowPass(fs_, 18000.0 * std::pow(5000.0 / 18000.0, heatAmount_), 0.6);
                else
                    heatBump_[ch].bypass();
                heatTiltLow_[ch].setLowShelf(fs_, 900.0, -3.0 * heatTone_);
                heatTiltHigh_[ch].setHighShelf(fs_, 900.0, 3.0 * heatTone_);
            }
        }
    }

    float heatSample(size_t ch, float x, float t) noexcept
    {
        const int character = gate_[heat].choice;
        const double drive = heatDriveFrom_ + (heatDriveTo_ - heatDriveFrom_) * t;
        const double comp = heatCompFrom_ + (heatCompTo_ - heatCompFrom_) * t;
        const double u = drive * heatPre_[ch].process(x);
        // ADAA1: the mean of the curve over the step from the previous input.
        const double previous = heatPrevious_[ch];
        const double du = u - previous;
        double y = 0.0;
        if (std::abs(du) > 1.0e-6)
        {
            // The integral at the previous sample was worked out then, unless the
            // curve (character, dead zone) changed since.
            auto& cached = heatPreviousF_[ch];
            const double fPrevious = cached.character == character && cached.deadZone == heatDeadZone_
                                         ? cached.value
                                         : curveIntegral(character, previous, heatDeadZone_);
            const double fu = curveIntegral(character, u, heatDeadZone_);
            y = (fu - fPrevious) / du;
            cached = {fu, character, heatDeadZone_};
        }
        else
        {
            y = curve(character, 0.5 * (u + previous), heatDeadZone_);
            heatPreviousF_[ch].character = -1;
        }
        heatPrevious_[ch] = u;
        acc_.heatDriven = std::max(acc_.heatDriven, static_cast<float>(std::abs(u)));
        acc_.heatShaped = std::max(acc_.heatShaped, static_cast<float>(std::abs(y)));
        float out = static_cast<float>(y * comp);
        // A DC blocker: the tube's bias and the fuzz's asymmetry leave an offset.
        const float blocked = out - heatDcIn_[ch] + 0.9995f * heatDc_[ch];
        heatDcIn_[ch] = out;
        heatDc_[ch] = dsp::flush(blocked, denormalFloor);
        out = heatPost_[ch].process(blocked);
        out = heatBump_[ch].process(out);
        return heatTiltHigh_[ch].process(heatTiltLow_[ch].process(out));
    }

    // ----- Chamber ------------------------------------------------------------------------------
    // Dattorro's plate, at its design rate.
    static constexpr double plateRate = 29761.0;

    int scaled(double samplesAtPlateRate) const noexcept
    {
        return std::max(1, static_cast<int>(samplesAtPlateRate * fs_ / plateRate));
    }
    int ms(double milliseconds) const noexcept
    {
        return std::max(1, static_cast<int>(milliseconds * 0.001 * fs_));
    }

    void prepareChamber()
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Patina::prepareChamber");
        // Room: early reflections from an input line, then four lines mixed
        // by a Hadamard matrix with damping in each.
        for (auto& line : roomInput_)
            line.prepare(ms(60.0));
        static constexpr double tapsMs[2][6] = {{3.1, 7.3, 11.9, 17.1, 23.3, 31.7},
                                                {4.3, 8.9, 13.1, 19.7, 26.9, 35.3}};
        for (size_t ch = 0; ch < 2; ++ch)
            for (size_t k = 0; k < 6; ++k)
                roomTap_[ch][k] = ms(tapsMs[ch][k]);
        static constexpr double roomLengthsMs[] = {19.3, 23.9, 29.1, 34.7};
        for (size_t i = 0; i < 4; ++i)
        {
            roomLength_[i] = ms(roomLengthsMs[i]);
            roomLine_[i].prepare(roomLength_[i] + 2);
        }
        // Spring: two springs (one a side) of a dispersive allpass chain in a
        // feedback loop.
        springLength_[0] = ms(33.0);
        springLength_[1] = ms(37.7);
        for (size_t ch = 0; ch < 2; ++ch)
            springLine_[ch].prepare(springLength_[ch] + 2);
        // Plate: Dattorro's figure-of-eight tank.
        static constexpr double diffusers[] = {142.0, 107.0, 379.0, 277.0};
        for (size_t i = 0; i < 4; ++i)
            plateDiffuser_[i].prepare(scaled(diffusers[i]));
        const int excursion = scaled(16.0) + 2;
        plateExcursion_ = static_cast<float>(scaled(8.0));
        plateTankAp1_[0].prepare(scaled(672.0), excursion);
        plateTankAp1_[1].prepare(scaled(908.0), excursion);
        plateTankAp2_[0].prepare(scaled(1800.0));
        plateTankAp2_[1].prepare(scaled(2656.0));
        plateDelay1Length_ = {scaled(4453.0), scaled(4217.0)};
        plateDelay2Length_ = {scaled(3720.0), scaled(3163.0)};
        for (size_t i = 0; i < 2; ++i)
        {
            plateDelay1_[i].prepare(plateDelay1Length_[i]);
            plateDelay2_[i].prepare(plateDelay2Length_[i]);
        }
        static constexpr double taps[14] = {266.0,
                                            2974.0,
                                            1913.0,
                                            1996.0,
                                            1990.0,
                                            187.0,
                                            1066.0,
                                            353.0,
                                            3627.0,
                                            1228.0,
                                            2673.0,
                                            2111.0,
                                            335.0,
                                            121.0};
        for (size_t i = 0; i < 14; ++i)
            plateTap_[i] = scaled(taps[i]);
        plateLoopSeconds_ = (672.0 + 908.0 + 1800.0 + 2656.0 + 4453.0 + 4217.0 + 3720.0 + 3163.0) / plateRate;
        resetChamber();
    }

    void resetChamber() noexcept
    {
        for (auto& line : roomInput_)
            line.clear();
        for (auto& line : roomLine_)
            line.clear();
        for (auto& line : springLine_)
            line.clear();
        for (auto& f : roomDamp_)
            f = 0.0f;
        for (auto& chain : springChain_)
            for (auto& s : chain)
                s = {};
        for (auto& f : springLow_)
            f.reset();
        for (auto& f : springHigh_)
            f.reset();
        for (auto& ap : plateDiffuser_)
            ap.line.clear();
        for (size_t i = 0; i < 2; ++i)
        {
            plateTankAp1_[i].line.clear();
            plateTankAp2_[i].line.clear();
            plateDelay1_[i].clear();
            plateDelay2_[i].clear();
            plateDamp_[i] = 0.0f;
        }
        plateBandwidth_ = 0.0f;
        platePhase_ = 0.0f;
        for (auto& f : chamberIn_)
            f.reset();
        for (auto& f : chamberLow_)
            f.reset();
        for (auto& f : chamberHigh_)
            f.reset();
        for (auto& f : roomBox_)
            f.reset();
        chamberDesigned_ = -1.0e9f;
    }

    float chamberWetGain() const noexcept
    {
        switch (gate_[chamber].choice)
        {
            case spring:
                return 0.9f;
            case plate:
                return 1.1f;
            default:
                return 0.85f;
        }
    }

    void chamberControls() noexcept
    {
        const int space = gate_[chamber].choice;
        const float rt = chamberDecaySeconds(space, chamberDecay_);
        for (size_t i = 0; i < 4; ++i)
            roomGain_[i] =
                std::pow(10.0f, -3.0f * static_cast<float>(roomLength_[i]) / (rt * static_cast<float>(fs_)));
        for (size_t ch = 0; ch < 2; ++ch)
            springGain_[ch] = std::pow(
                10.0f, -3.0f * static_cast<float>(springLength_[ch]) / (rt * static_cast<float>(fs_)));
        plateDecay_ = std::pow(10.0f, -0.75f * static_cast<float>(plateLoopSeconds_) / rt);
        // Damping in the loops follows the tone.
        const float brightness = 0.5f * (chamberTone_ + 1.0f); // 0..1
        roomDampK_ = onePoleCoefficient(fs_, 2500.0 * std::pow(4.0, brightness));
        plateDampK_ = onePoleCoefficient(fs_, 3000.0 * std::pow(4.0, brightness));
        plateBandK_ = onePoleCoefficient(fs_, 5000.0 * std::pow(3.0, brightness));
        if (std::abs(chamberTone_ - chamberDesigned_) > 0.002f)
        {
            chamberDesigned_ = chamberTone_;
            const double lowpass = 2500.0 * std::pow(5.0, brightness);
            const double highpass = 260.0 * std::pow(0.4, brightness);
            for (size_t ch = 0; ch < 2; ++ch)
            {
                chamberIn_[ch].setHighPass1(fs_, 120.0);
                chamberLow_[ch].setHighPass(fs_, highpass, 0.6);
                chamberHigh_[ch].setLowPass(fs_, lowpass, 0.6);
                springLow_[ch].setHighPass(fs_, 180.0, 0.7);
                springHigh_[ch].setLowPass(fs_, 3200.0 * std::pow(2.0, brightness), 0.7071);
                roomBox_[ch].setPeaking(fs_, 650.0, 1.0, 3.0);
            }
        }
    }

    void chamberSample(float l, float r, float& wl, float& wr) noexcept
    {
        constexpr float guard = 1.0e-18f; // keeps the tails out of denormals
        l = chamberIn_[0].process(l) + guard;
        r = chamberIn_[1].process(r) + guard;
        switch (gate_[chamber].choice)
        {
            case spring:
            {
                const float mono = 0.5f * (l + r);
                float out[2];
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    float x = mono + springGain_[ch] * springLine_[ch].at(springLength_[ch] - 1);
                    // The chain's dispersion: low frequencies take longer
                    // round the loop than high ones, the spring's chirp.
                    for (auto& s : springChain_[ch])
                    {
                        const float y = springCoefficient * x + s.x1 - springCoefficient * s.y1;
                        s.x1 = x;
                        s.y1 = dsp::flush(y, denormalFloor);
                        x = y;
                    }
                    x = springHigh_[ch].process(x);
                    springLine_[ch].push(x);
                    out[ch] = springLow_[ch].process(x);
                }
                wl = out[0];
                wr = out[1];
                break;
            }
            case plate:
            {
                float x = 0.5f * (l + r);
                plateBandwidth_ += (x - plateBandwidth_) * plateBandK_;
                x = plateBandwidth_;
                x = plateDiffuser_[0].process(x, 0.75f);
                x = plateDiffuser_[1].process(x, 0.75f);
                x = plateDiffuser_[2].process(x, 0.625f);
                x = plateDiffuser_[3].process(x, 0.625f);
                platePhase_ += twoPi * 1.0f / static_cast<float>(fs_);
                if (platePhase_ > twoPi)
                    platePhase_ -= twoPi;
                const float mods[2] = {plateExcursion_ * (1.0f + std::sin(platePhase_)),
                                       plateExcursion_ * (1.0f + std::cos(platePhase_))};
                // The figure of eight: each tank is fed from the other's end.
                const float feed[2] = {plateDelay2_[1].at(plateDelay2Length_[1] - 1),
                                       plateDelay2_[0].at(plateDelay2Length_[0] - 1)};
                for (size_t side = 0; side < 2; ++side)
                {
                    float v = x + plateDecay_ * feed[side];
                    v = plateTankAp1_[side].process(v, -0.7f, mods[side]);
                    plateDelay1_[side].push(v);
                    v = plateDelay1_[side].at(plateDelay1Length_[side] - 1);
                    plateDamp_[side] += (v - plateDamp_[side]) * plateDampK_;
                    v = plateDamp_[side] * plateDecay_;
                    v = plateTankAp2_[side].process(v, 0.5f);
                    plateDelay2_[side].push(v);
                }
                // Dattorro's output taps.
                const auto& d1L = plateDelay1_[0];
                const auto& d1R = plateDelay1_[1];
                const auto& d2L = plateDelay2_[0];
                const auto& d2R = plateDelay2_[1];
                const auto& a2L = plateTankAp2_[0].line;
                const auto& a2R = plateTankAp2_[1].line;
                const auto& t = plateTap_;
                wl = 0.6f * (d1R.at(t[0]) + d1R.at(t[1]) - a2R.at(t[2]) + d2R.at(t[3]) - d1L.at(t[4]) -
                             a2L.at(t[5]) - d2L.at(t[6]));
                wr = 0.6f * (d1L.at(t[7]) + d1L.at(t[8]) - a2L.at(t[9]) + d2L.at(t[10]) - d1R.at(t[11]) -
                             a2R.at(t[12]) - d2R.at(t[13]));
                break;
            }
            default:
            {
                roomInput_[0].push(l);
                roomInput_[1].push(r);
                // Early reflections: a small, boxy room, the walls answering
                // from alternate sides.
                static constexpr float tapGains[6] = {0.62f, -0.48f, 0.41f, -0.33f, 0.27f, -0.2f};
                float early[2] = {0.0f, 0.0f};
                for (size_t ch = 0; ch < 2; ++ch)
                    for (size_t k = 0; k < 6; ++k)
                        early[ch] +=
                            tapGains[k] * roomInput_[ch == 0 ? (k % 2) : 1 - (k % 2)].at(roomTap_[ch][k]);
                float o[4], v[4];
                for (size_t i = 0; i < 4; ++i)
                {
                    o[i] = roomLine_[i].at(roomLength_[i] - 1);
                    roomDamp_[i] += (o[i] - roomDamp_[i]) * roomDampK_;
                    v[i] = roomDamp_[i] * roomGain_[i];
                }
                // Hadamard: orthogonal, so the loop's decay is the lines' own.
                const float h0 = 0.5f * (v[0] + v[1] + v[2] + v[3]);
                const float h1 = 0.5f * (v[0] - v[1] + v[2] - v[3]);
                const float h2 = 0.5f * (v[0] + v[1] - v[2] - v[3]);
                const float h3 = 0.5f * (v[0] - v[1] - v[2] + v[3]);
                roomLine_[0].push(h0 + 0.5f * (l + early[0]));
                roomLine_[1].push(h1 + 0.5f * (r + early[1]));
                roomLine_[2].push(h2 + 0.5f * early[0]);
                roomLine_[3].push(h3 + 0.5f * early[1]);
                wl = roomBox_[0].process(0.7f * early[0] + 0.8f * (o[0] + o[2]));
                wr = roomBox_[1].process(0.7f * early[1] + 0.8f * (o[1] + o[3]));
                break;
            }
        }
        wl = chamberHigh_[0].process(chamberLow_[0].process(wl));
        wr = chamberHigh_[1].process(chamberLow_[1].process(wr));
    }

    // ----- Drift --------------------------------------------------------------------------------
    struct Wobble
    {
        float wowPhase = 0.0f, flutterPhase = 0.0f;
        Contour wowRate, wowShape, wowOffset, flutterShape;
        dsp::XorShift32 rng;
    };

    void resetDrift() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            driftLine_[ch].clear();
            wobble_[ch] = {};
            wobble_[ch].rng = dsp::XorShift32(ch == 0 ? 0x1234567u : 0x7654321u);
            wobble_[ch].flutterPhase = ch == 0 ? 0.0f : 1.7f;
            driftFrom_[ch] = driftTo_[ch] = 0.0f;
            driftScrape_[ch] = 0.0f;
        }
        driftWowDepth_ = driftFlutterDepth_ = 0.0f;
    }

    // One channel's wow and flutter shapes (each about -1..1) for the machine.
    std::pair<float, float> wobbleShapes(size_t ch, int machine, float seconds, float rateScale) noexcept
    {
        auto& w = wobble_[ch];
        float wowHz = 0.45f, flutterHz = 6.5f;
        if (machine == deck)
        {
            wowHz = 0.9f;
            flutterHz = 11.0f;
        }
        else if (machine == disc)
        {
            wowHz = 33.333f / 60.0f; // the record turning
            flutterHz = 7.5f;
        }
        const float wander = machine == disc ? 0.02f : machine == deck ? 0.35f : 0.25f;
        const float rateNoise = w.wowRate.bipolar(seconds, 0.3f, w.rng);
        w.wowPhase += twoPi * wowHz * rateScale * (1.0f + wander * rateNoise) * seconds;
        w.flutterPhase += twoPi * flutterHz * rateScale * (1.0f + 0.5f * wander * rateNoise) * seconds;
        if (w.wowPhase > twoPi)
            w.wowPhase -= twoPi;
        if (w.flutterPhase > twoPi)
            w.flutterPhase -= twoPi;
        const float shape = w.wowShape.step(seconds, 0.25f, w.rng);
        const float offset = w.wowOffset.bipolar(seconds, 0.15f, w.rng);
        float wow = 0.0f;
        if (machine == disc)
            wow = 0.82f * std::sin(w.wowPhase) + 0.18f * std::sin(2.0f * w.wowPhase + 0.7f);
        else
            wow = std::sin(w.wowPhase) * (0.6f + 0.4f * shape) + (machine == deck ? 0.4f : 0.3f) * offset;
        // Flutter: the capstan's tone with its level wandering, and on a deck
        // a rough scrape of fast noise.
        const float flutterLevel = 0.55f + 0.45f * w.flutterShape.step(seconds, 1.5f, w.rng);
        const float scrapeK = 1.0f - std::exp(-twoPi * 30.0f * seconds);
        driftScrape_[ch] += (w.rng.bipolar() - driftScrape_[ch]) * scrapeK;
        float flutter = std::sin(w.flutterPhase) * flutterLevel;
        if (machine == deck)
            flutter = 0.65f * flutter + 0.9f * driftScrape_[ch];
        else
            flutter += 0.25f * driftScrape_[ch];
        return {std::clamp(wow, -1.0f, 1.0f), std::clamp(flutter, -1.2f, 1.2f)};
    }

    void driftControls(int n) noexcept
    {
        const float seconds = static_cast<float>(n) / static_cast<float>(fs_);
        const float rateScale = std::pow(2.0f, 2.0f * driftRate_ - 1.0f); // 0.5x .. 2x
        const int machine = gate_[drift].choice;
        const float wowHz = (machine == deck ? 0.9f : machine == disc ? 0.5556f : 0.45f) * rateScale;
        const float flutterHz = (machine == deck ? 11.0f : machine == disc ? 7.5f : 6.5f) * rateScale;
        // Depths as delay swings: a peak speed error p at f Hz swings the delay
        // by p / (2 pi f) seconds. Up to 2% wow and 0.4% flutter.
        const float wowDepth = 0.02f * wow_ / (twoPi * wowHz) * static_cast<float>(fs_);
        const float flutterDepth = 0.004f * flutter_ / (twoPi * flutterHz) * static_cast<float>(fs_);
        driftWowDepth_ += (wowDepth - driftWowDepth_) * (first_ ? 1.0f : slowSmooth_);
        driftFlutterDepth_ += (flutterDepth - driftFlutterDepth_) * (first_ ? 1.0f : slowSmooth_);
        const auto left = wobbleShapes(0, machine, seconds, rateScale);
        const auto own = wobbleShapes(1, machine, seconds, rateScale);
        const float s = driftStereo_;
        const std::pair<float, float> right{left.first + (own.first - left.first) * s,
                                            left.second + (own.second - left.second) * s};
        const float centre = driftWowDepth_ * 1.05f + driftFlutterDepth_ * 1.25f + 1.0f;
        const float maxDelay = static_cast<float>(driftLine_[0].mask - 8);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const auto& shape = ch == 0 ? left : right;
            driftFrom_[ch] = driftTo_[ch];
            driftTo_[ch] = std::clamp(
                centre + driftWowDepth_ * shape.first + driftFlutterDepth_ * shape.second, 1.0f, maxDelay);
            if (first_)
                driftFrom_[ch] = driftTo_[ch];
            // The speed: the delay growing plays slower.
            const float slope = (driftTo_[ch] - driftFrom_[ch]) / static_cast<float>(n);
            const float cents = 1200.0f * std::log2(std::max(0.5f, 1.0f - slope));
            (ch == 0 ? acc_.driftLeft : acc_.driftRight) += cents * static_cast<float>(n);
        }
        acc_.driftCount += n;
    }

    // ----- Grain --------------------------------------------------------------------------------
    struct NoiseChannel
    {
        dsp::XorShift32 rng;
        float clickEnv = 0.0f, clickDecay = 0.0f, popEnv = 0.0f, popDecay = 0.0f;
        Biquad clickTone, popTone, surface, surfaceTop, rumble, hissLow, hissPeak, hissTop, airTop, airDrone;
        Pink pink;
        Contour level;
    };

    void resetGrain() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            noise_[ch] = {};
            noise_[ch].rng = dsp::XorShift32(ch == 0 ? 0x2468aceu : 0x13579bdu);
        }
        humPhase_ = 0.0f;
        humLevel_ = {};
        recordPhase_ = 0.0f;
        grainDesigned_ = -1;
        grainTextureDesigned_ = -1.0f;
    }

    void grainControls(float seconds) noexcept
    {
        const int source = gate_[grain].choice;
        const float a = grainAmount_;
        // Level: -60 dB rising to -16 dB at full; nothing at all at zero.
        grainGain_ = a <= 0.0f ? 0.0f : dbToGain(-60.0f + 44.0f * a) * std::min(1.0f, a * 12.0f);
        if (source != grainDesigned_ || std::abs(grainTexture_ - grainTextureDesigned_) > 0.002f)
        {
            grainDesigned_ = source;
            grainTextureDesigned_ = grainTexture_;
            const double t = grainTexture_;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto& c = noise_[ch];
                c.clickTone.setHighPass(fs_, 1500.0, 0.7);
                c.popTone.setLowPass(fs_, 1800.0, 0.8);
                c.surface.setHighPass(fs_, 900.0, 0.7);
                c.surfaceTop.setLowPass(fs_, 7000.0, 0.7);
                c.rumble.setLowPass(fs_, 24.0, 0.9);
                c.hissLow.setHighPass(fs_, 180.0, 0.7);
                c.hissPeak.setPeaking(fs_, 6000.0, 0.8, 4.0);
                c.hissTop.setLowPass(fs_, 4000.0 * std::pow(4.5, t), 0.7);
                c.airTop.setLowPass(fs_, 1200.0 * std::pow(7.0, t), 0.6);
                c.airDrone.setBandPass(fs_, 95.0, 2.5);
            }
            // The hum's harmonics: a pure tone at no texture, a buzz at full.
            float total = 0.0f;
            for (size_t k = 0; k < humHarmonics; ++k)
            {
                const auto harmonic = static_cast<float>(k + 1);
                const float buzz = static_cast<float>(t);
                float amp = 1.0f;
                if (k > 0)
                    amp = (0.08f + 0.92f * std::pow(buzz, 0.7f)) * ((k + 1) % 2 == 1 ? 0.9f : 0.55f) /
                          std::pow(harmonic, 1.3f - 0.7f * buzz);
                humAmp_[k] = amp;
                total += amp * amp;
            }
            const float norm = 1.0f / std::sqrt(std::max(1.0e-6f, 0.5f * total));
            for (auto& amp : humAmp_)
                amp *= norm;
        }
        // Crackle density: 2 to 300 a second.
        clickProbability_ = 2.0f * std::pow(150.0f, grainTexture_) / static_cast<float>(fs_);
        humLevel_.step(seconds, 0.2f, noise_[0].rng);
    }

    float followGain() const noexcept
    {
        // The input's level from -50 to -20 dBFS as 0..1.
        const float x = std::clamp((gainToDb(inEnv_) + 50.0f) / 30.0f, 0.0f, 1.0f);
        if (grainFollow_ >= 0.0f)
            return 1.0f + (x * x - 1.0f) * grainFollow_;             // rides: gone in silence
        return 1.0f + (dbToGain(-18.0f * x) - 1.0f) * -grainFollow_; // ducks under the music
    }

    void grainSample(float& outL, float& outR) noexcept
    {
        const int source = gate_[grain].choice;
        if (source == hum50 || source == hum60)
        {
            const float hz = source == hum50 ? 50.0f : 60.0f;
            humPhase_ += twoPi * hz / static_cast<float>(fs_);
            if (humPhase_ > twoPi)
                humPhase_ -= twoPi;
            const float s1 = std::sin(humPhase_), c2 = 2.0f * std::cos(humPhase_);
            float previous = 0.0f, current = s1, sum = 0.0f;
            for (size_t k = 0; k < humHarmonics; ++k)
            {
                sum += humAmp_[k] * current;
                const float next = c2 * current - previous;
                previous = current;
                current = next;
            }
            const float level = 0.9f + 0.2f * humLevel_.b;
            outL = sum * level;
            outR = sum * level * 0.97f;
            return;
        }
        if (source == vinyl)
        {
            recordPhase_ += twoPi * (33.333f / 60.0f) / static_cast<float>(fs_);
            if (recordPhase_ > twoPi)
                recordPhase_ -= twoPi;
            // A shared event decides where each crackle lands.
            auto& rng = noise_[0].rng;
            if (rng.uniform() < clickProbability_)
            {
                const float u = std::max(1.0e-4f, rng.uniform());
                const bool pop = rng.uniform() < 0.07f;
                const float amp = std::min(1.0f, 0.18f * std::pow(-std::log(u), 1.6f)) * (pop ? 2.5f : 1.0f);
                const float pan = rng.uniform();
                const float gl = pan < 0.5f ? 1.0f : 2.0f * (1.0f - pan) + 0.15f;
                const float gr = pan > 0.5f ? 1.0f : 2.0f * pan + 0.15f;
                const float decay = std::exp(
                    -1.0f / (static_cast<float>(fs_) * (pop ? 0.0018f : 0.00018f) * (0.5f + rng.uniform())));
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    auto& c = noise_[ch];
                    const float g = amp * (ch == 0 ? gl : gr);
                    if (pop)
                    {
                        c.popEnv = std::max(c.popEnv, g);
                        c.popDecay = decay;
                    }
                    else
                    {
                        c.clickEnv = std::max(c.clickEnv, g);
                        c.clickDecay = decay;
                    }
                }
            }
            const float swish = 0.75f + 0.25f * std::sin(recordPhase_);
            const float rumble = noise_[0].rumble.process(noise_[0].rng.bipolar()) * 18.0f;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto& c = noise_[ch];
                const float white = c.rng.bipolar();
                const float click = c.clickTone.process(white * c.clickEnv) * 14.0f;
                const float popSign = white > 0.0f ? 1.0f : -1.0f;
                const float pop = c.popTone.process(popSign * c.popEnv) * 3.0f;
                c.clickEnv = dsp::flush(c.clickEnv * c.clickDecay, denormalFloor);
                c.popEnv = dsp::flush(c.popEnv * c.popDecay, denormalFloor);
                const float surface = c.surfaceTop.process(c.surface.process(c.rng.bipolar())) * 2.4f * swish;
                (ch == 0 ? outL : outR) = click + pop + surface + rumble;
            }
            return;
        }
        for (size_t ch = 0; ch < 2; ++ch)
        {
            auto& c = noise_[ch];
            const float white = c.rng.bipolar();
            float y = 0.0f;
            if (source == hiss)
                y = c.hissTop.process(c.hissPeak.process(c.hissLow.process(white))) * 2.6f;
            else
            {
                // Room air: soft, wide noise over a low drone.
                const float drift = 0.8f + 0.4f * c.level.step(1.0f / static_cast<float>(fs_), 0.12f, c.rng);
                y = c.airTop.process(c.pink.process(white)) * 1.9f * drift + c.airDrone.process(white) * 3.0f;
            }
            (ch == 0 ? outL : outR) = y;
        }
    }

    // ----- Wear ---------------------------------------------------------------------------------
    void resetWear() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            wearLow_[ch].reset();
            wearHigh_[ch].reset();
            wearBump_[ch].reset();
            skewFrom_[ch] = skewTo_[ch] = 0.0f;
            skewGain_[ch] = 1.0f;
        }
        dropRemaining_ = 0;
        dropDip_ = 0.0f;
        dropDepthDb_ = 0.0f;
        dropFrom_ = dropTo_ = 1.0f;
        wearDesigned_ = {-1.0f, -1.0f};
        skewShape_ = {};
        skewLevel_ = {};
    }

    void wearControls(int n, float seconds) noexcept
    {
        // Dropouts arrive at random, up to three a second, each a dip of 3 to
        // 27 dB lasting 20 to 220 ms, the highs lost deeper.
        const float d = wearDropouts_;
        if (dropRemaining_ <= 0 && d > 0.0f && wearRng_.uniform() < 3.0f * d * d * seconds)
        {
            dropRemaining_ = static_cast<int>((0.02f + 0.2f * wearRng_.uniform()) * static_cast<float>(fs_));
            dropDepthDb_ = 3.0f + (6.0f + 18.0f * d) * wearRng_.uniform();
        }
        const bool dropping = dropRemaining_ > 0;
        dropRemaining_ -= n;
        const float k = smoothingCoefficient(fs_ / static_cast<double>(n), dropping ? 0.004f : 0.03f);
        dropDip_ += ((dropping ? 1.0f : 0.0f) - dropDip_) * k;
        if (dropDip_ < 1.0e-4f)
            dropDip_ = 0.0f;
        dropFrom_ = dropTo_;
        dropTo_ = dbToGain(-dropDepthDb_ * dropDip_);
        if (first_)
            dropFrom_ = dropTo_;

        // Skew: the azimuth wanders, the right channel ahead or behind by up
        // to 0.35 ms, the lagging side duller and the levels apart.
        const float shape = skewShape_.bipolar(seconds, 0.08f, wearRng_);
        const float levelShape = skewLevel_.bipolar(seconds, 0.11f, wearRng_);
        const float lagMs = 0.35f * wearSkew_ * shape;
        const float lag = lagMs * 0.001f * static_cast<float>(fs_);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            skewFrom_[ch] = skewTo_[ch];
            skewTo_[ch] = ch == 0 ? std::max(0.0f, -lag) : std::max(0.0f, lag);
            if (first_)
                skewFrom_[ch] = skewTo_[ch];
            skewGain_[ch] = dbToGain((ch == 0 ? 1.0f : -1.0f) * 1.2f * wearSkew_ * levelShape);
        }
        const float base = wearCutoffHz(wearLoss_) * (1.0f - 0.75f * dropDip_);
        for (size_t ch = 0; ch < 2; ++ch)
        {
            const bool lagging = (ch == 1) == (lag > 0.0f);
            const float cutoff = base * (lagging ? 1.0f - 0.35f * wearSkew_ * std::abs(shape) : 1.0f);
            if (std::abs(cutoff - wearDesigned_[ch]) > 0.002f * wearDesigned_[ch] || first_)
            {
                wearDesigned_[ch] = cutoff;
                wearHigh_[ch].setLowPass(fs_, std::min(cutoff, static_cast<float>(fs_) * 0.45f), 0.75);
                wearLow_[ch].setHighPass1(fs_, 10.0 + 70.0 * wearLoss_ * wearLoss_);
                wearBump_[ch].setPeaking(fs_, 110.0, 1.0, 1.5 * wearLoss_);
            }
        }
        acc_.wearDip = std::max(acc_.wearDip, dropDepthDb_ * dropDip_);
        acc_.wearCutoff = base;
        acc_.wearSkew = lagMs;
    }

    float wearFilter(size_t ch, float x) noexcept
    {
        return wearBump_[ch].process(wearHigh_[ch].process(wearLow_[ch].process(x)));
    }

    // ----- Crush --------------------------------------------------------------------------------
    void resetCrush() noexcept
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            for (auto& f : crushPre_[ch])
                f.reset();
            for (auto& f : crushPost_[ch])
                f.reset();
            crushHeld_[ch] = 0.0f;
        }
        crushPhase_ = 1.0f;
        crushThreshold_ = 1.0f;
        crushDesigned_ = -1.0f;
    }

    void crushControls() noexcept
    {
        crushBitsNow_ = crushBitsFor(crushBitsAmount_);
        crushRateNow_ = crushRateFor(crushRateAmount_);
        crushLevels_ = std::pow(2.0f, crushBitsNow_ - 1.0f);
        const int flavour = gate_[crush].choice;
        crushFiltered_ = flavour == soft || flavour == mulaw;
        if (crushFiltered_ && std::abs(crushRateNow_ - crushDesigned_) > 0.002f * crushDesigned_)
        {
            crushDesigned_ = crushRateNow_;
            // Eighth-order Butterworth either side of the hold, below its
            // Nyquist: a converter of the time with good filters.
            static constexpr double qs[4] = {0.5098, 0.6013, 0.9000, 2.5629};
            const double hz = std::min(0.4 * crushRateNow_, 0.45 * fs_);
            for (size_t ch = 0; ch < 2; ++ch)
                for (size_t k = 0; k < 4; ++k)
                {
                    crushPre_[ch][k].setLowPass(fs_, hz, qs[k]);
                    crushPost_[ch][k].setLowPass(fs_, hz, qs[k]);
                }
        }
        acc_.crushBits = crushBitsNow_;
        acc_.crushRate = crushRateNow_;
    }

    float crushSample(size_t ch, float x) noexcept
    {
        const int flavour = gate_[crush].choice;
        if (crushFiltered_)
            for (auto& f : crushPre_[ch])
                x = f.process(x);
        // The hold: a new sample each time the converter's clock ticks (one
        // clock for both sides, as a real converter).
        if (ch == 0)
        {
            crushTick_ = false;
            crushPhase_ += crushRateNow_ / static_cast<float>(fs_);
            if (crushPhase_ >= crushThreshold_)
            {
                crushPhase_ -= crushThreshold_;
                crushTick_ = true;
                crushThreshold_ = flavour == jitter ? 1.0f + 0.45f * crushRng_.bipolar() : 1.0f;
            }
        }
        if (crushTick_ || crushRateNow_ >= static_cast<float>(fs_))
        {
            float v = std::clamp(x, -1.0f, 1.0f);
            if (flavour == mulaw)
            {
                constexpr float mu = 255.0f;
                const float c = std::copysign(std::log1p(mu * std::abs(v)) / std::log1p(mu), v);
                const float q = std::round(c * crushLevels_) / crushLevels_;
                v = std::copysign((std::pow(1.0f + mu, std::abs(q)) - 1.0f) / mu, q);
            }
            else
                v = std::round(v * crushLevels_) / crushLevels_;
            crushHeld_[ch] = v;
        }
        float y = crushHeld_[ch];
        if (crushFiltered_)
            for (auto& f : crushPost_[ch])
                y = f.process(y);
        return y;
    }

    // ----- Output -------------------------------------------------------------------------------
    void outputControls() noexcept
    {
        if (std::abs(tone_ - lastTone_) > 0.002f)
        {
            lastTone_ = tone_;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                tiltLow_[ch].setLowShelf(fs_, 900.0, -3.0 * tone_);
                tiltHigh_[ch].setHighShelf(fs_, 900.0, 3.0 * tone_);
            }
        }
        lowCutOn_ = lowCut_ > 20.5f;
        if (lowCutOn_ && std::abs(lowCut_ - lastLowCut_) > 0.002f * lowCut_)
        {
            lastLowCut_ = lowCut_;
            for (auto& f : outLowCut_)
                f.setHighPass(fs_, lowCut_, 0.707);
        }
        highCutOn_ = highCut_ < 19900.0f;
        if (highCutOn_ && std::abs(highCut_ - lastHighCut_) > 0.002f * highCut_)
        {
            lastHighCut_ = highCut_;
            for (auto& f : outHighCut_)
                f.setLowPass(fs_, std::min(static_cast<double>(highCut_), 0.45 * fs_), 0.707);
        }
    }

    // ----- telemetry ----------------------------------------------------------------------------
    struct Accumulator
    {
        float heatLevel = 0, heatDriven = 0, heatShaped = 0, chamberWet = 0;
        float driftLeft = 0, driftRight = 0;
        int driftCount = 0;
        float grainSquare = 0, grainSignal = 0;
        int grainCount = 0;
        float wearDip = 0, wearCutoff = 20000, wearSkew = 0, crushBits = 16, crushRate = 48000;
    };

    void publishTelemetry(int n) noexcept
    {
        frameCount_ += n;
        if (frameCount_ < frameInterval_)
            return;
        frameCount_ -= frameInterval_;
        Telemetry::Frame f{};
        for (int s = 0; s < stageCount; ++s)
            f[static_cast<size_t>(Telemetry::amountHeat + s)] =
                gate_[static_cast<size_t>(s)].on ? amount_[static_cast<size_t>(s)] : 0.0f;
        f[Telemetry::heatLevel] = acc_.heatLevel;
        f[Telemetry::heatSquash] =
            acc_.heatShaped > 1.0e-5f ? std::max(0.0f, gainToDb(acc_.heatDriven / acc_.heatShaped)) : 0.0f;
        f[Telemetry::chamberWet] = acc_.chamberWet;
        const float count = static_cast<float>(std::max(1, acc_.driftCount));
        f[Telemetry::driftLeft] = gate_[drift].on ? acc_.driftLeft / count : 0.0f;
        f[Telemetry::driftRight] = gate_[drift].on ? acc_.driftRight / count : 0.0f;
        f[Telemetry::grainNoise] =
            acc_.grainCount > 0 ? std::sqrt(acc_.grainSquare / static_cast<float>(acc_.grainCount)) : 0.0f;
        f[Telemetry::grainSignal] = acc_.grainSignal;
        f[Telemetry::wearDipDb] = acc_.wearDip;
        f[Telemetry::wearCutoff] = acc_.wearCutoff;
        f[Telemetry::wearSkewMs] = acc_.wearSkew;
        f[Telemetry::crushBits] = acc_.crushBits;
        f[Telemetry::crushRate] = acc_.crushRate;
        telemetry_.publish(f);
        acc_ = {};
    }

    static constexpr size_t humHarmonics = 15;
    static constexpr float springCoefficient = 0.62f;
    static constexpr size_t springStages = 20;

    engine::ProcessorFeedback feedback_;
    Telemetry telemetry_;
    double fs_ = 48000.0;
    Params p_;
    bool first_ = true;
    float fadeStep_ = 0.0f, smooth_ = 0.0f, slowSmooth_ = 0.0f;
    std::array<StageGate, stageCount> gate_{};
    std::array<float, stageCount> fadeFrom_{}, amount_{}, breathGain_{};
    std::array<Contour, stageCount> breath_{};
    dsp::XorShift32 breathRng_{0xb4ea7u};
    int frameInterval_ = 240, frameCount_ = 0;
    Accumulator acc_;

    // Smoothed controls.
    float heatAmount_ = 0, heatTone_ = 0, chamberMix_ = 0, chamberTone_ = 0, chamberDecay_ = 0;
    float wow_ = 0, flutter_ = 0, driftRate_ = 0.5f, driftStereo_ = 0;
    float grainAmount_ = 0, grainTexture_ = 0, grainFollow_ = 0;
    float wearLoss_ = 0, wearDropouts_ = 0, wearSkew_ = 0;
    float crushBitsAmount_ = 0, crushRateAmount_ = 0;
    float tone_ = 0, width_ = 1, output_ = 1, lowCut_ = 20, highCut_ = 20000;

    // Heat
    std::array<Biquad, 2> heatPre_, heatPost_, heatBump_, heatTiltLow_, heatTiltHigh_;
    std::array<double, 2> heatPrevious_{};
    // The curve's integral at heatPrevious_, and what it was worked out with.
    struct HeatIntegral
    {
        double value = 0.0;
        int character = -1; // -1: not worked out
        double deadZone = 0.0;
    };
    std::array<HeatIntegral, 2> heatPreviousF_{};
    std::array<float, 2> heatDc_{}, heatDcIn_{};
    float heatDriveFrom_ = 0, heatDriveTo_ = 0, heatCompFrom_ = 1, heatCompTo_ = 1;
    double heatDeadZone_ = 0.02;
    int heatDesigned_ = -1;
    float heatShaped_ = -1, heatToneShaped_ = -1;

    // Chamber
    std::array<dsp::DelayLine, 2> roomInput_;
    std::array<dsp::DelayLine, 4> roomLine_;
    std::array<int, 4> roomLength_{};
    std::array<float, 4> roomGain_{}, roomDamp_{};
    float roomDampK_ = 0.5f;
    struct AllpassState
    {
        float x1 = 0.0f, y1 = 0.0f;
    };
    std::array<std::array<AllpassState, springStages>, 2> springChain_{};
    std::array<dsp::DelayLine, 2> springLine_;
    std::array<int, 2> springLength_{};
    std::array<float, 2> springGain_{};
    std::array<Biquad, 2> springLow_, springHigh_;
    std::array<Allpass, 4> plateDiffuser_;
    std::array<Allpass, 2> plateTankAp1_, plateTankAp2_;
    std::array<dsp::DelayLine, 2> plateDelay1_, plateDelay2_;
    std::array<float, 2> plateDamp_{};
    std::array<int, 2> plateDelay1Length_{1, 1}, plateDelay2Length_{1, 1};
    std::array<int, 14> plateTap_{};
    std::array<std::array<int, 6>, 2> roomTap_{};
    float plateExcursion_ = 1.0f;
    float plateBandwidth_ = 0, plateDecay_ = 0.5f, plateDampK_ = 0.5f, plateBandK_ = 0.5f, platePhase_ = 0;
    double plateLoopSeconds_ = 0.7;
    std::array<Biquad, 2> chamberIn_, chamberLow_, chamberHigh_, roomBox_;
    float chamberDesigned_ = -1.0e9f;

    // Drift
    std::array<dsp::DelayLine, 2> driftLine_;
    std::array<Wobble, 2> wobble_;
    std::array<float, 2> driftFrom_{}, driftTo_{}, driftScrape_{};
    float driftWowDepth_ = 0, driftFlutterDepth_ = 0;

    // Grain
    std::array<NoiseChannel, 2> noise_;
    std::array<float, humHarmonics> humAmp_{};
    float humPhase_ = 0, recordPhase_ = 0, grainGain_ = 0, clickProbability_ = 0, inEnv_ = 0;
    Contour humLevel_;
    int grainDesigned_ = -1;
    float grainTextureDesigned_ = -1;

    // Wear
    std::array<dsp::DelayLine, 2> skewLine_;
    std::array<Biquad, 2> wearLow_, wearHigh_, wearBump_;
    std::array<float, 2> skewFrom_{}, skewTo_{}, skewGain_{1.0f, 1.0f}, wearDesigned_{-1.0f, -1.0f};
    Contour skewShape_, skewLevel_;
    dsp::XorShift32 wearRng_{0xd3a7u};
    int dropRemaining_ = 0;
    float dropDip_ = 0, dropDepthDb_ = 0, dropFrom_ = 1, dropTo_ = 1;

    // Crush
    std::array<std::array<Biquad, 4>, 2> crushPre_, crushPost_;
    std::array<float, 2> crushHeld_{};
    float crushPhase_ = 1, crushThreshold_ = 1, crushDesigned_ = -1, crushBitsNow_ = 16,
          crushRateNow_ = 48000, crushLevels_ = 32768;
    bool crushFiltered_ = false, crushTick_ = false;
    dsp::XorShift32 crushRng_{0xc0ffeeu};

    // Output
    std::array<Biquad, 2> tiltLow_, tiltHigh_, outLowCut_, outHighCut_;
    float lastTone_ = -1.0e9f, lastLowCut_ = -1.0e9f, lastHighCut_ = -1.0e9f;
    bool lowCutOn_ = false, highCutOn_ = false;
};
} // namespace

const Telemetry* telemetryOf(const engine::InsertProcessor* processor) noexcept
{
    const auto* patina = dynamic_cast<const Patina*>(processor);
    return patina != nullptr ? &patina->telemetry() : nullptr;
}

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Patina>();
}

const std::vector<EffectPreset>& presets()
{
    static const std::vector<EffectPreset> list = []
    {
        PresetBuilder made(type, parameterInfos());
        constexpr double off = 0.0, on = 1.0;
        made.add("Gentle Age", {});
        made.add("Reel Warmth",
                 {{heatCharacter, tape},
                  {heatDrive, 45.0f},
                  {heatTone, -15.0f},
                  {driftWow, 12.0f},
                  {driftFlutter, 10.0f},
                  {driftBreath, 10.0f},
                  {grainSource, hiss},
                  {grainAmount, 12.0f},
                  {wearLoss, 15.0f},
                  {wearDropouts, 0.0f},
                  {wearSkew, 10.0f}});
        made.add("Worn Cassette",
                 {{heatCharacter, tape},
                  {heatDrive, 40.0f},
                  {driftMachine, deck},
                  {driftWow, 40.0f},
                  {driftFlutter, 35.0f},
                  {driftBreath, 30.0f},
                  {grainSource, hiss},
                  {grainAmount, 35.0f},
                  {grainTexture, 50.0f},
                  {wearLoss, 55.0f},
                  {wearDropouts, 25.0f},
                  {wearSkew, 30.0f},
                  {lowCut, 40.0f},
                  {highCut, 12000.0f}});
        made.add("Dusty Vinyl",
                 {{heatDrive, 25.0f},
                  {driftMachine, disc},
                  {driftWow, 20.0f},
                  {driftFlutter, 5.0f},
                  {grainSource, vinyl},
                  {grainAmount, 45.0f},
                  {grainTexture, 45.0f},
                  {wearLoss, 35.0f},
                  {wearDropouts, 0.0f},
                  {lowCut, 30.0f}});
        made.add("Spring Tank",
                 {{heatDrive, 30.0f},
                  {chamberOn, on},
                  {chamberType, spring},
                  {chamberMix, 40.0f},
                  {chamberDecay, 55.0f},
                  {chamberTone, -10.0f},
                  {driftOn, off},
                  {grainOn, off},
                  {wearLoss, 15.0f},
                  {wearDropouts, 0.0f},
                  {wearSkew, 0.0f}});
        made.add("Plate Memory",
                 {{heatDrive, 20.0f},
                  {chamberOn, on},
                  {chamberType, plate},
                  {chamberMix, 35.0f},
                  {chamberDecay, 60.0f},
                  {driftWow, 15.0f},
                  {driftFlutter, 10.0f},
                  {grainSource, air},
                  {grainAmount, 15.0f},
                  {wearLoss, 20.0f},
                  {wearDropouts, 0.0f}});
        made.add("Old Radio",
                 {{heatCharacter, iron},
                  {heatDrive, 55.0f},
                  {chamberOn, on},
                  {chamberType, room},
                  {chamberMix, 15.0f},
                  {chamberDecay, 20.0f},
                  {driftOn, off},
                  {grainSource, hum50},
                  {grainAmount, 15.0f},
                  {grainTexture, 60.0f},
                  {wearLoss, 40.0f},
                  {wearDropouts, 15.0f},
                  {lowCut, 300.0f},
                  {highCut, 3500.0f},
                  {width, 0.0f}});
        made.add("Phone Line",
                 {{heatCharacter, fuzz},
                  {heatDrive, 30.0f},
                  {driftOn, off},
                  {grainOn, off},
                  {wearLoss, 10.0f},
                  {wearDropouts, 20.0f},
                  {wearSkew, 0.0f},
                  {crushOn, on},
                  {crushFlavour, mulaw},
                  {crushBits, 8.0f},
                  {crushRate, 8000.0f},
                  {lowCut, 400.0f},
                  {highCut, 3400.0f},
                  {width, 0.0f}});
        made.add("Lo-Fi Sampler",
                 {{heatCharacter, tape},
                  {heatDrive, 30.0f},
                  {driftOn, off},
                  {grainOn, off},
                  {wearLoss, 20.0f},
                  {wearDropouts, 0.0f},
                  {wearSkew, 0.0f},
                  {crushOn, on},
                  {crushFlavour, hard},
                  {crushBits, 12.0f},
                  {crushRate, 26000.0f}});
        made.add("Basement Tape",
                 {{heatCharacter, tape},
                  {heatDrive, 50.0f},
                  {chamberOn, on},
                  {chamberType, room},
                  {chamberMix, 20.0f},
                  {driftWow, 30.0f},
                  {driftFlutter, 25.0f},
                  {driftBreath, 40.0f},
                  {grainSource, hiss},
                  {grainAmount, 40.0f},
                  {grainFollow, -40.0f},
                  {wearLoss, 45.0f},
                  {wearSkew, 50.0f}});
        made.add("Faded Film",
                 {{heatDrive, 30.0f},
                  {chamberOn, on},
                  {chamberType, room},
                  {chamberMix, 10.0f},
                  {driftWow, 35.0f},
                  {driftFlutter, 25.0f},
                  {driftBreath, 30.0f},
                  {grainSource, air},
                  {grainAmount, 25.0f},
                  {wearLoss, 50.0f},
                  {wearDropouts, 15.0f},
                  {tone, -30.0f},
                  {width, 70.0f}});
        made.add("Broken Machine",
                 {{age, 85.0f},
                  {heatCharacter, fuzz},
                  {heatDrive, 60.0f},
                  {driftMachine, deck},
                  {driftWow, 60.0f},
                  {driftFlutter, 50.0f},
                  {driftBreath, 50.0f},
                  {grainAmount, 40.0f},
                  {wearLoss, 50.0f},
                  {wearDropouts, 60.0f},
                  {crushOn, on},
                  {crushFlavour, jitter},
                  {crushBits, 10.0f},
                  {crushRate, 16000.0f}});
        return made.done();
    }();
    return list;
}

} // namespace northlight::plugins::patina
