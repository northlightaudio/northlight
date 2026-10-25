#include "plugins/Bloom.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BeatSync.h"
#include "plugins/FeedbackNetwork.h"
#include "plugins/ParamHelpers.h"
#include "plugins/ProcessorBase.h"
#include "plugins/dsp/Random.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace northlight::plugins::bloom
{

namespace
{
constexpr int lineCount = fdn::lineCount;
static_assert(lineCount == static_cast<int>(lineMs.size()));
constexpr float pi = std::numbers::pi_v<float>;
constexpr float speedOfSound = 343.0f;
// The size whose level the others are matched to (the default).
constexpr float referenceSizePercent = 60.0f;
// Full shimmer turns each pass this far (radians) from the unshifted sound to the shifted.
constexpr float maxShimmerAngle = 0.7f;

// Bloom's network: long lines that stretch from a large room to an absurd space,
// with room for the shimmer's grain.
const fdn::Tuning& tuning() noexcept
{
    static const fdn::Tuning t = []
    {
        fdn::Tuning x;
        x.lineMs = lineMs;
        x.allpassMs = loopAllpassMs;
        x.diffuserLeftMs = {1.7f, 2.9f, 5.3f, 7.9f};
        x.diffuserRightMs = {1.9f, 3.1f, 5.9f, 8.3f};
        x.maxScale = maxScale;
        x.maxModSeconds = maxModSeconds;
        x.shimmerWindowSeconds = shimmerWindowSeconds;
        // Highs above the damping (and lows below the low cut) decay four times
        // faster than the decay.
        x.bandDecayRatio = 4.0f;
        x.earlyGain = 0.6f;
        x.energyCeiling = energyCeiling;
        return x;
    }();
    return t;
}

int choiceIndex(float value, int count) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}
} // namespace

float sizeScale(float percent) noexcept
{
    const float f = std::clamp(percent, 0.0f, 100.0f) * 0.01f;
    return minScale * std::pow(maxScale / minScale, f);
}

float sizeMetres(float percent) noexcept
{
    return fdn::meanLineSeconds(tuning()) * sizeScale(percent) * speedOfSound;
}

float pitchRatio(float choice, int line) noexcept
{
    const int i = choiceIndex(choice, static_cast<int>(pitchSemitones.size()));
    const float semitones =
        i == bothOctaves && line % 2 == 1 ? -12.0f : pitchSemitones[static_cast<size_t>(i)];
    return std::exp2(semitones / 12.0f);
}

double preDelaySeconds(float preDelayMs, float syncChoice, double tempoBpm) noexcept
{
    if (sync::quarters(syncChoice) > 0.0)
        return std::min(maxSyncedPreDelaySeconds, sync::seconds(syncChoice, tempoBpm));
    return std::clamp(static_cast<double>(preDelayMs), 0.0, static_cast<double>(maxPreDelayMs)) * 0.001;
}

float decayAt(float hz, float decaySeconds, float sizePercent, float dampingHz, float lowCutHz) noexcept
{
    return fdn::decayAt(tuning(), hz, decaySeconds, sizeScale(sizePercent), dampingHz, lowCutHz);
}

namespace
{
std::vector<engine::ParameterInfo> parameterInfos()
{
    std::vector<engine::ParameterInfo> p(count);
    p[decay] = {"Decay", "s", minDecaySeconds, maxDecaySeconds, 6.0f, true};
    p[size] = {"Size", "%", 0.0f, 100.0f, 60.0f, false};
    p[freeze] = choice("Freeze", {"Off", "On"}, 0.0f);
    p[shimmer] = {"Shimmer", "%", 0.0f, 100.0f, 0.0f, false};
    p[pitch] = choice("Pitch", {"-12", "+5", "+7", "+12", "+19", "+12 / -12"}, 3.0f);
    p[swell] = {"Swell", "ms", swellOffMs, maxSwellMs, swellOffMs, true};
    p[preDelay] = {"Pre-delay", "ms", 0.0f, maxPreDelayMs, 20.0f, false};
    p[sync] = choice("Pre-delay sync", sync::choices(), 0.0f);
    p[diffusion] = {"Diffusion", "%", 0.0f, 100.0f, 80.0f, false};
    p[modulation] = {"Modulation", "%", 0.0f, 100.0f, 30.0f, false};
    p[modRate] = {"Rate", "Hz", 0.05f, 5.0f, 0.5f, true};
    p[lowCut] = {"Low cut", "Hz", 20.0f, 1000.0f, 80.0f, true};
    p[damping] = {"Damping", "Hz", 500.0f, 20000.0f, 6000.0f, true};
    p[tilt] = {"Tilt", "dB", -6.0f, 6.0f, 0.0f, false};
    p[duck] = {"Duck", "%", 0.0f, 100.0f, 0.0f, false};
    p[duckRelease] = {"Duck release", "ms", 50.0f, 3000.0f, 400.0f, true};
    p[gate] = {"Gate", "ms", gateOffMs, maxGateMs, gateOffMs, true};
    p[width] = {"Width", "%", 0.0f, 100.0f, 100.0f, false};
    p[mix] = {"Mix", "%", 0.0f, 100.0f, 35.0f, false};
    return p;
}

// A fixed pattern of taps the swell reads the input through: positions across
// the swell time (0..1, irregular so they make no rhythm), gains rising toward
// the end with energy summing to one, and signs that keep the cloud from
// colouring the sound. The two channels have their own patterns.
constexpr int swellTaps = 24;
struct SwellPattern
{
    std::array<float, swellTaps> position{}, gain{};
};

std::array<SwellPattern, 2> makeSwellPatterns() noexcept
{
    std::array<SwellPattern, 2> patterns{};
    dsp::Lcg rng{0x9e3779b9u};
    for (auto& p : patterns)
    {
        float energy = 0.0f;
        for (int k = 0; k < swellTaps; ++k)
        {
            const float at =
                (static_cast<float>(k) + 0.15f + 0.7f * rng.unit()) / static_cast<float>(swellTaps);
            const float g = std::pow(at, 1.6f) * (rng.unit() < 0.5f ? -1.0f : 1.0f);
            p.position[static_cast<size_t>(k)] = at;
            p.gain[static_cast<size_t>(k)] = g;
            energy += g * g;
        }
        const float norm = 1.0f / std::sqrt(energy);
        for (auto& g : p.gain)
            g *= norm;
    }
    return patterns;
}

class Bloom final : public ProcessorBase
{
public:
    Bloom() : ProcessorBase(type, parameterInfos()), feedback_(true), patterns_(makeSwellPatterns()) {}

    engine::ProcessorFeedback* feedback() noexcept override { return &feedback_; }

private:
    // ----- preparation (controller thread) --------------------------------------------------

    void prepareAudio(double sampleRate, int maxBlockSize) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Bloom::prepareAudio");
        feedback_.prepare(sampleRate);
        const auto block = static_cast<size_t>(std::max(1, maxBlockSize));
        wetLeft_.assign(block, 0.0f);
        wetRight_.assign(block, 0.0f);
        bypassLeft_.assign(block, 0.0f);
        bypassRight_.assign(block, 0.0f);

        // The input: the longest pre-delay plus the longest swell, at this rate.
        const double inputSeconds =
            std::max(maxSyncedPreDelaySeconds, static_cast<double>(maxPreDelayMs) * 0.001) +
            static_cast<double>(maxSwellMs) * 0.001 + 0.01;
        const int inputSize = fdn::powerOfTwoAtLeast(inputSeconds * sampleRate + 8.0);
        for (auto& b : input_)
            b.assign(static_cast<size_t>(inputSize), 0.0f);
        inputMask_ = inputSize - 1;
        inputWrite_ = 0;

        network_.prepare(tuning(), sampleRate);
        dcLow_ = {0.0f, 0.0f};
        tiltLow_ = {0.0f, 0.0f};
        duckEnvelope_ = keyEnvelope_ = keyPeak_ = 0.0f;
        duckCountdown_ = 0;
        duckDb_ = 0.0f;
        duckGain_ = 1.0f;
        duckGainStep_ = 0.0f;
        gateHold_ = 0;
        gateGain_ = 1.0f;
        tailSquare_ = drySquare_ = 0.0f;
        silentSamples_ = 0;
        idle_ = false;
        primed_ = false; // the first block takes the parameters as they are (they arrive after prepare)

        const auto rate = static_cast<float>(sampleRate);
        const auto smoothing = [rate](float seconds) { return dsp::stepForSeconds(seconds, rate); };
        sizeStep_ = smoothing(0.25f);   // a size change glides, like tape
        freezeStep_ = smoothing(0.06f); // freeze fades in and out over about 150 ms
        glideStep_ = smoothing(0.05f);  // the pre-delay
        swellStep_ = smoothing(0.2f);   // the swell's spread
        paramStep_ = smoothing(0.02f);  // the levels
        duckAttack_ = smoothing(0.005f);
        keyRelease_ = smoothing(0.02f);
        keyPeakFall_ = std::pow(10.0f, -12.0f / 20.0f / rate); // the key's reference falls 12 dB a second
        gateOpen_ = smoothing(0.001f);
        gateClose_ = smoothing(0.004f);
        rmsStep_ = smoothing(0.05f);
        dcStep_ = fdn::onePole(15.0f, sampleRate);
        tiltStep_ = fdn::onePole(tiltHz, sampleRate);
    }

    // ----- per block --------------------------------------------------------------------------

    struct Targets
    {
        float scale = 1.0f, freeze = 0.0f, shimmer = 0.0f, swell = 0.0f, swellMix = 0.0f, pre = 0.0f;
        float diffusion = 0.0f, modSamples = 0.0f, inputGain = 1.0f;
        float wet = 0.0f, width = 1.0f, tiltLow = 1.0f, tiltHigh = 1.0f;
        float duckDepthDb = 0.0f, duckRelease = 0.0f;
        int gateSamples = 0;
        bool gateOn = false;
    };

    Targets targets() const noexcept
    {
        const auto rate = static_cast<float>(sampleRate_);
        Targets t;
        t.scale = sizeScale(parameter(size));
        t.freeze = parameter(freeze) >= 0.5f ? 1.0f : 0.0f;
        t.shimmer = std::clamp(parameter(shimmer) * 0.01f, 0.0f, 1.0f);
        const float swellMs = parameter(swell);
        t.swell = std::max(0.0f, swellMs) * 0.001f * rate;
        // Off at swellOffMs; the cloud fades in over the next 20 ms of the range.
        t.swellMix = std::clamp((swellMs - swellOffMs) / 20.0f, 0.0f, 1.0f);
        t.pre =
            static_cast<float>(preDelaySeconds(parameter(preDelay), parameter(sync), playHead().tempoBpm)) *
            rate;
        t.diffusion = std::clamp(parameter(diffusion) * 0.01f, 0.0f, 1.0f);
        const float depth = std::clamp(parameter(modulation) * 0.01f, 0.0f, 1.0f);
        const float hz = std::max(0.01f, parameter(modRate));
        // From a slow chorus (a fraction of a millisecond) to warping, the
        // deviation held to about a semitone at fast rates.
        float sweep = depth > 0.0f ? 0.00025f + (maxModSeconds - 0.00025f) * depth * depth : 0.0f;
        sweep = std::min(sweep, maxPitchDeviation / (2.0f * pi * hz));
        t.modSamples = sweep * rate;
        // Long decays build more energy from a held input: half of it (in dB)
        // is taken back at the input, the governor holds the rest. Small sizes
        // build more too (more passes a second): most of that is taken back.
        const float rt = std::clamp(parameter(decay), minDecaySeconds, maxDecaySeconds);
        t.inputGain = std::min(1.0f, std::pow(2.0f / rt, 0.25f)) *
                      std::pow(t.scale / sizeScale(referenceSizePercent), 0.4f);
        t.wet = std::clamp(parameter(mix) * 0.01f, 0.0f, 1.0f);
        t.width = std::clamp(parameter(width) * 0.01f, 0.0f, 1.0f);
        const float tiltDb = std::clamp(parameter(tilt), -6.0f, 6.0f);
        t.tiltLow = dbToGain(-0.5f * tiltDb);
        t.tiltHigh = dbToGain(0.5f * tiltDb);
        t.duckDepthDb = std::clamp(parameter(duck) * 0.01f, 0.0f, 1.0f) * duckMaxDb;
        t.duckRelease = dsp::stepForSeconds(std::max(0.01f, parameter(duckRelease) * 0.001f), rate);
        const float gateMs = parameter(gate);
        t.gateOn = gateMs > gateOffMs + 0.5f;
        t.gateSamples = static_cast<int>(gateMs * 0.001f * rate);
        return t;
    }

    // The smoothed controls jump to their targets (the first block, and silence).
    void snapTo(const Targets& t) noexcept
    {
        scale_ = t.scale;
        freeze_ = t.freeze;
        shimmer_ = t.shimmer * (1.0f - t.freeze);
        swell_ = t.swell;
        swellMix_ = t.swellMix;
        pre_ = t.pre;
        modSamples_ = t.modSamples;
        inputGain_ = t.inputGain;
        wet_ = t.wet;
        width_ = t.width;
        tiltLowGain_ = t.tiltLow;
        tiltHighGain_ = t.tiltHigh;
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const bool watched = feedback_.watched();
        feedback_.setTempoBpm(playHead().tempoBpm);
        const auto t = targets();
        if (!primed_)
        {
            snapTo(t);
            primed_ = true;
        }
        if (watched)
            feedback_.tap(0, left, right, numSamples); // the dry input, for the card's contrast

        // Silence in, a dead network and nothing held: nothing to compute.
        float inPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            inPeak = std::max(inPeak, std::max(std::abs(left[i]), std::abs(right[i])));
        silentSamples_ = inPeak < 1.0e-7f ? silentSamples_ + numSamples : 0;
        idle_ = silentSamples_ > inputMask_ + 1 && network_.energy() < 1.0e-14f;
        if (idle_)
        {
            snapTo(t);
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] *= 1.0f - t.wet;
                right[i] *= 1.0f - t.wet;
                wetLeft_[static_cast<size_t>(i)] = wetRight_[static_cast<size_t>(i)] = 0.0f;
                if (watched)
                    feedback_.addSample(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            }
            tailSquare_ = drySquare_ = 0.0f;
            if (watched)
                tapTail(numSamples);
            return;
        }

        const auto rate = static_cast<float>(sampleRate_);
        fdn::BlockSettings block;
        block.decaySeconds = std::clamp(parameter(decay), minDecaySeconds, maxDecaySeconds);
        // The decay at the size this block ends near.
        block.endScale = scale_ + (t.scale - scale_) * std::min(1.0f, sizeStep_ * numSamples);
        block.modSamples = modSamples_;
        block.dampingHz = std::clamp(parameter(damping), 500.0f, 20000.0f);
        block.lowCutHz = std::clamp(parameter(lowCut), 20.0f, 1000.0f);
        block.diffusion = t.diffusion;
        block.lfoHz = std::max(0.01f, parameter(modRate));
        for (int i = 0; i < lineCount; ++i)
            block.pitchRatios[static_cast<size_t>(i)] = pitchRatio(parameter(pitch), i);
        network_.beginBlock(block);
        const float shimmerTarget = t.shimmer * (1.0f - t.freeze);
        const bool shimmering = shimmerTarget > 1.0e-4f || shimmer_ > 1.0e-4f;
        const float* inL = input_[0].data();
        const float* inR = input_[1].data();

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n], dryR = right[n];
            // Smoothed controls.
            scale_ += (t.scale - scale_) * sizeStep_;
            freeze_ += (t.freeze - freeze_) * freezeStep_;
            shimmer_ += (shimmerTarget - shimmer_) * freezeStep_;
            pre_ += (t.pre - pre_) * glideStep_;
            swell_ += (t.swell - swell_) * swellStep_;
            swellMix_ += (t.swellMix - swellMix_) * paramStep_;
            modSamples_ += (t.modSamples - modSamples_) * paramStep_;
            inputGain_ += (t.inputGain - inputGain_) * paramStep_;
            wet_ += (t.wet - wet_) * paramStep_;
            width_ += (t.width - width_) * paramStep_;
            tiltLowGain_ += (t.tiltLow - tiltLowGain_) * paramStep_;
            tiltHighGain_ += (t.tiltHigh - tiltHighGain_) * paramStep_;
            const float thaw = 1.0f - freeze_;
            // The shimmer's blend keeps the power (the two paths are unrelated
            // sound). Each pass moves sin² of it up an interval, so even at the
            // most most of it stays: the tail climbs over seconds and is gone
            // from the top only as fast as it climbs.
            const float keep = shimmering ? std::cos(shimmer_ * maxShimmerAngle) : 1.0f;
            const float shift = shimmering ? std::sin(shimmer_ * maxShimmerAngle) : 0.0f;

            // The input, its DC taken out, into the pre-delay line.
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = ch == 0 ? dryL : dryR;
                dcLow_[static_cast<size_t>(ch)] += dcStep_ * (x - dcLow_[static_cast<size_t>(ch)]);
                input_[static_cast<size_t>(ch)][static_cast<size_t>(inputWrite_)] =
                    x - dcLow_[static_cast<size_t>(ch)];
            }
            const float pre = std::max(1.0f, pre_);
            float sendL = dsp::readLinear(inL, inputMask_, inputWrite_, pre);
            float sendR = dsp::readLinear(inR, inputMask_, inputWrite_, pre);
            // The swell: the input again through the rising cloud of taps.
            if (swellMix_ > 1.0e-4f)
            {
                float cloudL = 0.0f, cloudR = 0.0f;
                for (int k = 0; k < swellTaps; ++k)
                {
                    const auto kk = static_cast<size_t>(k);
                    cloudL += patterns_[0].gain[kk] *
                              dsp::readLinear(
                                  inL, inputMask_, inputWrite_, pre + patterns_[0].position[kk] * swell_);
                    cloudR += patterns_[1].gain[kk] *
                              dsp::readLinear(
                                  inR, inputMask_, inputWrite_, pre + patterns_[1].position[kk] * swell_);
                }
                sendL += (cloudL - sendL) * swellMix_;
                sendR += (cloudR - sendR) * swellMix_;
            }
            inputWrite_ = (inputWrite_ + 1) & inputMask_;
            // Diffusion.
            network_.diffuse(sendL, sendR);
            const float inject = inputGain_ * thaw;
            sendL *= inject;
            sendR *= inject;

            // The network.
            fdn::Frame frame;
            frame.scale = scale_;
            frame.sweep = std::max(modSamples_, frozenModSeconds * rate * freeze_);
            frame.freeze = freeze_;
            frame.shimmer = shimmer_;
            frame.keep = keep;
            frame.shift = shift;
            frame.shimmering = shimmering;
            float tailL = 0.0f, tailR = 0.0f;
            network_.process(frame, sendL, sendR, tailL, tailR);

            // The tail: tilt, width.
            tiltLow_[0] += tiltStep_ * (tailL - tiltLow_[0]);
            tiltLow_[1] += tiltStep_ * (tailR - tiltLow_[1]);
            tailL = tiltLow_[0] * tiltLowGain_ + (tailL - tiltLow_[0]) * tiltHighGain_;
            tailR = tiltLow_[1] * tiltLowGain_ + (tailR - tiltLow_[1]) * tiltHighGain_;
            const float mid = 0.5f * (tailL + tailR), side = 0.5f * (tailL - tailR) * width_;
            tailL = mid + side;
            tailR = mid - side;

            // Ducking under the dry input.
            const float level = std::max(std::abs(dryL), std::abs(dryR));
            duckEnvelope_ += (level - duckEnvelope_) * (level > duckEnvelope_ ? duckAttack_ : t.duckRelease);
            // The duck's depth from the envelope in dB, every duckControl samples (a
            // log and an exp), the gain gliding to it in between.
            if (--duckCountdown_ <= 0)
            {
                duckCountdown_ = duckControl;
                duckDb_ = 0.0f;
                if (t.duckDepthDb > 0.0f)
                {
                    const float envelopeDb = gainToDb(duckEnvelope_);
                    duckDb_ = t.duckDepthDb * std::clamp((envelopeDb + 54.0f) / 36.0f, 0.0f, 1.0f);
                }
                const float target = duckDb_ > 0.0f ? std::exp(-duckDb_ * 0.11512925f) : 1.0f;
                duckGainStep_ = (target - duckGain_) / static_cast<float>(duckControl);
            }
            duckGain_ += duckGainStep_;
            const float duckDb = duckDb_;
            const float duckGain = duckGain_;

            // The gate: open while the input is near its recent peak, held for
            // the gate time after it falls away, then closed fast (never while frozen).
            keyEnvelope_ = level > keyEnvelope_ ? level : keyEnvelope_ + (level - keyEnvelope_) * keyRelease_;
            keyPeak_ = std::max(keyEnvelope_, keyPeak_ * keyPeakFall_);
            if (keyEnvelope_ > std::max(0.001f, keyPeak_ * 0.063f))
                gateHold_ = t.gateSamples;
            else if (gateHold_ > 0)
                --gateHold_;
            const float gateTarget = !t.gateOn || freeze_ > 0.5f || gateHold_ > 0 ? 1.0f : 0.0f;
            gateGain_ += (gateTarget - gateGain_) * (gateTarget > gateGain_ ? gateOpen_ : gateClose_);

            const float shape = duckGain * gateGain_;
            tailL *= shape;
            tailR *= shape;
            wetLeft_[static_cast<size_t>(n)] = tailL;
            wetRight_[static_cast<size_t>(n)] = tailR;
            left[n] = dryL * (1.0f - wet_) + tailL * wet_;
            right[n] = dryR * (1.0f - wet_) + tailR * wet_;

            if (watched)
            {
                tailSquare_ += (0.5f * (tailL * tailL + tailR * tailR) - tailSquare_) * rmsStep_;
                drySquare_ += (0.5f * (dryL * dryL + dryR * dryR) - drySquare_) * rmsStep_;
                const float out = std::max(std::abs(left[n]), std::abs(right[n]));
                feedback_.addSample(level, out, duckDb, std::sqrt(tailSquare_), std::sqrt(drySquare_));
            }
        }
        if (watched)
            tapTail(numSamples);
        // A long silence leaves the followers creeping toward zero: no denormals.
        if (duckEnvelope_ < 1.0e-9f)
            duckEnvelope_ = 0.0f;
        if (keyEnvelope_ < 1.0e-9f)
            keyEnvelope_ = 0.0f;
        if (keyPeak_ < 1.0e-9f)
            keyPeak_ = 0.0f;
        if (tailSquare_ < 1.0e-20f)
            tailSquare_ = 0.0f;
        if (drySquare_ < 1.0e-20f)
            drySquare_ = 0.0f;
        // Never expected: a non-finite value would ring for ever, so the network starts again.
        if (network_.diverged())
            clearState();
    }

    // Tap 1, which the card's tail display reads: the tail on its own (before
    // the mix), so it shows at any mix.
    void tapTail(int numSamples) noexcept { feedback_.tap(1, wetLeft_.data(), wetRight_.data(), numSamples); }

    void clearState() noexcept
    {
        for (auto& b : input_)
            std::fill(b.begin(), b.end(), 0.0f);
        network_.clear();
        tiltLow_ = {0.0f, 0.0f};
        dcLow_ = {0.0f, 0.0f};
    }

    // Bypassed, the network keeps running on a copy of the input so the tail
    // stays current (and a frozen tail stays held) for when it comes back.
    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        std::copy(left, left + numSamples, bypassLeft_.data());
        std::copy(right, right + numSamples, bypassRight_.data());
        processAudio(bypassLeft_.data(), bypassRight_.data(), numSamples);
    }

    engine::ProcessorFeedback feedback_;
    std::array<SwellPattern, 2> patterns_;
    std::array<std::vector<float>, 2> input_;
    int inputMask_ = 0, inputWrite_ = 0;
    fdn::FeedbackNetwork network_;
    std::vector<float> wetLeft_, wetRight_, bypassLeft_, bypassRight_;
    std::array<float, 2> dcLow_{}, tiltLow_{};
    // Smoothed controls.
    float scale_ = 1.0f, freeze_ = 0.0f, shimmer_ = 0.0f, swell_ = 0.0f, swellMix_ = 0.0f, pre_ = 0.0f;
    float modSamples_ = 0.0f, inputGain_ = 1.0f, wet_ = 0.0f, width_ = 1.0f;
    float tiltLowGain_ = 1.0f, tiltHighGain_ = 1.0f;
    // Followers.
    float duckEnvelope_ = 0.0f, keyEnvelope_ = 0.0f, keyPeak_ = 0.0f, gateGain_ = 1.0f;
    static constexpr int duckControl = 16; // samples between the duck's depth updates
    int duckCountdown_ = 0;
    float duckDb_ = 0.0f, duckGain_ = 1.0f, duckGainStep_ = 0.0f;
    int gateHold_ = 0;
    float tailSquare_ = 0.0f, drySquare_ = 0.0f;
    int silentSamples_ = 0;
    bool idle_ = false, primed_ = false;
    // Coefficients for the rate.
    float sizeStep_ = 0.0f, freezeStep_ = 0.0f, glideStep_ = 0.0f, swellStep_ = 0.0f, paramStep_ = 0.0f;
    float duckAttack_ = 0.0f;
    float keyRelease_ = 0.0f, keyPeakFall_ = 1.0f, gateOpen_ = 0.0f, gateClose_ = 0.0f, rmsStep_ = 0.0f;
    float dcStep_ = 0.0f, tiltStep_ = 0.0f;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Bloom>();
}

} // namespace northlight::plugins::bloom
