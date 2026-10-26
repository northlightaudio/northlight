#include "plugins/Patina.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/Biquad.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/ParamHelpers.h"
#include "plugins/dsp/Random.h"
#include "plugins/patina/Chamber.h"
#include "plugins/patina/Crush.h"
#include "plugins/patina/Drift.h"
#include "plugins/patina/Grain.h"
#include "plugins/patina/Heat.h"
#include "plugins/patina/Parts.h"
#include "plugins/patina/Wear.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

// The processor: the parameters, each stage's switch and choice (faded so
// neither clicks), Age and Breath over the stages' amounts, the stages in
// signal order (plugins/patina/), the output section and the telemetry.

namespace northlight::plugins::patina
{

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

namespace
{

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

class Patina final : public FeedbackBuiltIn
{
public:
    Patina() : FeedbackBuiltIn(type, parameterInfos()) {}

    const Telemetry& telemetry() const noexcept { return telemetry_; }

private:
    // ----- preparing ----------------------------------------------------------------------------
    void prepareBuiltIn(double sampleRate, int) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Patina::prepareBuiltIn");
        fs_ = sampleRate;
        const float rate = static_cast<float>(sampleRate);
        telemetry_.prepare(sampleRate);
        frameInterval_ = std::max(1, static_cast<int>(sampleRate * Telemetry::frameSeconds));
        frameCount_ = 0;
        acc_ = {};
        fadeStep_ = 1.0f / (rate * stageFadeSeconds);
        smooth_ = smoothingCoefficient(sampleRate / subBlock, 0.04f);
        slowSmooth_ = smoothingCoefficient(sampleRate / subBlock, 0.3f);

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
        heat_.prepare(sampleRate);
        chamber_.prepare(sampleRate);
        drift_.prepare(sampleRate);
        grain_.prepare(sampleRate);
        wear_.prepare(sampleRate);
        crush_.prepare(sampleRate);
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
        grain_.flush();
        drift_.flush();
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
        const float paceRate = paceHz(p.pace);
        for (int s = 0; s < stageCount; ++s)
        {
            const auto i = static_cast<size_t>(s);
            const float contour =
                breath_[i].step(seconds, paceRate * (0.8f + 0.1f * static_cast<float>(s)), breathRng_);
            breathGain_[i] = 1.0f - p.breath[i] * contour;
        }
        const auto amount = [&](int stage, float value)
        { return aged(value, p.age) * breathGain_[static_cast<size_t>(stage)]; };
        follow(heat_.amount, amount(heat, p.heatDrive), smooth_);
        follow(heat_.tone, p.heatTone, smooth_);
        follow(chamber_.mix, amount(chamber, p.chamberMix), smooth_);
        follow(chamber_.tone, p.chamberTone, smooth_);
        follow(chamber_.decay, p.chamberDecay, smooth_);
        follow(drift_.wow, amount(drift, p.wow), smooth_);
        follow(drift_.flutter, amount(drift, p.flutter), smooth_);
        follow(drift_.rate, p.driftRate, slowSmooth_);
        follow(drift_.stereo, p.driftStereo, smooth_);
        follow(grain_.amount, amount(grain, p.grainAmount), smooth_);
        follow(grain_.texture, p.grainTexture, smooth_);
        follow(grain_.follow, p.grainFollow, smooth_);
        follow(wear_.loss, amount(wear, p.wearLoss), smooth_);
        follow(wear_.dropouts, amount(wear, p.wearDropouts), smooth_);
        follow(wear_.skew, amount(wear, p.wearSkew), smooth_);
        follow(crush_.bitsAmount, amount(crush, p.crushBits), smooth_);
        follow(crush_.rateAmount, amount(crush, p.crushRate), smooth_);
        follow(tone_, p.tone, smooth_);
        follow(width_, p.width, smooth_);
        follow(output_, p.output, smooth_);
        follow(lowCut_, p.lowCut, smooth_);
        follow(highCut_, p.highCut, smooth_);

        amount_ = {heat_.amount,
                   chamber_.mix,
                   std::max(drift_.wow, drift_.flutter),
                   grain_.amount,
                   std::max({wear_.loss, wear_.dropouts, wear_.skew}),
                   std::max(crush_.bitsAmount, crush_.rateAmount)};

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
        heat_.controls(gate_[heat].choice, first_);
        chamber_.controls(gate_[chamber].choice);
        drift_.controls(gate_[drift].choice, n, slowSmooth_, first_, acc_);
        grain_.controls(gate_[grain].choice, seconds);
        wear_.controls(n, seconds, first_, acc_);
        crush_.controls(gate_[crush].choice, acc_);
        outputControls();
    }

    void resetStage(int stage) noexcept
    {
        switch (stage)
        {
            case heat:
                heat_.reset();
                break;
            case chamber:
                chamber_.reset();
                break;
            case drift:
                break; // its line keeps running, so it comes back seamlessly
            case grain:
                grain_.reset();
                break;
            case wear:
                wear_.reset();
                break;
            default:
                crush_.reset();
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
                const float hl = heat_.sample(0, l, t, acc_), hr = heat_.sample(1, r, t, acc_);
                l = l * (1.0f - f) + hl * f;
                r = r * (1.0f - f) + hr * f;
            }
            // Chamber
            if (chamberActive)
            {
                float wl = 0.0f, wr = 0.0f;
                chamber_.sample(l, r, wl, wr);
                const float g = chamber_.mix * fadeOf(chamber, i) * chamber_.wetGain();
                acc_.chamberWet = std::max(acc_.chamberWet, std::max(std::abs(wl), std::abs(wr)) * g);
                l += wl * g;
                r += wr * g;
            }
            // Drift: the line always runs, so switching it on is seamless.
            {
                drift_.push(l, r);
                if (driftActive)
                {
                    const float f = fadeOf(drift, i);
                    const float ol = drift_.read(0, t), orr = drift_.read(1, t);
                    l = l * (1.0f - f) + ol * f;
                    r = r * (1.0f - f) + orr * f;
                }
            }
            // Grain: the medium's noise, riding or ducking under what plays.
            {
                const float level = std::max(std::abs(l), std::abs(r));
                grain_.track(level, env, envRelease);
                if (grainActive)
                {
                    acc_.grainSignal = std::max(acc_.grainSignal, level);
                    float nl = 0.0f, nr = 0.0f;
                    grain_.sample(nl, nr);
                    const float g = grain_.gain() * fadeOf(grain, i);
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
                wear_.push(l, r);
                if (wearActive)
                {
                    const float f = fadeOf(wear, i);
                    const float wl = wear_.sample(0, t), wr = wear_.sample(1, t);
                    l = l * (1.0f - f) + wl * f;
                    r = r * (1.0f - f) + wr * f;
                }
            }
            // Crush
            if (crushActive)
            {
                const float f = fadeOf(crush, i);
                const float cl = crush_.sample(0, l), cr = crush_.sample(1, r);
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

    // The stages, in signal order.
    Heat heat_;
    Chamber chamber_;
    Drift drift_;
    Grain grain_;
    Wear wear_;
    Crush crush_;

    // Output
    float tone_ = 0, width_ = 1, output_ = 1, lowCut_ = 20, highCut_ = 20000;
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
