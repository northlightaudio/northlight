#include "plugins/Vesper.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BeatSync.h"
#include "plugins/Biquad.h"
#include "plugins/FeedbackNetwork.h"
#include "plugins/ProcessorBase.h"
#include "plugins/VocalListener.h"
#include "plugins/dsp/Denormals.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
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

double lookaheadSeconds(float choice) noexcept
{
    const int i = std::clamp(static_cast<int>(std::lround(choice)), 0, 2);
    return i == 0 ? 0.0 : i == 1 ? shortLookaheadSeconds : longLookaheadSeconds;
}

int lookaheadSamples(float choice, double sampleRate) noexcept
{
    return static_cast<int>(std::lround(lookaheadSeconds(choice) * sampleRate));
}

namespace
{
constexpr int spaceCount = 3;
// Listener frames waiting for the delayed audio to reach them (Long holds 200).
constexpr int frameQueueSize = 512;
// Line ends waiting to be thrown: a few at most within a lookahead.
constexpr int maxPendingEnds = 8;
// Catch fades in and out over about 150 ms, as Bloom's Freeze; held, the lines
// keep at least this much movement so the pad never turns static.
constexpr float catchFadeSeconds = 0.06f;
constexpr float heldModSeconds = 0.0006f;
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
            {"Output", "dB", -12.0f, 12.0f, 0.0f, false},
            choice("Lookahead", {"Off", "Short", "Long"}, 0.0f),
            {"Swell", "%", 0.0f, 100.0f, 0.0f, false},
            choice("Catch", {"Off", "On"}, 0.0f),
            {"Halo", "%", 0.0f, 100.0f, 0.0f, false}};
}

// Halo: sympathetic strings. Six feedback combs, each tuned to a note the singer
// held, ring with what goes into the reverb, so
// the tail sounds the pitches just sung and their harmonics, in tune with the
// singer however the singer is tuned. A note is held when its pitch runs on for
// 120 ms without a jump (100 cents in a frame) or a glide of 150 cents away from its
// centre (its average pitch), so a vibrato counts; the string follows the centre
// while the note is sung. A note no string holds goes to a free or
// the quietest one, faded out over 30 ms first so a retune never clicks. A one-pole
// low-pass in each loop makes them ring like strings, not buzz; its delay is taken
// off the loop so the pitch stays true. Mono in and out. Real-time safe after
// prepare().
class HaloBank
{
public:
    static constexpr int voiceCount = 6;
    static constexpr float jumpCents = 100.0f, glideCents = 150.0f, sameNoteCents = 50.0f;
    static constexpr double steadySeconds = 0.12, centreSeconds = 0.3, retuneSeconds = 0.03;
    static constexpr float dampingHz = 4000.0f;

    void prepare(double sampleRate)
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("HaloBank::prepare");
        sampleRate_ = sampleRate;
        const int size = fdn::powerOfTwoAtLeast(sampleRate / vocal::lowestHz + 8.0);
        for (auto& v : voices_)
            v.buffer.assign(static_cast<size_t>(size), 0.0f);
        mask_ = size - 1;
        lowStep_ = fdn::onePole(dampingHz, sampleRate);
        loopLag_ = (1.0f - lowStep_) / lowStep_; // the low-pass's delay at low frequencies, samples
        fadeStep_ = static_cast<float>(1.0 / (retuneSeconds * sampleRate));
        steadyFrames_ = static_cast<int>(std::lround(steadySeconds / vocal::frameSeconds));
        centreStep_ = static_cast<float>(1.0 - std::exp(-vocal::frameSeconds / centreSeconds));
        envelopeStep_ = 1.0f - std::exp(-1.0f / static_cast<float>(0.05 * sampleRate));
        clear();
    }

    void clear() noexcept
    {
        for (auto& v : voices_)
        {
            std::fill(v.buffer.begin(), v.buffer.end(), 0.0f);
            v.hz = v.pendingHz = 0.0f;
            v.delay = 1.0f;
            v.feedback = v.drive = v.gain = v.low = v.energy = 0.0f;
            v.lastHeard = 0;
        }
        write_ = 0;
        candidateHz_ = centre_ = last_ = 0.0f;
        steady_ = 0;
        current_ = -1;
        clock_ = 0;
    }

    // Once a block: how long a string rings (RT60).
    void setDecay(float seconds) noexcept { decaySeconds_ = std::max(0.1f, seconds); }

    // A Listener frame: a note held that no string holds is given one, and the
    // string holding the note being sung follows its centre.
    void hear(const vocal::Hearing& h) noexcept
    {
        ++clock_;
        if (h.sound != vocal::Sound::voiced || !(h.pitchHz > 0.0f))
        {
            steady_ = 0;
            current_ = -1;
            return;
        }
        // The centre is the note's average pitch in octaves so far, then over its
        // last 300 ms or so; a jump or a glide away from it starts a new note.
        const float octaves = std::log2(h.pitchHz);
        if (candidateHz_ > 0.0f && std::abs(1200.0f * (octaves - last_)) < jumpCents &&
            std::abs(1200.0f * (octaves - centre_)) < glideCents)
        {
            steady_ = std::min(steady_ + 1, 1 << 20);
            centre_ += (octaves - centre_) * std::max(centreStep_, 1.0f / static_cast<float>(steady_ + 1));
        }
        else
        {
            centre_ = octaves;
            steady_ = 0;
            current_ = -1;
        }
        last_ = octaves;
        candidateHz_ = std::exp2(centre_);
        if (steady_ < steadyFrames_)
            return;
        if (current_ >= 0)
        {
            auto& v = voices_[static_cast<size_t>(current_)];
            (v.pendingHz > 0.0f ? v.pendingHz : v.hz) = candidateHz_;
            v.lastHeard = clock_;
            return;
        }
        for (size_t i = 0; i < voices_.size(); ++i)
        {
            auto& v = voices_[i];
            const float held = v.pendingHz > 0.0f ? v.pendingHz : v.hz;
            if (held > 0.0f && std::abs(cents(candidateHz_, held)) < sameNoteCents)
            {
                current_ = static_cast<int>(i);
                v.lastHeard = clock_;
                return;
            }
        }
        // A free string, else the quietest (the longest unheard of equals).
        size_t pick = 0;
        for (size_t i = 0; i < voices_.size(); ++i)
        {
            const auto& v = voices_[i];
            const auto& best = voices_[pick];
            if (v.hz <= 0.0f && v.pendingHz <= 0.0f)
            {
                pick = i;
                break;
            }
            if (v.energy < best.energy || (v.energy == best.energy && v.lastHeard < best.lastHeard))
                pick = i;
        }
        voices_[pick].pendingHz = candidateHz_;
        voices_[pick].lastHeard = clock_;
        current_ = static_cast<int>(pick);
    }

    // One sample in; the strings' sum out.
    float process(float x) noexcept
    {
        float out = 0.0f;
        for (auto& v : voices_)
        {
            // A string to retune fades out, then starts again silent at its new note.
            if (v.pendingHz > 0.0f)
            {
                v.gain = std::max(0.0f, v.gain - fadeStep_);
                if (v.gain <= 0.0f)
                    retune(v);
            }
            else if (v.hz > 0.0f && v.gain < 1.0f)
                v.gain = std::min(1.0f, v.gain + fadeStep_);
            if (v.hz <= 0.0f)
                continue;
            const float read = dsp::readLinear(v.buffer.data(), mask_, write_, v.delay);
            v.low += lowStep_ * (read - v.low);
            const float y = x * v.drive * v.gain + v.feedback * v.low;
            v.buffer[static_cast<size_t>(write_)] = y;
            const float sound = v.low * v.gain;
            v.energy += (sound * sound - v.energy) * envelopeStep_;
            out += sound;
        }
        write_ = (write_ + 1) & mask_;
        return out;
    }

    // Whether any string still sounds (or is about to).
    bool ringing() const noexcept
    {
        for (const auto& v : voices_)
            if (v.pendingHz > 0.0f || (v.hz > 0.0f && v.energy > 1.0e-12f))
                return true;
        return false;
    }

    // The notes held, for the tests.
    std::array<float, voiceCount> notes() const noexcept
    {
        std::array<float, voiceCount> n{};
        for (size_t i = 0; i < n.size(); ++i)
            n[i] = voices_[i].hz;
        return n;
    }

    // Once a block, after setDecay: each string's loop gain for its note.
    void tune() noexcept
    {
        for (auto& v : voices_)
            if (v.hz > 0.0f)
                setLoop(v);
    }

    void flushDenormals() noexcept
    {
        for (auto& v : voices_)
        {
            v.low = dsp::flush(v.low, 1.0e-20f);
            if (v.energy < 1.0e-20f)
                v.energy = 0.0f;
        }
    }

private:
    struct Voice
    {
        std::vector<float> buffer;
        float hz = 0.0f, pendingHz = 0.0f;
        float delay = 1.0f, feedback = 0.0f, drive = 0.0f, gain = 0.0f, low = 0.0f, energy = 0.0f;
        std::int64_t lastHeard = 0;
    };

    static float cents(float a, float b) noexcept { return 1200.0f * std::log2(a / b); }

    void retune(Voice& v) noexcept
    {
        std::fill(v.buffer.begin(), v.buffer.end(), 0.0f);
        v.hz = v.pendingHz;
        v.pendingHz = 0.0f;
        v.low = 0.0f;
        v.energy = 0.0f;
        setLoop(v);
    }

    void setLoop(Voice& v) noexcept
    {
        const auto period = static_cast<float>(sampleRate_ / v.hz);
        v.delay = std::clamp(period - loopLag_, 2.0f, static_cast<float>(mask_ - 2));
        v.feedback = std::pow(10.0f, -3.0f * period / (static_cast<float>(sampleRate_) * decaySeconds_));
        // The comb's gain at its note is about 1 / (1 - feedback): driven by that much
        // less, a held matching note rings about as loud as it went in.
        v.drive = 1.0f - v.feedback;
    }

    std::array<Voice, voiceCount> voices_;
    double sampleRate_ = 48000.0;
    int mask_ = 0, write_ = 0;
    float lowStep_ = 0.0f, loopLag_ = 0.0f, fadeStep_ = 0.0f, envelopeStep_ = 0.0f;
    float decaySeconds_ = 3.0f;
    float candidateHz_ = 0.0f, centre_ = 0.0f, last_ = 0.0f, centreStep_ = 0.0f;
    int steady_ = 0, steadyFrames_ = 48, current_ = -1;
    std::int64_t clock_ = 0;
};

class Vesper final : public ProcessorBase
{
public:
    Vesper() : ProcessorBase(type, parameterInfos()), feedback_(true) {}

    engine::ProcessorFeedback* feedback() noexcept override { return &feedback_; }

    // The engine reads this to line everything else up; a change of Lookahead
    // changes it, and the engine compiles the graph again (as the limiter's does).
    int latencySamples() const noexcept override
    {
        return lookaheadSamples(parameter(lookahead), sampleRate_);
    }

    const Telemetry& telemetry() const noexcept { return telemetry_; }

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
        delayedLeft_.assign(block, 0.0f);
        delayedRight_.assign(block, 0.0f);

        // The dry signal's delay for Lookahead, and the frames waiting for it.
        const int drySize = fdn::powerOfTwoAtLeast(longLookaheadSeconds * sampleRate + 8.0);
        for (auto& b : dry_)
            b.assign(static_cast<size_t>(drySize), 0.0f);
        dryMask_ = drySize - 1;

        listener_.prepare(sampleRate);
        halo_.prepare(sampleRate);
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
        const auto smoothing = [rate](float seconds) { return dsp::stepForSeconds(seconds, rate); };
        const auto perFrame = [](float seconds) { return 1.0f - std::exp(-listenerFrameSeconds / seconds); };
        sendDown_ = smoothing(0.002f);
        sendUp_ = smoothing(0.03f);
        sizeStep_ = smoothing(0.25f);
        glideStep_ = smoothing(0.05f);
        paramStep_ = smoothing(0.02f);
        spaceStep_ = smoothing(0.01f);
        duckAttack_ = smoothing(0.01f);
        rmsStep_ = smoothing(0.05f);
        fastRmsStep_ = smoothing(0.01f);
        mudMeasureStep_ = smoothing(0.05f);
        endsUp_ = perFrame(endsAttackSeconds);
        endsDown_ = perFrame(endsSettleSeconds);
        followStep_ = perFrame(followGlideSeconds);
        mudStep_ = perFrame(0.05f);
        catchStep_ = smoothing(catchFadeSeconds);
        leadSamples_ = static_cast<int>(std::lround(listenerLeadSeconds * sampleRate));
        throwSamples_ = static_cast<std::int64_t>(std::lround(throwSeconds * sampleRate));
        telemetryInterval_ = std::max(1, static_cast<int>(sampleRate * Telemetry::frameSeconds));
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
        int lookahead = 0;
        float swell = 0.0f, held = 0.0f, halo = 0.0f;
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
        t.duckRelease = dsp::stepForSeconds(static_cast<float>(releaseSeconds), rate);
        t.ends = std::clamp(parameter(ends) * 0.01f, 0.0f, 1.0f);
        t.decay = std::clamp(parameter(decay), minDecaySeconds, maxDecaySeconds);
        t.follow = parameter(follow);
        t.mud = std::clamp(parameter(mud) * 0.01f, 0.0f, 1.0f);
        t.lookahead = std::min(lookaheadSamples(parameter(lookahead), sampleRate_), dryMask_);
        // Swell needs to know a line is coming long before it does: Long only.
        t.swell = t.lookahead >= lookaheadSamples(2.0f, sampleRate_)
                      ? std::clamp(parameter(swell) * 0.01f, 0.0f, 1.0f)
                      : 0.0f;
        t.held = parameter(catchTail) >= 0.5f ? 1.0f : 0.0f;
        t.halo = std::clamp(parameter(halo) * 0.01f, 0.0f, 1.0f);
        return t;
    }

    // The send's cut for what was heard: consonants and sibilance by Clarity,
    // breaths by Breaths.
    static float sendCutFor(const vocal::Hearing& h, const Targets& t) noexcept
    {
        switch (h.sound)
        {
            case vocal::Sound::consonant:
                return -sendCutDb * t.clarity;
            case vocal::Sound::voiced:
                return -sendCutDb * t.clarity * h.sibilance;
            case vocal::Sound::breath:
                return -sendCutDb * t.breaths;
            case vocal::Sound::silence:
                break;
        }
        return 0.0f;
    }

    // A frame just heard (ahead of the audio by the lookahead). With no lookahead
    // it applies at once; otherwise it waits for the delayed audio, a little early.
    // A line end is known before its last word reaches the reverb: that word is
    // thrown (its send lifted by Ends) and Ends starts as it finishes. A line's
    // start sets Swell going.
    void heard(const Targets& t) noexcept
    {
        const auto& h = listener_.hearing();
        liveCutDb_ = sendCutFor(h, t);
        if (h.lineEnd)
            lineClosed_ = true;
        else if (h.sound == vocal::Sound::voiced && lineClosed_)
        {
            lineClosed_ = false;
            if (t.swell > 0.0f)
            {
                swellStart_ = clock_;
                swellEnd_ = clock_ + std::max(1, t.lookahead - leadSamples_);
            }
        }
        if (t.lookahead == 0)
        {
            onFrame(h, t);
            return;
        }
        auto queued = h;
        queued.lineEnd = false; // ends are scheduled on their own
        if (queuedFrames_ < frameQueueSize)
        {
            auto& slot = frames_[static_cast<size_t>((frameRead_ + queuedFrames_) % frameQueueSize)];
            slot.at = clock_ + std::max(0, t.lookahead - std::min(t.lookahead, leadSamples_));
            slot.hearing = queued;
            ++queuedFrames_;
        }
        if (h.lineEnd)
        {
            // The last word ended `pauseSeconds` ago in the input, so it reaches the
            // reverb lookahead-less-the-pause from now (now, if that is past).
            const auto lastWord =
                clock_ - static_cast<std::int64_t>(h.pauseSeconds * sampleRate_) + t.lookahead;
            for (auto& e : pendingEnds_)
                if (!e.active)
                {
                    e.active = true;
                    e.throwAt = std::max(clock_, lastWord - throwSamples_);
                    e.endAt = std::max(clock_, lastWord);
                    break;
                }
        }
    }

    // Frames and line ends whose moment the delayed audio has reached.
    void applyDue(const Targets& t) noexcept
    {
        while (queuedFrames_ > 0 && frames_[static_cast<size_t>(frameRead_)].at <= clock_)
        {
            onFrame(frames_[static_cast<size_t>(frameRead_)].hearing, t);
            frameRead_ = (frameRead_ + 1) % frameQueueSize;
            --queuedFrames_;
        }
        for (auto& e : pendingEnds_)
        {
            if (!e.active)
                continue;
            if (!e.throwing && clock_ >= e.throwAt && e.throwAt < e.endAt)
            {
                e.throwing = true;
                ++throwing_;
            }
            if (clock_ >= e.endAt)
            {
                if (e.throwing)
                    --throwing_;
                e = {};
                endsTarget_ = 1.0f;
                lineEndSeen_ = true;
            }
        }
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

    // A Listener frame as the delayed audio reaches it: what the send, the duck,
    // Ends and Follow aim for next.
    void onFrame(const vocal::Hearing& h, const Targets& t) noexcept
    {
        sendTargetDb_ = sendCutFor(h, t);
        if (t.halo > 0.0f)
            halo_.hear(h);
        heardSound_ = h.sound;
        heardPitch_ = h.heldPitchHz;
        if (h.lineEnd)
            lineEndSeen_ = true;
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
        if (t.lookahead != lookahead_)
        {
            // A new lookahead: what was waiting for the old one is let go (the
            // engine lines the graph up again around the new latency).
            lookahead_ = t.lookahead;
            queuedFrames_ = 0;
            for (auto& e : pendingEnds_)
                e = {};
            throwing_ = 0;
            swellEnd_ = swellStart_;
        }
        if (watched)
            feedback_.tap(0, left, right, numSamples);

        // Silence in, the networks dead: nothing to compute but the dry delay.
        float inPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            inPeak = std::max(inPeak, std::max(std::abs(left[i]), std::abs(right[i])));
        silentSamples_ = inPeak < 1.0e-7f ? silentSamples_ + numSamples : 0;
        bool dead = silentSamples_ > inputMask_ + 1 && t.held == 0.0f && held_ < 1.0e-4f &&
                    !(haloGain_ > 0.0f && halo_.ringing());
        for (const auto& n : networks_)
            dead = dead && n.energy() < 1.0e-14f;
        if (dead)
        {
            snapTo(t);
            for (int i = 0; i < numSamples; ++i)
            {
                const auto ii = static_cast<size_t>(i);
                const auto [dryL, dryR] = delay(left[i], right[i]);
                delayedLeft_[ii] = dryL;
                delayedRight_[ii] = dryR;
                left[i] = dryL * (1.0f - t.wet) * t.out;
                right[i] = dryR * (1.0f - t.wet) * t.out;
                wetLeft_[ii] = wetRight_[ii] = 0.0f;
                listener_.push(0.0f);
                if (watched)
                {
                    feedback_.addSample(0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
                    observe(0.0f, 0.0f, 0.0f);
                }
            }
            tailSquare_ = drySquare_ = dryFast_ = 0.0f;
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
        const float throwDb = endsLiftDb * t.ends;
        // Halo's strings ring longer than the tail.
        // Turned off and faded out, they stop and are cleared.
        if (t.halo == 0.0f && haloGain_ > 0.0f && haloGain_ < 1.0e-4f)
        {
            halo_.clear();
            haloGain_ = 0.0f;
        }
        const bool haloOn = t.halo > 0.0f || haloGain_ > 0.0f;
        if (haloOn)
        {
            halo_.setDecay(std::max(haloMinSeconds, t.decay * haloDecayRatio));
            halo_.tune();
        }

        for (int n = 0; n < numSamples; ++n)
        {
            const auto nn = static_cast<size_t>(n);
            // The Listener hears the input as it arrives; everything else works on
            // it delayed by the lookahead.
            const float liveL = left[n], liveR = right[n];
            const auto [dryL, dryR] = delay(liveL, liveR);
            delayedLeft_[nn] = dryL;
            delayedRight_[nn] = dryR;
            if (listener_.push(0.5f * (liveL + liveR)))
                heard(t);
            applyDue(t);

            scale_ += (t.scale - scale_) * sizeStep_;
            pre_ += (t.pre - pre_) * glideStep_;
            wet_ += (t.wet - wet_) * paramStep_;
            out_ += (t.out - out_) * paramStep_;
            width_ += (t.width - width_) * paramStep_;
            midGain_ += (t.midGain - midGain_) * paramStep_;
            held_ += (t.held - held_) * catchStep_;
            const float target = sendTargetDb_ + (throwing_ > 0 ? throwDb : 0.0f);
            sendDb_ += (target - sendDb_) * (target < sendDb_ ? sendDown_ : sendUp_);
            if (std::abs(target - sendDb_) < 1.0e-3f)
                sendDb_ = target; // settled: the gain below is then worked out once
            if (sendDb_ != sendGainDb_)
            {
                sendGainDb_ = sendDb_;
                sendGain_ = dbToGain(sendDb_);
            }
            // Held, the input stays out of the pad.
            const float open = 1.0f - held_;
            const float send = sendGain_ * open;

            // The shaped send into the pre-delay, with Halo's strings, which it plays.
            float strings = 0.0f;
            if (haloOn)
            {
                haloGain_ += (t.halo - haloGain_) * paramStep_;
                strings = halo_.process(0.5f * (dryL + dryR) * send) * haloGain_ * haloLevel * open;
            }
            input_[0][static_cast<size_t>(inputWrite_)] = dryL * send + strings;
            input_[1][static_cast<size_t>(inputWrite_)] = dryR * send + strings;
            const float pre = std::max(1.0f, pre_);
            float sendL = dsp::readLinear(inL, inputMask_, inputWrite_, pre);
            float sendR = dsp::readLinear(inR, inputMask_, inputWrite_, pre);
            // Swell: the line still to come goes in early, rising, so the tail
            // builds into its first word like a reversed reverb.
            if (clock_ >= swellStart_ && clock_ < swellEnd_)
            {
                const float x =
                    static_cast<float>(clock_ - swellStart_) / static_cast<float>(swellEnd_ - swellStart_);
                liveDb_ += (liveCutDb_ - liveDb_) * (liveCutDb_ < liveDb_ ? sendDown_ : sendUp_);
                if (std::abs(liveCutDb_ - liveDb_) < 1.0e-3f)
                    liveDb_ = liveCutDb_;
                if (liveDb_ != liveGainDb_)
                {
                    liveGainDb_ = liveDb_;
                    liveGain_ = dbToGain(liveDb_);
                }
                const float g = t.swell * swellGain * x * x * liveGain_ * open;
                sendL += liveL * g;
                sendR += liveR * g;
            }

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
                // Catch holds the current space: no loss, and a little movement.
                frame.freeze = k == t.space ? held_ : 0.0f;
                frame.sweep = std::max(sp.modSeconds, heldModSeconds * frame.freeze) * rate;
                float outL = 0.0f, outR = 0.0f;
                networks_[kk].process(frame, l * inject, r * inject, outL, outR);
                // Early reflections, alternating sides, the far side's quieter and inverted.
                for (int e = 0; e < sp.taps; ++e)
                {
                    const auto ee = static_cast<size_t>(e);
                    const float at = pre + sp.tapMs[ee] * 0.001f * rate;
                    const float g = sp.tapGain[ee] * inject;
                    const float tl = dsp::readLinear(inL, inputMask_, inputWrite_, at);
                    const float tr = dsp::readLinear(inR, inputMask_, inputWrite_, at);
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
            if (std::abs(duckTarget - duck_) < 1.0e-5f)
                duck_ = duckTarget;
            const float duckDb = t.duckDb * duck_;
            if (const float shapeDb = endsLiftDb * t.ends * ends_ - duckDb; shapeDb != shapeDb_)
            {
                shapeDb_ = shapeDb;
                shapeGain_ = dbToGain(shapeDb);
            }
            const float shape = shapeGain_;
            tailL *= shape;
            tailR *= shape;

            wetLeft_[nn] = tailL;
            wetRight_[nn] = tailR;
            left[n] = (dryL * (1.0f - wet_) + tailL * wet_) * out_;
            right[n] = (dryR * (1.0f - wet_) + tailR * wet_) * out_;

            if (watched)
            {
                tailSquare_ += (0.5f * (tailL * tailL + tailR * tailR) - tailSquare_) * rmsStep_;
                drySquare_ += (0.5f * (dryL * dryL + dryR * dryR) - drySquare_) * rmsStep_;
                // The card's land follows the voice closely, so a line's end shows where it is.
                dryFast_ += (0.5f * (dryL * dryL + dryR * dryR) - dryFast_) * fastRmsStep_;
                const float level = std::max(std::abs(dryL), std::abs(dryR));
                const float out = std::max(std::abs(left[n]), std::abs(right[n]));
                feedback_.addSample(level, out, duckDb, std::sqrt(tailSquare_), std::sqrt(drySquare_));
                observe(std::sqrt(dryFast_), std::sqrt(tailSquare_), duckDb);
            }
        }
        if (watched)
            feedback_.tap(1, wetLeft_.data(), wetRight_.data(), numSamples);

        // No denormals after a long silence.
        for (auto& f : follow_)
            f.flushDenormals();
        for (auto& f : mudCut_)
            f.flushDenormals();
        mudBand_.flushDenormals();
        if (mudBandPower_ < 1.0e-20f)
            mudBandPower_ = 0.0f;
        if (tailPower_ < 1.0e-20f)
            tailPower_ = 0.0f;
        if (tailSquare_ < 1.0e-20f)
            tailSquare_ = 0.0f;
        if (drySquare_ < 1.0e-20f)
            drySquare_ = 0.0f;
        if (dryFast_ < 1.0e-20f)
            dryFast_ = 0.0f;
        if (duck_ < 1.0e-9f)
            duck_ = 0.0f;
        if (held_ < 1.0e-9f)
            held_ = 0.0f;
        halo_.flushDenormals();
        // Never expected: a non-finite value would ring for ever, so everything starts again.
        for (const auto& net : networks_)
            if (net.diverged())
            {
                clearState();
                break;
            }
    }

    // One sample's share of the card's telemetry frame (only while an editor
    // watches); a frame is published every Telemetry::frameSeconds.
    void observe(float dryRms, float tailRms, float duckDb) noexcept
    {
        auto& f = telemetryFrame_;
        f[Telemetry::sound] = static_cast<float>(heardSound_);
        if (heardSound_ == vocal::Sound::voiced)
            f[Telemetry::voiced] = 1.0f;
        if (heardSound_ == vocal::Sound::breath)
            f[Telemetry::breath] = 1.0f;
        f[Telemetry::keptOutDb] = std::max(f[Telemetry::keptOutDb], -std::min(0.0f, sendDb_));
        if (lineEndSeen_)
        {
            f[Telemetry::lineEnd] = 1.0f;
            lineEndSeen_ = false;
        }
        f[Telemetry::heldPitchHz] = heardPitch_;
        f[Telemetry::followHz] = followHz_;
        f[Telemetry::dryRms] = std::max(f[Telemetry::dryRms], dryRms);
        f[Telemetry::tailRms] = std::max(f[Telemetry::tailRms], tailRms);
        f[Telemetry::duckDb] = std::max(f[Telemetry::duckDb], duckDb);
        f[Telemetry::held] = held_;
        if (++telemetryCount_ >= telemetryInterval_)
        {
            telemetry_.publish(f);
            f = {};
            telemetryCount_ = 0;
        }
    }

    // One sample into the dry delay; the sample `lookahead_` behind it out. The
    // clock counts the samples in.
    std::pair<float, float> delay(float l, float r) noexcept
    {
        dry_[0][static_cast<size_t>(dryWrite_)] = l;
        dry_[1][static_cast<size_t>(dryWrite_)] = r;
        const auto at = static_cast<size_t>((dryWrite_ - lookahead_) & dryMask_);
        dryWrite_ = (dryWrite_ + 1) & dryMask_;
        ++clock_;
        return {dry_[0][at], dry_[1][at]};
    }

    void clearState() noexcept
    {
        for (auto& b : input_)
            std::fill(b.begin(), b.end(), 0.0f);
        for (auto& b : dry_)
            std::fill(b.begin(), b.end(), 0.0f);
        dryWrite_ = 0;
        for (auto& net : networks_)
            net.clear();
        listener_.reset();
        halo_.clear();
        haloGain_ = 0.0f;
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
        liveDb_ = liveCutDb_ = 0.0f;
        duck_ = duckHang_ = 0.0f;
        ends_ = endsTarget_ = 0.0f;
        followHz_ = 0.0f;
        mudDb_ = mudBandPower_ = tailPower_ = 0.0f;
        tailSquare_ = drySquare_ = 0.0f;
        held_ = 0.0f;
        silentSamples_ = 0;
        queuedFrames_ = frameRead_ = 0;
        for (auto& e : pendingEnds_)
            e = {};
        throwing_ = 0;
        lineClosed_ = true;
        swellStart_ = swellEnd_ = clock_;
    }

    // Bypassed, the reverb keeps running on a copy of the input so the tail stays
    // current for when it comes back, and the input comes out delayed by the
    // lookahead, as the graph was lined up for.
    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        std::copy(left, left + numSamples, bypassLeft_.data());
        std::copy(right, right + numSamples, bypassRight_.data());
        processAudio(bypassLeft_.data(), bypassRight_.data(), numSamples);
        std::copy(delayedLeft_.begin(), delayedLeft_.begin() + numSamples, left);
        std::copy(delayedRight_.begin(), delayedRight_.begin() + numSamples, right);
    }

    // A Listener frame waiting for the delayed audio to reach the sound it heard.
    struct QueuedFrame
    {
        std::int64_t at = 0;
        vocal::Hearing hearing;
    };
    // A line end waiting for its last word: thrown from `throwAt`, Ends from `endAt`.
    struct PendingEnd
    {
        bool active = false, throwing = false;
        std::int64_t throwAt = 0, endAt = 0;
    };

    engine::ProcessorFeedback feedback_;
    vocal::Listener listener_;
    HaloBank halo_;
    float haloGain_ = 0.0f;
    std::array<fdn::FeedbackNetwork, spaceCount> networks_;
    std::array<std::vector<float>, 2> input_, dry_;
    int inputMask_ = 0, inputWrite_ = 0, dryMask_ = 0, dryWrite_ = 0;
    std::vector<float> wetLeft_, wetRight_, bypassLeft_, bypassRight_, delayedLeft_, delayedRight_;
    std::array<Biquad, 2> follow_, mudCut_;
    Biquad mudBand_;
    // Lookahead.
    int lookahead_ = 0, leadSamples_ = 0;
    std::int64_t clock_ = 0, throwSamples_ = 0;
    std::array<QueuedFrame, frameQueueSize> frames_{};
    int frameRead_ = 0, queuedFrames_ = 0;
    std::array<PendingEnd, maxPendingEnds> pendingEnds_{};
    int throwing_ = 0;
    bool lineClosed_ = true;
    std::int64_t swellStart_ = 0, swellEnd_ = 0;
    // Smoothed controls.
    float scale_ = 1.0f, pre_ = 0.0f, wet_ = 0.0f, out_ = 1.0f, width_ = 1.0f, midGain_ = 1.0f;
    float held_ = 0.0f;
    std::array<float, spaceCount> spaceGain_{};
    // What the Listener drives.
    float sendDb_ = 0.0f, sendTargetDb_ = 0.0f, liveDb_ = 0.0f, liveCutDb_ = 0.0f;
    // The gains of the dB values above, kept while those hold (a pow per sample
    // otherwise); the dB they were worked out at, NaN-free sentinels to start.
    float sendGainDb_ = -1.0e9f, sendGain_ = 0.0f;
    float liveGainDb_ = -1.0e9f, liveGain_ = 0.0f;
    float shapeDb_ = -1.0e9f, shapeGain_ = 0.0f;
    float duck_ = 0.0f, duckHang_ = 0.0f;
    float ends_ = 0.0f, endsTarget_ = 0.0f;
    float followHz_ = 0.0f;
    float mudDb_ = 0.0f, mudBandPower_ = 0.0f, tailPower_ = 0.0f;
    float tailSquare_ = 0.0f, drySquare_ = 0.0f, dryFast_ = 0.0f;
    int silentSamples_ = 0;
    bool primed_ = false;
    // Coefficients for the rate.
    float sendDown_ = 0.0f, sendUp_ = 0.0f, sizeStep_ = 0.0f, glideStep_ = 0.0f, paramStep_ = 0.0f;
    float spaceStep_ = 0.0f, duckAttack_ = 0.0f, rmsStep_ = 0.0f, fastRmsStep_ = 0.0f, mudMeasureStep_ = 0.0f;
    float endsUp_ = 0.0f, endsDown_ = 0.0f, followStep_ = 0.0f, mudStep_ = 0.0f, catchStep_ = 0.0f;
    // The card's telemetry: what was last heard (as the delayed audio reaches it)
    // and the frame being gathered.
    Telemetry telemetry_;
    Telemetry::Frame telemetryFrame_{};
    int telemetryInterval_ = 240, telemetryCount_ = 0;
    vocal::Sound heardSound_ = vocal::Sound::silence;
    float heardPitch_ = 0.0f;
    bool lineEndSeen_ = false;
};
} // namespace

const Telemetry* telemetryOf(const engine::InsertProcessor* processor) noexcept
{
    const auto* vesper = dynamic_cast<const Vesper*>(processor);
    return vesper != nullptr ? &vesper->telemetry() : nullptr;
}

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Vesper>();
}

} // namespace northlight::plugins::vesper
