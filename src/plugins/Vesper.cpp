#include "plugins/Vesper.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BeatSync.h"
#include "plugins/Biquad.h"
#include "plugins/FeedbackNetwork.h"
#include "plugins/ProcessorBase.h"
#include "plugins/VocalListener.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace northlight::plugins::vesper
{

float autoPreDelayMs(float syllablesPerSecond) noexcept
{
    if (!(syllablesPerSecond > 0.0f))
        return 60.0f;
    return std::clamp(105.0f - 16.0f * syllablesPerSecond, 25.0f, 90.0f);
}

double preDelaySeconds(
    float preDelayMs, float autoChoice, float syncChoice, float syllablesPerSecond, double tempoBpm) noexcept
{
    if (autoChoice >= 0.5f)
        return autoPreDelayMs(syllablesPerSecond) * 0.001;
    if (sync::quarters(syncChoice) > 0.0)
        return std::min(maxSyncedPreDelaySeconds, sync::seconds(syncChoice, tempoBpm));
    return std::clamp(static_cast<double>(preDelayMs), 0.0, static_cast<double>(maxPreDelayMs)) * 0.001;
}

float followCutoffHz(float heldPitchHz, float octaves) noexcept
{
    if (octaves <= followOffOctaves || !(heldPitchHz > 0.0f))
        return 0.0f;
    return heldPitchHz / std::exp2(std::clamp(octaves, minFollowOctaves, maxFollowOctaves));
}

namespace
{
constexpr int spaceCount = 3;
constexpr int maxEarlyTaps = 8;
constexpr float listenerFrameSeconds = static_cast<float>(vocal::frameSeconds);
// After the last sung frame the singer still counts as singing this long, so the
// duck holds across the dips between syllables.
constexpr float duckHangSeconds = 0.04f;
constexpr float endsAttackSeconds = 0.08f, endsSettleSeconds = 0.4f;
constexpr float followGlideSeconds = 0.08f;
constexpr float mudHz = 350.0f;

// One space: its network's tuning, how hard the send drives it, how much it
// diffuses, how it moves, and its early reflections (none for the plate).
struct Space
{
    fdn::Tuning tuning;
    float inject = 1.0f;
    float diffusion = 0.8f;
    float modSeconds = 0.0003f, modHz = 0.6f;
    int taps = 0;
    std::array<float, maxEarlyTaps> tapMs{}, tapGain{};
};

std::array<Space, spaceCount> makeSpaces()
{
    std::array<Space, spaceCount> s{};
    const auto base = [](Space& x)
    {
        x.tuning.maxScale = maxSizePercent / 100.0f;
        x.tuning.maxModSeconds = 0.001f;
        x.tuning.shimmerWindowSeconds = 0.0f;
        x.tuning.energyCeiling = 0.5f;
    };
    // Plate: dense at once and bright, no early reflections, the highs damped late.
    auto& plate = s[0];
    base(plate);
    plate.tuning.lineMs = {13.1f, 15.7f, 17.9f, 20.3f, 23.1f, 25.7f, 28.9f, 31.9f};
    plate.tuning.allpassMs = {1.3f, 1.9f, 2.3f, 2.9f, 3.1f, 3.7f, 4.1f, 4.7f};
    plate.tuning.diffuserLeftMs = {0.9f, 1.4f, 2.1f, 3.3f};
    plate.tuning.diffuserRightMs = {1.0f, 1.6f, 2.3f, 3.6f};
    plate.tuning.bandDecayRatio = 3.0f;
    plate.tuning.earlyGain = 0.7f;
    plate.inject = 1.0f;
    plate.diffusion = 0.9f;
    plate.modSeconds = 0.0003f;
    plate.modHz = 0.7f;
    // Chamber: a small live room, a little early slap, warm.
    auto& chamber = s[1];
    base(chamber);
    chamber.tuning.lineMs = {17.3f, 19.9f, 23.3f, 26.1f, 29.7f, 33.1f, 37.3f, 41.9f};
    chamber.tuning.allpassMs = {1.9f, 2.7f, 3.3f, 4.1f, 4.9f, 5.7f, 6.3f, 7.1f};
    chamber.tuning.diffuserLeftMs = {1.3f, 2.3f, 3.7f, 5.3f};
    chamber.tuning.diffuserRightMs = {1.5f, 2.5f, 4.1f, 5.9f};
    chamber.tuning.bandDecayRatio = 4.0f;
    chamber.tuning.earlyGain = 0.45f;
    chamber.inject = 0.9f;
    chamber.diffusion = 0.7f;
    chamber.modSeconds = 0.0002f;
    chamber.modHz = 0.5f;
    chamber.taps = 6;
    chamber.tapMs = {8.3f, 11.9f, 15.7f, 19.1f, 23.9f, 29.3f};
    chamber.tapGain = {0.45f, 0.4f, 0.35f, 0.3f, 0.25f, 0.2f};
    // Hall: big and slow to build, for ballads.
    auto& hall = s[2];
    base(hall);
    hall.tuning.lineMs = {28.5f, 33.6f, 37.7f, 42.4f, 48.0f, 53.9f, 60.4f, 66.5f};
    hall.tuning.allpassMs = {2.9f, 3.9f, 5.1f, 6.3f, 7.5f, 8.7f, 10.1f, 11.5f};
    hall.tuning.diffuserLeftMs = {1.7f, 2.9f, 5.3f, 7.9f};
    hall.tuning.diffuserRightMs = {1.9f, 3.1f, 5.9f, 8.3f};
    hall.tuning.bandDecayRatio = 4.0f;
    hall.tuning.earlyGain = 0.35f;
    hall.inject = 0.8f;
    hall.diffusion = 0.75f;
    hall.modSeconds = 0.0005f;
    hall.modHz = 0.35f;
    hall.taps = 8;
    hall.tapMs = {12.1f, 17.3f, 23.9f, 29.7f, 36.1f, 43.3f, 51.7f, 59.9f};
    hall.tapGain = {0.35f, 0.32f, 0.3f, 0.27f, 0.24f, 0.2f, 0.17f, 0.14f};
    return s;
}

const std::array<Space, spaceCount>& spaces()
{
    static const auto s = makeSpaces();
    return s;
}

int choiceIndex(float value, int count) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

engine::ParameterInfo choice(std::string name, std::vector<std::string> choices, float defaultValue)
{
    engine::ParameterInfo info;
    info.name = std::move(name);
    info.maximum = static_cast<float>(choices.size() - 1);
    info.defaultValue = defaultValue;
    info.choices = std::move(choices);
    return info;
}

std::vector<engine::ParameterInfo> parameterInfos()
{
    return {choice("Space", {"Plate", "Chamber", "Hall"}, 0.0f),
            {"Decay", "s", minDecaySeconds, maxDecaySeconds, 1.8f, true},
            {"Size", "%", minSizePercent, maxSizePercent, 100.0f, false},
            {"Pre-delay", "ms", 0.0f, maxPreDelayMs, 45.0f, false},
            choice("Auto pre-delay", {"Off", "On"}, 1.0f),
            choice("Pre-delay sync", sync::choices(), 0.0f),
            {"Clarity", "%", 0.0f, 100.0f, 60.0f, false},
            {"Breaths", "%", 0.0f, 100.0f, 100.0f, false},
            {"Follow", "oct", minFollowOctaves, maxFollowOctaves, 1.0f, false},
            {"Mud", "%", 0.0f, 100.0f, 30.0f, false},
            {"Low cut", "Hz", 20.0f, 1000.0f, 100.0f, true},
            {"Damping", "Hz", 2000.0f, 20000.0f, 9000.0f, true},
            {"Width", "%", 0.0f, 150.0f, 100.0f, false},
            {"Centre", "%", 0.0f, 100.0f, 40.0f, false},
            {"Duck", "dB", 0.0f, maxDuckDb, 6.0f, false},
            {"Release", "ms", 50.0f, 2000.0f, 250.0f, true},
            choice("Release sync", sync::choices(), 8.0f), // 1/8
            {"Ends", "%", 0.0f, 100.0f, 40.0f, false},
            {"Gap", "ms", vocal::minGapSeconds * 1000.0f, vocal::maxGapSeconds * 1000.0f, 250.0f, false},
            {"Mix", "%", 0.0f, 100.0f, 25.0f, false},
            {"Output", "dB", -12.0f, 12.0f, 0.0f, false}};
}

void flush(Biquad& f) noexcept
{
    if (std::abs(f.z1) < 1.0e-20f)
        f.z1 = 0.0f;
    if (std::abs(f.z2) < 1.0e-20f)
        f.z2 = 0.0f;
}

class Vesper final : public ProcessorBase
{
public:
    Vesper() : ProcessorBase(type, parameterInfos()), feedback_(true) {}

    engine::ProcessorFeedback* feedback() noexcept override { return &feedback_; }

private:
    // ----- preparation (controller thread) --------------------------------------------------

    void prepareAudio(double sampleRate, int maxBlockSize) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Vesper::prepareAudio");
        feedback_.prepare(sampleRate);
        const auto block = static_cast<size_t>(std::max(1, maxBlockSize));
        wetLeft_.assign(block, 0.0f);
        wetRight_.assign(block, 0.0f);
        bypassLeft_.assign(block, 0.0f);
        bypassRight_.assign(block, 0.0f);

        listener_.prepare(sampleRate);
        for (int k = 0; k < spaceCount; ++k)
            networks_[static_cast<size_t>(k)].prepare(spaces()[static_cast<size_t>(k)].tuning, sampleRate);

        // The send: the longest pre-delay plus the latest early reflection.
        const double inputSeconds =
            std::max(maxSyncedPreDelaySeconds, static_cast<double>(maxPreDelayMs) * 0.001) + 0.07;
        const int inputSize = fdn::powerOfTwoAtLeast(inputSeconds * sampleRate + 8.0);
        for (auto& b : input_)
            b.assign(static_cast<size_t>(inputSize), 0.0f);
        inputMask_ = inputSize - 1;
        inputWrite_ = 0;

        mudBand_.setBandPass(sampleRate, mudHz, 0.9);
        for (auto& f : follow_)
            f.bypass();
        for (auto& f : mudCut_)
            f.bypass();

        const auto rate = static_cast<float>(sampleRate);
        const auto smoothing = [rate](float seconds) { return 1.0f - std::exp(-1.0f / (seconds * rate)); };
        const auto perFrame = [](float seconds) { return 1.0f - std::exp(-listenerFrameSeconds / seconds); };
        sendDown_ = smoothing(0.002f);
        sendUp_ = smoothing(0.03f);
        sizeStep_ = smoothing(0.25f);
        glideStep_ = smoothing(0.05f);
        paramStep_ = smoothing(0.02f);
        spaceStep_ = smoothing(0.01f);
        duckAttack_ = smoothing(0.01f);
        rmsStep_ = smoothing(0.05f);
        mudMeasureStep_ = smoothing(0.05f);
        endsUp_ = perFrame(endsAttackSeconds);
        endsDown_ = perFrame(endsSettleSeconds);
        followStep_ = perFrame(followGlideSeconds);
        mudStep_ = perFrame(0.05f);
        clearState();
        primed_ = false; // the first block takes the parameters as they are (they arrive after prepare)
    }

    // ----- per block --------------------------------------------------------------------------

    struct Targets
    {
        int space = 0;
        float scale = 1.0f, pre = 0.0f, wet = 0.0f, out = 1.0f, width = 1.0f, midGain = 1.0f;
        float clarity = 0.0f, breaths = 0.0f, duckDb = 0.0f, duckRelease = 0.0f, ends = 0.0f;
        float decay = 1.8f, follow = 1.0f, mud = 0.0f;
    };

    Targets targets() const noexcept
    {
        const auto rate = static_cast<float>(sampleRate_);
        Targets t;
        t.space = choiceIndex(parameter(space), spaceCount);
        t.scale = std::clamp(parameter(size), minSizePercent, maxSizePercent) * 0.01f;
        t.pre = static_cast<float>(preDelaySeconds(parameter(preDelay),
                                                   parameter(preDelayAuto),
                                                   parameter(preDelaySync),
                                                   listener_.hearing().syllablesPerSecond,
                                                   playHead().tempoBpm)) *
                rate;
        t.wet = std::clamp(parameter(mix) * 0.01f, 0.0f, 1.0f);
        t.out = dbToGain(std::clamp(parameter(output), -12.0f, 12.0f));
        t.width = std::clamp(parameter(width) * 0.01f, 0.0f, 1.5f);
        t.midGain = 1.0f - 0.8f * std::clamp(parameter(centre) * 0.01f, 0.0f, 1.0f);
        t.clarity = std::clamp(parameter(clarity) * 0.01f, 0.0f, 1.0f);
        t.breaths = std::clamp(parameter(breaths) * 0.01f, 0.0f, 1.0f);
        t.duckDb = std::clamp(parameter(duck), 0.0f, maxDuckDb);
        const double releaseSeconds = sync::quarters(parameter(releaseSync)) > 0.0
                                          ? sync::seconds(parameter(releaseSync), playHead().tempoBpm)
                                          : std::max(0.01, parameter(release) * 0.001);
        t.duckRelease = 1.0f - std::exp(-1.0f / (static_cast<float>(releaseSeconds) * rate));
        t.ends = std::clamp(parameter(ends) * 0.01f, 0.0f, 1.0f);
        t.decay = std::clamp(parameter(decay), minDecaySeconds, maxDecaySeconds);
        t.follow = parameter(follow);
        t.mud = std::clamp(parameter(mud) * 0.01f, 0.0f, 1.0f);
        return t;
    }

    void snapTo(const Targets& t) noexcept
    {
        scale_ = t.scale;
        pre_ = t.pre;
        wet_ = t.wet;
        out_ = t.out;
        width_ = t.width;
        midGain_ = t.midGain;
        for (int k = 0; k < spaceCount; ++k)
            spaceGain_[static_cast<size_t>(k)] = k == t.space ? 1.0f : 0.0f;
    }

    // A Listener frame: what the send, the duck, Ends and Follow aim for next.
    void onFrame(const Targets& t) noexcept
    {
        const auto& h = listener_.hearing();
        switch (h.sound)
        {
            case vocal::Sound::consonant:
                sendTargetDb_ = -sendCutDb * t.clarity;
                break;
            case vocal::Sound::voiced:
                sendTargetDb_ = -sendCutDb * t.clarity * h.sibilance;
                break;
            case vocal::Sound::breath:
                sendTargetDb_ = -sendCutDb * t.breaths;
                break;
            case vocal::Sound::silence:
                sendTargetDb_ = 0.0f;
                break;
        }
        const bool singing = h.sound == vocal::Sound::voiced || h.sound == vocal::Sound::consonant;
        if (singing)
            duckHang_ = duckHangSeconds;
        else
            duckHang_ = std::max(0.0f, duckHang_ - listenerFrameSeconds);
        // Ends: up at a line end, back down once the next line is sung.
        if (h.lineEnd)
            endsTarget_ = 1.0f;
        else if (h.sound == vocal::Sound::voiced)
            endsTarget_ = 0.0f;
        ends_ += (endsTarget_ - ends_) * (endsTarget_ > ends_ ? endsUp_ : endsDown_);
        // Follow: the tail's high-pass glides after the sung note.
        const float cutoff = followCutoffHz(h.heldPitchHz, t.follow);
        if (cutoff > 0.0f)
        {
            followHz_ = followHz_ > 0.0f ? followHz_ + (cutoff - followHz_) * followStep_ : cutoff;
            for (auto& f : follow_)
                f.setHighPass(sampleRate_, std::clamp(static_cast<double>(followHz_), 20.0, 2000.0));
        }
        else if (followHz_ > 0.0f)
        {
            followHz_ = 0.0f;
            for (auto& f : follow_)
                f.bypass();
        }
        // Mud: the tail's 250-500 Hz share past a quarter of its power is cut,
        // as deep as Mud allows at a half.
        const float shareDb = 10.0f * std::log10((mudBandPower_ + 1.0e-20f) / (tailPower_ + 1.0e-20f));
        const float targetDb = -mudCutDb * t.mud * std::clamp((shareDb + 6.0f) / 3.0f, 0.0f, 1.0f);
        const float previous = mudDb_;
        mudDb_ += (targetDb - mudDb_) * mudStep_;
        if (std::abs(mudDb_ - previous) > 0.01f || (mudDb_ == 0.0f) != (previous == 0.0f))
        {
            if (mudDb_ > -0.05f)
                for (auto& f : mudCut_)
                    f.bypass();
            else
                for (auto& f : mudCut_)
                    f.setPeaking(sampleRate_, mudHz, 0.9, mudDb_);
        }
    }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        const bool watched = feedback_.watched();
        feedback_.setTempoBpm(playHead().tempoBpm);
        listener_.setGapSeconds(parameter(gap) * 0.001f);
        const auto t = targets();
        if (!primed_)
        {
            snapTo(t);
            primed_ = true;
        }
        if (watched)
            feedback_.tap(0, left, right, numSamples);

        // Silence in, the networks dead: nothing to compute.
        float inPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            inPeak = std::max(inPeak, std::max(std::abs(left[i]), std::abs(right[i])));
        silentSamples_ = inPeak < 1.0e-7f ? silentSamples_ + numSamples : 0;
        bool dead = silentSamples_ > inputMask_ + 1;
        for (const auto& n : networks_)
            dead = dead && n.energy() < 1.0e-14f;
        if (dead)
        {
            snapTo(t);
            for (int i = 0; i < numSamples; ++i)
            {
                left[i] *= (1.0f - t.wet) * t.out;
                right[i] *= (1.0f - t.wet) * t.out;
                wetLeft_[static_cast<size_t>(i)] = wetRight_[static_cast<size_t>(i)] = 0.0f;
                listener_.push(0.0f);
                if (watched)
                    feedback_.addSample(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            }
            tailSquare_ = drySquare_ = 0.0f;
            if (watched)
                feedback_.tap(1, wetLeft_.data(), wetRight_.data(), numSamples);
            return;
        }

        const auto rate = static_cast<float>(sampleRate_);
        // Each space's network for this block; a space faded out and rung out is skipped.
        const float endScale = scale_ + (t.scale - scale_) * std::min(1.0f, sizeStep_ * numSamples);
        bool running[spaceCount];
        for (int k = 0; k < spaceCount; ++k)
        {
            const auto kk = static_cast<size_t>(k);
            running[k] = k == t.space || spaceGain_[kk] > 1.0e-4f || networks_[kk].energy() > 1.0e-12f;
            if (!running[k])
                continue;
            const auto& sp = spaces()[kk];
            fdn::BlockSettings block;
            block.decaySeconds = t.decay * (1.0f + (endsDecayRatio - 1.0f) * t.ends * ends_);
            block.endScale = endScale;
            block.modSamples = sp.modSeconds * rate;
            block.dampingHz = std::clamp(parameter(damping), 2000.0f, 20000.0f);
            block.lowCutHz = std::clamp(parameter(lowCut), 20.0f, 1000.0f);
            block.diffusion = sp.diffusion;
            block.lfoHz = sp.modHz;
            networks_[kk].beginBlock(block);
        }
        const float* inL = input_[0].data();
        const float* inR = input_[1].data();
        // Inputs drive the reverb harder at short decays; long ones hold back a little.
        const float decayTrim = std::min(1.0f, std::pow(2.0f / t.decay, 0.25f));

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = left[n], dryR = right[n];
            if (listener_.push(0.5f * (dryL + dryR)))
                onFrame(t);

            scale_ += (t.scale - scale_) * sizeStep_;
            pre_ += (t.pre - pre_) * glideStep_;
            wet_ += (t.wet - wet_) * paramStep_;
            out_ += (t.out - out_) * paramStep_;
            width_ += (t.width - width_) * paramStep_;
            midGain_ += (t.midGain - midGain_) * paramStep_;
            sendDb_ += (sendTargetDb_ - sendDb_) * (sendTargetDb_ < sendDb_ ? sendDown_ : sendUp_);
            const float send = dbToGain(sendDb_);

            // The shaped send into the pre-delay.
            input_[0][static_cast<size_t>(inputWrite_)] = dryL * send;
            input_[1][static_cast<size_t>(inputWrite_)] = dryR * send;
            const float pre = std::max(1.0f, pre_);
            const float sendL = fdn::readLinear(inL, inputMask_, inputWrite_, pre);
            const float sendR = fdn::readLinear(inR, inputMask_, inputWrite_, pre);

            float tailL = 0.0f, tailR = 0.0f;
            fdn::Frame frame;
            frame.scale = scale_;
            for (int k = 0; k < spaceCount; ++k)
            {
                if (!running[k])
                    continue;
                const auto kk = static_cast<size_t>(k);
                const auto& sp = spaces()[kk];
                spaceGain_[kk] += ((k == t.space ? 1.0f : 0.0f) - spaceGain_[kk]) * spaceStep_;
                const float inject = sp.inject * decayTrim * spaceGain_[kk];
                float l = sendL, r = sendR;
                networks_[kk].diffuse(l, r);
                frame.sweep = sp.modSeconds * rate;
                float outL = 0.0f, outR = 0.0f;
                networks_[kk].process(frame, l * inject, r * inject, outL, outR);
                // Early reflections, alternating sides, the far side's quieter and inverted.
                for (int e = 0; e < sp.taps; ++e)
                {
                    const auto ee = static_cast<size_t>(e);
                    const float at = pre + sp.tapMs[ee] * 0.001f * rate;
                    const float g = sp.tapGain[ee] * inject;
                    const float tl = fdn::readLinear(inL, inputMask_, inputWrite_, at);
                    const float tr = fdn::readLinear(inR, inputMask_, inputWrite_, at);
                    if (e % 2 == 0)
                    {
                        outL += g * tl;
                        outR -= 0.6f * g * tr;
                    }
                    else
                    {
                        outR += g * tr;
                        outL -= 0.6f * g * tl;
                    }
                }
                tailL += outL;
                tailR += outR;
            }
            inputWrite_ = (inputWrite_ + 1) & inputMask_;

            // The tail: Follow, Mud, then width and centre.
            tailL = mudCut_[0].process(follow_[0].process(tailL));
            tailR = mudCut_[1].process(follow_[1].process(tailR));
            const float tailMono = 0.5f * (tailL + tailR);
            const float band = mudBand_.process(tailMono);
            mudBandPower_ += (band * band - mudBandPower_) * mudMeasureStep_;
            tailPower_ += (tailMono * tailMono - tailPower_) * mudMeasureStep_;
            const float mid = 0.5f * (tailL + tailR) * midGain_, side = 0.5f * (tailL - tailR) * width_;
            tailL = mid + side;
            tailR = mid - side;

            // Duck while the singer sings; Ends lifts the tail at a line end.
            const float duckTarget = duckHang_ > 0.0f ? 1.0f : 0.0f;
            duck_ += (duckTarget - duck_) * (duckTarget > duck_ ? duckAttack_ : t.duckRelease);
            const float duckDb = t.duckDb * duck_;
            const float shape = dbToGain(endsLiftDb * t.ends * ends_ - duckDb);
            tailL *= shape;
            tailR *= shape;

            wetLeft_[static_cast<size_t>(n)] = tailL;
            wetRight_[static_cast<size_t>(n)] = tailR;
            left[n] = (dryL * (1.0f - wet_) + tailL * wet_) * out_;
            right[n] = (dryR * (1.0f - wet_) + tailR * wet_) * out_;

            if (watched)
            {
                tailSquare_ += (0.5f * (tailL * tailL + tailR * tailR) - tailSquare_) * rmsStep_;
                drySquare_ += (0.5f * (dryL * dryL + dryR * dryR) - drySquare_) * rmsStep_;
                const float level = std::max(std::abs(dryL), std::abs(dryR));
                const float out = std::max(std::abs(left[n]), std::abs(right[n]));
                feedback_.addSample(level, out, duckDb, std::sqrt(tailSquare_), std::sqrt(drySquare_));
            }
        }
        if (watched)
            feedback_.tap(1, wetLeft_.data(), wetRight_.data(), numSamples);

        // No denormals after a long silence.
        for (auto& f : follow_)
            flush(f);
        for (auto& f : mudCut_)
            flush(f);
        flush(mudBand_);
        if (mudBandPower_ < 1.0e-20f)
            mudBandPower_ = 0.0f;
        if (tailPower_ < 1.0e-20f)
            tailPower_ = 0.0f;
        if (tailSquare_ < 1.0e-20f)
            tailSquare_ = 0.0f;
        if (drySquare_ < 1.0e-20f)
            drySquare_ = 0.0f;
        if (duck_ < 1.0e-9f)
            duck_ = 0.0f;
        // Never expected: a non-finite value would ring for ever, so everything starts again.
        for (const auto& net : networks_)
            if (net.diverged())
            {
                clearState();
                break;
            }
    }

    void clearState() noexcept
    {
        for (auto& b : input_)
            std::fill(b.begin(), b.end(), 0.0f);
        for (auto& net : networks_)
            net.clear();
        listener_.reset();
        for (auto& f : follow_)
        {
            f.bypass();
            f.reset();
        }
        for (auto& f : mudCut_)
        {
            f.bypass();
            f.reset();
        }
        mudBand_.reset();
        sendDb_ = sendTargetDb_ = 0.0f;
        duck_ = duckHang_ = 0.0f;
        ends_ = endsTarget_ = 0.0f;
        followHz_ = 0.0f;
        mudDb_ = mudBandPower_ = tailPower_ = 0.0f;
        tailSquare_ = drySquare_ = 0.0f;
        silentSamples_ = 0;
    }

    // Bypassed, the reverb keeps running on a copy of the input so the tail
    // stays current for when it comes back.
    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        std::copy(left, left + numSamples, bypassLeft_.data());
        std::copy(right, right + numSamples, bypassRight_.data());
        processAudio(bypassLeft_.data(), bypassRight_.data(), numSamples);
    }

    engine::ProcessorFeedback feedback_;
    vocal::Listener listener_;
    std::array<fdn::FeedbackNetwork, spaceCount> networks_;
    std::array<std::vector<float>, 2> input_;
    int inputMask_ = 0, inputWrite_ = 0;
    std::vector<float> wetLeft_, wetRight_, bypassLeft_, bypassRight_;
    std::array<Biquad, 2> follow_, mudCut_;
    Biquad mudBand_;
    // Smoothed controls.
    float scale_ = 1.0f, pre_ = 0.0f, wet_ = 0.0f, out_ = 1.0f, width_ = 1.0f, midGain_ = 1.0f;
    std::array<float, spaceCount> spaceGain_{};
    // What the Listener drives.
    float sendDb_ = 0.0f, sendTargetDb_ = 0.0f;
    float duck_ = 0.0f, duckHang_ = 0.0f;
    float ends_ = 0.0f, endsTarget_ = 0.0f;
    float followHz_ = 0.0f;
    float mudDb_ = 0.0f, mudBandPower_ = 0.0f, tailPower_ = 0.0f;
    float tailSquare_ = 0.0f, drySquare_ = 0.0f;
    int silentSamples_ = 0;
    bool primed_ = false;
    // Coefficients for the rate.
    float sendDown_ = 0.0f, sendUp_ = 0.0f, sizeStep_ = 0.0f, glideStep_ = 0.0f, paramStep_ = 0.0f;
    float spaceStep_ = 0.0f, duckAttack_ = 0.0f, rmsStep_ = 0.0f, mudMeasureStep_ = 0.0f;
    float endsUp_ = 0.0f, endsDown_ = 0.0f, followStep_ = 0.0f, mudStep_ = 0.0f;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Vesper>();
}

} // namespace northlight::plugins::vesper
