#include "plugins/TapeEcho.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BeatSync.h"
#include "plugins/ProcessorBase.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace northlight::plugins::tapeecho
{

std::uint8_t headMask(float choice) noexcept
{
    const int i =
        std::clamp(static_cast<int>(std::lround(choice)), 0, static_cast<int>(headModes.size()) - 1);
    return headModes[static_cast<size_t>(i)].mask;
}

int activeHeadCount(std::uint8_t mask) noexcept
{
    int n = 0;
    for (int k = 0; k < headCount; ++k)
        n += (mask >> k) & 1;
    return n;
}

double headOneSeconds(float timeMs, float syncChoice, double tempoBpm) noexcept
{
    const double seconds =
        sync::quarters(syncChoice) > 0.0 ? sync::seconds(syncChoice, tempoBpm) : timeMs * 0.001;
    return std::clamp(seconds, minTimeMs * 0.001, maxTimeMs * 0.001);
}

bool syncClamped(float syncChoice, double tempoBpm) noexcept
{
    if (sync::quarters(syncChoice) <= 0.0)
        return false;
    const double seconds = sync::seconds(syncChoice, tempoBpm);
    return seconds > maxTimeMs * 0.001 + 1.0e-9 || seconds < minTimeMs * 0.001 - 1.0e-9;
}

double highLossHz(float agePercent, double headOneSeconds) noexcept
{
    const double age = std::clamp(agePercent * 0.01, 0.0, 1.0);
    const double base = 16000.0 * (1.0 - 0.78 * age);
    // A slower tape carries fewer highs (head 1 at 180 ms is the reference).
    const double speed = std::clamp(std::pow(0.18 / std::max(1.0e-3, headOneSeconds), 0.3), 0.6, 1.25);
    return std::clamp(base * speed, 1500.0, 20000.0);
}

void LoopFilters::design(
    double sampleRate, float bassDb, float trebleDb, float agePercent, double headOneSeconds) noexcept
{
    highPass.setHighPass1(sampleRate, loopHighPassHz);
    bassShelf.setLowShelf(sampleRate, bassHz, bassDb);
    trebleShelf.setHighShelf(sampleRate, trebleHz, trebleDb);
    highLoss.setLowPass(sampleRate, highLossHz(agePercent, headOneSeconds), 0.6);
}

double LoopFilters::magnitude(double hz, double sampleRate) const noexcept
{
    return highPass.magnitude(hz, sampleRate) * bassShelf.magnitude(hz, sampleRate) *
           trebleShelf.magnitude(hz, sampleRate) * highLoss.magnitude(hz, sampleRate);
}

float loopGain(float feedbackPercent,
               float bassDb,
               float trebleDb,
               float agePercent,
               double headOneSeconds,
               double sampleRate) noexcept
{
    LoopFilters filters;
    filters.design(sampleRate, bassDb, trebleDb, agePercent, headOneSeconds);
    double peak = 0.0;
    constexpr int points = 96;
    const double top = std::min(20000.0, sampleRate * 0.45);
    for (int i = 0; i < points; ++i)
    {
        const double hz = 20.0 * std::pow(top / 20.0, static_cast<double>(i) / (points - 1));
        peak = std::max(peak, filters.magnitude(hz, sampleRate));
    }
    return static_cast<float>(std::max(0.0f, feedbackPercent) * 0.01 * peak);
}

namespace
{
// A whole note at 60 bpm on head 4, with room for the motor's overshoot and the wow.
constexpr double bufferSeconds = headSpacing.back() * maxTimeMs * 0.001 * 1.15 + 0.05;

// xorshift32: cheap, allocation-free noise for the wobble, hiss and dropouts.
struct Rng
{
    std::uint32_t state = 0x9E3779B9u;
    float next() noexcept // -1..1
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) * (2.0f / 4294967296.0f) - 1.0f;
    }
    float unit() noexcept { return 0.5f * (next() + 1.0f); } // 0..1
};

// A smooth random curve in -1..1: a new random point every period (jittered
// +-40%), eased between, so the drift has no corners.
struct Drift
{
    float from = 0.0f, to = 0.0f;
    int left = 0, length = 1;
    float next(Rng& rng, float periodSamples) noexcept
    {
        if (left <= 0)
        {
            from = to;
            to = rng.next();
            length = std::max(1, static_cast<int>(periodSamples * (0.6f + 0.8f * rng.unit())));
            left = length;
        }
        const float t = 1.0f - static_cast<float>(left) / static_cast<float>(length);
        --left;
        return from + (to - from) * t * t * (3.0f - 2.0f * t);
    }
};

// The tape's saturation: tanh scaled to the headroom, with first-order
// antiderivative anti-aliasing (the mean of the curve over each step).
struct Saturator
{
    double previousX = 0.0, previousF = 0.0;

    static double curve(double x) noexcept { return tapeHeadroom * std::tanh(x / tapeHeadroom); }
    static double integral(double x) noexcept
    {
        constexpr double h = tapeHeadroom;
        const double a = std::abs(x / h);
        return h * h * (a + std::log1p(std::exp(-2.0 * a)) - std::numbers::ln2);
    }
    void reset() noexcept { previousX = previousF = 0.0; }
    float process(float in) noexcept
    {
        const double x = in;
        const double f = integral(x);
        const double dx = x - previousX;
        const double y = std::abs(dx) > 1.0e-5 ? (f - previousF) / dx : curve(0.5 * (x + previousX));
        previousX = x;
        previousF = f;
        return static_cast<float>(y);
    }
};

// A short spring: the dispersion of a stretched allpass cascade (each stage
// (a + z^-K) / (1 + a z^-K), K putting its whole sweep below the spring's
// transition frequency, so the highs arrive later than the lows: the chirp)
// inside a delay loop that sends each chirp round again, more dispersed, with a
// one-pole darkening it. Band-limited in and out, as a spring tank is.
class Spring
{
public:
    static constexpr int stages = 80;
    static constexpr float coefficient = 0.7f;
    static constexpr float loopGain = 0.72f;

    void prepare(double sampleRate, double loopSeconds)
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("Spring::prepare");
        k_ = std::max(1, static_cast<int>(std::lround(sampleRate / (2.0 * springTransitionHz))));
        memory_.assign(static_cast<size_t>(stages * k_), 0.0f);
        // The cascade itself delays the signal; the line makes up the rest of the loop.
        const double cascadeSamples = stages * k_ * (1.0 - coefficient) / (1.0 + coefficient);
        length_ = std::max(8, static_cast<int>(sampleRate * loopSeconds - cascadeSamples));
        loop_.assign(static_cast<size_t>(length_), 0.0f);
        position_ = write_ = 0;
        low_ = 0.0f;
        lowStep_ =
            1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 3200.0f / static_cast<float>(sampleRate));
        input_.setHighPass(sampleRate, 180.0);
        output_.setLowPass(sampleRate, springTransitionHz, 0.8);
        input_.reset();
        output_.reset();
    }

    float process(float x) noexcept
    {
        const float back = loop_[static_cast<size_t>(write_)]; // the oldest sample: a full loop ago
        low_ += lowStep_ * (back - low_);
        float u = input_.process(x) + loopGain * low_;
        auto* m = memory_.data() + position_;
        for (int s = 0; s < stages; ++s, m += k_)
        {
            const float old = *m;
            const float w = u - coefficient * old;
            u = coefficient * w + old;
            *m = w;
        }
        if (++position_ == k_)
            position_ = 0;
        loop_[static_cast<size_t>(write_)] = u;
        if (++write_ == length_)
            write_ = 0;
        if (std::abs(low_) < 1.0e-15f)
            low_ = 0.0f;
        return output_.process(u);
    }

private:
    std::vector<float> memory_, loop_;
    int k_ = 1, position_ = 0, length_ = 8, write_ = 0;
    float low_ = 0.0f, lowStep_ = 0.5f;
    Biquad input_, output_;
};

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
    std::vector<std::string> modes;
    for (const auto& m : headModes)
        modes.emplace_back(m.name);
    return {choice("Heads", std::move(modes), 4.0f), // heads 1 + 3
            {"Time", "ms", minTimeMs, maxTimeMs, 180.0f, true},
            {"Sync", "", 0.0f, static_cast<float>(sync::choiceCount - 1), 0.0f, false, sync::choices()},
            {"Feedback", "%", 0.0f, maxFeedbackPercent, 45.0f, false},
            {"Bass", "dB", -12.0f, 12.0f, 0.0f, false},
            {"Treble", "dB", -12.0f, 12.0f, 0.0f, false},
            {"Wow", "%", 0.0f, 100.0f, 20.0f, false},
            {"Flutter", "%", 0.0f, 100.0f, 15.0f, false},
            {"Age", "%", 0.0f, 100.0f, 30.0f, false},
            {"Drive", "dB", -12.0f, 18.0f, 0.0f, false},
            {"Spread", "%", 0.0f, 100.0f, 60.0f, false},
            choice("Stereo", {"Heads", "Ping-pong", "Stereo"}, 0.0f),
            {"Spring", "%", 0.0f, 100.0f, 0.0f, false},
            {"Mix", "%", 0.0f, 100.0f, 35.0f, false},
            choice("Kill dry", {"Off", "On"}, 0.0f),
            {"Output", "dB", -24.0f, 12.0f, 0.0f, false}};
}

// The per-sample smoothed gains: each head's level, the routing of the inputs
// to the two tracks, of the heads back to the record head and of each head to
// the outputs, and the plain gains. A mode or head change moves them over 15 ms.
namespace g
{
enum
{
    head0 = 0, // + k
    in00 = head0 + headCount,
    in01,
    in10,
    in11, // track t from input L / R
    fb00,
    fb01,
    fb10,
    fb11,           // track t from track u's heads
    pan = fb11 + 1, // + 4 * k: track 0 to L, track 0 to R, track 1 to L, track 1 to R
    feedback = pan + 4 * headCount,
    drive,
    driveComp,
    wet,
    dry,
    out,
    springSend,
    wow,
    flutter,
    count
};
}

class TapeEcho final : public ProcessorBase
{
public:
    TapeEcho() : ProcessorBase(type, parameterInfos()) {}

    engine::ProcessorFeedback* feedback() noexcept override { return &feedback_; }

private:
    struct Track
    {
        std::vector<float> tape;
        LoopFilters loop;
        Biquad preEmphasis, deEmphasis;
        Saturator saturator;
        float envelope = 0.0f;
    };

    void prepareAudio(double sampleRate, int maxBlockSize) override
    {
        NORTHLIGHT_ASSERT_NOT_REALTIME("TapeEcho::prepareAudio");
        feedback_.prepare(sampleRate);
        size_ = static_cast<int>(sampleRate * bufferSeconds) + 8;
        for (auto& t : tracks_)
        {
            t.tape.assign(static_cast<size_t>(size_), 0.0f);
            t.preEmphasis.setHighShelf(sampleRate, 3000.0, 6.0);
            t.deEmphasis.setHighShelf(sampleRate, 3000.0, -6.0);
            for (auto* b : {&t.loop.highPass, &t.loop.bassShelf, &t.loop.trebleShelf, &t.loop.highLoss})
                b->reset();
            t.preEmphasis.reset();
            t.deEmphasis.reset();
            t.saturator.reset();
            t.envelope = 0.0f;
        }
        positions_.assign(static_cast<size_t>(size_), 0.0);
        scratchLeft_.assign(static_cast<size_t>(std::max(1, maxBlockSize)), 0.0f);
        scratchRight_.assign(static_cast<size_t>(std::max(1, maxBlockSize)), 0.0f);
        springs_[0].prepare(sampleRate, 0.0361);
        springs_[1].prepare(sampleRate, 0.0417);

        // The tape as if it had been running at the default speed: positions
        // fall behind the record head one sample's travel at a time.
        const double d1 = headOneSeconds(parameter(time), parameter(sync), 120.0);
        const double v = 1.0 / (d1 * sampleRate);
        write_ = 0;
        tapePosition_ = 0.0;
        for (int age = 1; age <= size_; ++age)
            positions_[static_cast<size_t>(slotAgo(age))] = tapePosition_ - (age - 1) * v;
        for (int k = 0; k < headCount; ++k)
            readSlot_[static_cast<size_t>(k)] =
                slotAgo(std::min(size_ - 4, static_cast<int>(headSpacing[static_cast<size_t>(k)] / v) + 3));
        logDelay_ = std::log(d1);
        motorVelocity_ = 0.0;
        primed_ = false;
        smoothStep_ = 1.0f - std::exp(-1.0f / (0.015f * static_cast<float>(sampleRate)));
        envelopeAttack_ = 1.0f - std::exp(-1.0f / (0.01f * static_cast<float>(sampleRate)));
        envelopeRelease_ = std::exp(-1.0f / (1.5f * static_cast<float>(sampleRate)));
        smoothed_.fill(0.0f);
        targets_.fill(0.0f);
        wowPhase_ = flutterPhase_ = 0.0;
        wowDrift_ = flutterDrift_ = {};
        rng_ = {};
        dropoutLeft_ = 0;
        dropoutLength_ = 1;
        dropoutDepth_ = 1.0f;
        nextDropout_ = static_cast<int>(sampleRate);
        designedBass_ = designedTreble_ = designedAge_ = -1000.0f;
        designedDelay_ = -1.0;
        bassDb_ = trebleDb_ = 0.0f;
    }

    int slotAgo(int age) const noexcept // age 1: the newest sample
    {
        int slot = write_ - age;
        while (slot < 0)
            slot += size_;
        return slot;
    }
    int wrap(int slot) const noexcept { return slot >= size_ ? slot - size_ : slot; }

    void processAudio(float* left, float* right, int numSamples) noexcept override
    {
        render(left, right, numSamples, true);
    }

    // Bypassed, the machine keeps running on the input (so switching it back on
    // brings in echoes of what just played, not of what played before); the
    // input passes untouched.
    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        const int n = std::min(numSamples, static_cast<int>(scratchLeft_.size()));
        std::copy(left, left + n, scratchLeft_.data());
        std::copy(right, right + n, scratchRight_.data());
        render(scratchLeft_.data(), scratchRight_.data(), n, false);
        feedback_.addBypassed(left, right, numSamples);
    }

    void setTargets() noexcept
    {
        const std::uint8_t mask = headMask(parameter(heads));
        const int active = activeHeadCount(mask);
        const int mode = std::clamp(static_cast<int>(std::lround(parameter(stereo))), 0, 2);
        const float width = std::clamp(parameter(spread) * 0.01f, 0.0f, 1.0f);
        auto& t = targets_;
        t.fill(0.0f);
        int order = 0;
        for (int k = 0; k < headCount; ++k)
        {
            const bool on = ((mask >> k) & 1) != 0;
            t[static_cast<size_t>(g::head0 + k)] = on ? 1.0f : 0.0f;
            auto* p = &t[static_cast<size_t>(g::pan + 4 * k)];
            if (mode == stereoHeads)
            {
                // The active heads alternate left and right in the order they
                // play; a lone head sits in the middle. A balance law: centre is
                // full level on both sides.
                const float pan = active <= 1 ? 0.0f : (order % 2 == 0 ? -width : width);
                p[0] = std::min(1.0f, 1.0f - pan);
                p[1] = std::min(1.0f, 1.0f + pan);
            }
            else if (mode == stereoPingPong)
            {
                p[0] = 1.0f;
                p[1] = 1.0f - width;
                p[2] = 1.0f - width;
                p[3] = 1.0f;
            }
            else
            {
                const float a = 0.5f * (1.0f + width), b = 0.5f * (1.0f - width);
                p[0] = a;
                p[1] = b;
                p[2] = b;
                p[3] = a;
            }
            if (on)
                ++order;
        }
        if (mode == stereoTracks)
        {
            t[g::in00] = 1.0f;
            t[g::in11] = 1.0f;
            t[g::fb00] = 1.0f;
            t[g::fb11] = 1.0f;
        }
        else
        {
            t[g::in00] = t[g::in01] = 0.5f;
            if (mode == stereoPingPong)
                t[g::fb01] = t[g::fb10] = 1.0f;
            else
                t[g::fb00] = 1.0f;
        }
        const float driveDb = parameter(drive);
        const float mixAmount = std::clamp(parameter(mix) * 0.01f, 0.0f, 1.0f);
        t[g::feedback] = std::clamp(parameter(Param::feedback) * 0.01f, 0.0f, maxFeedbackPercent * 0.01f);
        t[g::drive] = dbToGain(driveDb);
        t[g::driveComp] = dbToGain(-0.5f * driveDb);
        t[g::wet] = mixAmount;
        t[g::dry] = parameter(killDry) >= 0.5f ? 0.0f : 1.0f - mixAmount;
        t[g::out] = dbToGain(parameter(output));
        t[g::springSend] = std::clamp(parameter(spring) * 0.01f, 0.0f, 1.0f);
        t[g::wow] = std::clamp(parameter(wow) * 0.01f, 0.0f, 1.0f);
        t[g::flutter] = std::clamp(parameter(flutter) * 0.01f, 0.0f, 1.0f);
    }

    // The loop filters follow the tone knobs (smoothed per block) and the motor
    // (a slower tape is darker), redesigned only when something moved.
    void designFilters(int numSamples, double headOne) noexcept
    {
        const float blockStep =
            1.0f - std::exp(-static_cast<float>(numSamples) / (0.03f * static_cast<float>(sampleRate_)));
        bassDb_ += (parameter(bass) - bassDb_) * blockStep;
        trebleDb_ += (parameter(treble) - trebleDb_) * blockStep;
        const float ageNow = parameter(age);
        if (std::abs(bassDb_ - designedBass_) < 0.01f && std::abs(trebleDb_ - designedTreble_) < 0.01f &&
            ageNow == designedAge_ && std::abs(headOne / designedDelay_ - 1.0) < 0.002)
            return;
        designedBass_ = bassDb_;
        designedTreble_ = trebleDb_;
        designedAge_ = ageNow;
        designedDelay_ = headOne;
        for (auto& t : tracks_)
            t.loop.design(sampleRate_, bassDb_, trebleDb_, ageNow, headOne);
    }

    // One playback head's sample from a track: the tape at the head's position,
    // found between the positions of the samples written around it (cubic).
    float readHead(const Track& t, int slot, float fraction) const noexcept
    {
        const auto& tape = t.tape;
        const int s0 = slot == 0 ? size_ - 1 : slot - 1;
        const int s2 = wrap(slot + 1), s3 = wrap(slot + 2);
        const float y0 = tape[static_cast<size_t>(s0)], y1 = tape[static_cast<size_t>(slot)];
        const float y2 = tape[static_cast<size_t>(s2)], y3 = tape[static_cast<size_t>(s3)];
        // Catmull-Rom.
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fraction + c2) * fraction + c1) * fraction + y1;
    }

    void render(float* left, float* right, int numSamples, bool publish) noexcept
    {
        const auto head = playHead();
        const double rate = sampleRate_;
        feedback_.setTempoBpm(head.tempoBpm);
        const double targetDelay = headOneSeconds(parameter(time), parameter(sync), head.tempoBpm);
        setTargets();
        if (!primed_)
        {
            // The first block takes the settings as they are (they arrive after prepare).
            logDelay_ = std::log(targetDelay);
            motorVelocity_ = 0.0;
            smoothed_ = targets_;
            bassDb_ = parameter(bass);
            trebleDb_ = parameter(treble);
            primed_ = true;
        }
        designFilters(numSamples, std::exp(logDelay_));

        const float ageAmount = std::clamp(parameter(age) * 0.01f, 0.0f, 1.0f);
        const float hiss = ageAmount > 0.0f ? dbToGain(-90.0f + 36.0f * ageAmount) : 0.0f;
        const float dropoutRate = ageAmount > 0.05f ? 0.1f + 1.4f * ageAmount * ageAmount : 0.0f; // a second
        const double logTarget = std::log(targetDelay);
        constexpr double omega = 2.0 * std::numbers::pi * motorHz;
        const double dt = 1.0 / rate;
        const double wowStep = 2.0 * std::numbers::pi * 0.55 / rate;
        const auto fs = static_cast<float>(rate);
        auto& s = smoothed_;
        const auto& t = targets_;

        for (int n = 0; n < numSamples; ++n)
        {
            for (size_t i = 0; i < s.size(); ++i)
                s[i] += (t[i] - s[i]) * smoothStep_;

            // The motor settles on its speed; the tape moves at it, wobbling.
            const double acceleration =
                omega * omega * (logTarget - logDelay_) - 2.0 * motorDamping * omega * motorVelocity_;
            motorVelocity_ = std::clamp(motorVelocity_ + acceleration * dt, -6.0, 6.0);
            logDelay_ += motorVelocity_ * dt;
            // Settled: the motor rests on its speed, so the exp and sqrt below are
            // worked out once rather than every sample.
            if (std::abs(motorVelocity_) < 1.0e-12 && std::abs(logTarget - logDelay_) < 1.0e-12)
            {
                logDelay_ = logTarget;
                motorVelocity_ = 0.0;
            }
            if (logDelay_ != headLog_)
            {
                headLog_ = logDelay_;
                headOne_ = std::exp(logDelay_);
                flutterHz_ = std::clamp(7.0 * std::sqrt(0.18 / headOne_), 3.0, 14.0);
            }
            const double headOne = headOne_;
            wowPhase_ += wowStep;
            if (wowPhase_ > 2.0 * std::numbers::pi)
                wowPhase_ -= 2.0 * std::numbers::pi;
            // The capstan's flutter turns with the motor: faster at a higher speed.
            flutterPhase_ += 2.0 * std::numbers::pi * flutterHz_ / rate;
            if (flutterPhase_ > 2.0 * std::numbers::pi)
                flutterPhase_ -= 2.0 * std::numbers::pi;
            const float wowShape =
                0.55f * static_cast<float>(std::sin(wowPhase_)) + 0.45f * wowDrift_.next(rng_, fs / 1.3f);
            const float flutterShape = 0.5f * static_cast<float>(std::sin(flutterPhase_)) +
                                       0.5f * flutterDrift_.next(rng_, fs / 18.0f);
            const double speed =
                (1.0 + s[g::wow] * wowDepth * wowShape + s[g::flutter] * flutterDepth * flutterShape) /
                (headOne * rate);
            tapePosition_ += speed;

            // The playback heads.
            float h[2][headCount] = {};
            float gainSum = 0.0f;
            for (int k = 0; k < headCount; ++k)
            {
                const double target = tapePosition_ - headSpacing[static_cast<size_t>(k)];
                int slot = readSlot_[static_cast<size_t>(k)];
                int next = wrap(slot + 1);
                while (next != write_ && positions_[static_cast<size_t>(next)] <= target)
                {
                    slot = next;
                    next = wrap(slot + 1);
                }
                readSlot_[static_cast<size_t>(k)] = slot;
                const float gain = s[static_cast<size_t>(g::head0 + k)];
                gainSum += gain;
                if (gain < 1.0e-4f || next == write_)
                    continue;
                const double p0 = positions_[static_cast<size_t>(slot)];
                const double span = positions_[static_cast<size_t>(next)] - p0;
                const float fraction =
                    span > 0.0 ? static_cast<float>(std::clamp((target - p0) / span, 0.0, 1.0)) : 0.0f;
                h[0][k] = gain * readHead(tracks_[0], slot, fraction);
                h[1][k] = gain * readHead(tracks_[1], slot, fraction);
            }
            float sum[2] = {0.0f, 0.0f};
            float wetL = 0.0f, wetR = 0.0f;
            for (int k = 0; k < headCount; ++k)
            {
                sum[0] += h[0][k];
                sum[1] += h[1][k];
                const auto* p = &s[static_cast<size_t>(g::pan + 4 * k)];
                wetL += p[0] * h[0][k] + p[2] * h[1][k];
                wetR += p[1] * h[0][k] + p[3] * h[1][k];
            }
            // The feedback is the heads' mean (so 100% is the edge of
            // oscillation whatever the mode); the output their power sum.
            const float heads = std::max(1.0f, gainSum);
            const float fbScale = s[g::feedback] / heads;
            const float outScale = s[g::driveComp] / std::sqrt(heads);

            // The record head: input and feedback through the loop's tone, the
            // tape (pre-emphasis, saturation, de-emphasis), hiss and the tape's
            // high loss, a worn spot now and then.
            float dropout = 1.0f;
            if (dropoutLeft_ > 0)
            {
                const float phase =
                    1.0f - static_cast<float>(dropoutLeft_) / static_cast<float>(dropoutLength_);
                dropout = 1.0f - (1.0f - dropoutDepth_) * std::sin(std::numbers::pi_v<float> * phase);
                --dropoutLeft_;
            }
            else if (dropoutRate > 0.0f && --nextDropout_ <= 0)
            {
                dropoutLength_ = std::max(1, static_cast<int>(fs * (0.015f + 0.045f * rng_.unit())));
                dropoutLeft_ = dropoutLength_;
                dropoutDepth_ = dbToGain(-(1.5f + 6.0f * rng_.unit()) * ageAmount);
                nextDropout_ = static_cast<int>(-std::log(std::max(1.0e-6f, rng_.unit())) * fs / dropoutRate);
            }
            const float inL = left[n], inR = right[n];
            const float in[2] = {s[g::in00] * inL + s[g::in01] * inR, s[g::in10] * inL + s[g::in11] * inR};
            const float back[2] = {(s[g::fb00] * sum[0] + s[g::fb01] * sum[1]) * fbScale,
                                   (s[g::fb10] * sum[0] + s[g::fb11] * sum[1]) * fbScale};
            float recorded = 0.0f, hottest = 0.0f;
            for (size_t ch = 0; ch < 2; ++ch)
            {
                auto& tr = tracks_[ch];
                float x = s[g::drive] * in[ch] + back[ch];
                x = tr.loop.highPass.process(x);
                x = tr.loop.bassShelf.process(x);
                x = tr.loop.trebleShelf.process(x);
                const float driven = tr.preEmphasis.process(x);
                const float saturated = tr.saturator.process(driven);
                hottest = std::max(hottest, std::abs(driven));
                float y = tr.deEmphasis.process(saturated);
                const float e = std::abs(y);
                tr.envelope = e > tr.envelope ? tr.envelope + (e - tr.envelope) * envelopeAttack_
                                              : tr.envelope * envelopeRelease_;
                if (tr.envelope < 1.0e-9f)
                    tr.envelope = 0.0f;
                // Hiss under the echoes: it breathes with what is on the tape
                // rather than hissing over silence.
                y += rng_.next() * hiss * std::min(1.0f, tr.envelope * 8.0f);
                y = tr.loop.highLoss.process(y) * dropout;
                tr.tape[static_cast<size_t>(write_)] = y;
                recorded = std::max(recorded, std::abs(y));
            }
            positions_[static_cast<size_t>(write_)] = tapePosition_;
            if (++write_ == size_)
                write_ = 0;

            wetL *= outScale;
            wetR *= outScale;
            // The spring hangs off the echoes: the send feeds it, its tail rings on.
            wetL += springs_[0].process(wetL * s[g::springSend]) * 0.7f;
            wetR += springs_[1].process(wetR * s[g::springSend]) * 0.7f;
            const float outL = (s[g::dry] * inL + s[g::wet] * wetL) * s[g::out];
            const float outR = (s[g::dry] * inR + s[g::wet] * wetR) * s[g::out];
            left[n] = outL;
            right[n] = outR;
            if (publish)
            {
                // The saturation: how far the static curve falls below a straight
                // line at the hottest sample going onto the tape (above about
                // -36 dBFS; the curve's compression is nil below that).
                float squashDb = 0.0f;
                if (hottest > 0.0158f)
                    squashDb = gainToDb(hottest / static_cast<float>(Saturator::curve(hottest)));
                feedback_.addSample(std::max(std::abs(inL), std::abs(inR)),
                                    std::max(std::abs(outL), std::abs(outR)),
                                    squashDb,
                                    recorded,
                                    static_cast<float>(headOne));
            }
        }
        if (std::abs(motorVelocity_) < 1.0e-12)
            motorVelocity_ = 0.0;
    }

    engine::ProcessorFeedback feedback_;
    std::array<Track, 2> tracks_;
    std::vector<double> positions_; // the tape position each slot was recorded at
    std::vector<float> scratchLeft_, scratchRight_;
    std::array<Spring, 2> springs_;
    std::array<int, headCount> readSlot_{};
    std::array<float, g::count> smoothed_{}, targets_{};
    int size_ = 1, write_ = 0;
    double tapePosition_ = 0.0;
    double logDelay_ = 0.0, motorVelocity_ = 0.0;
    // exp(logDelay_) and the flutter rate it sets, kept while logDelay_ holds.
    double headLog_ = 1.0e300, headOne_ = 1.0, flutterHz_ = 7.0;
    double wowPhase_ = 0.0, flutterPhase_ = 0.0;
    Drift wowDrift_, flutterDrift_;
    Rng rng_;
    int dropoutLeft_ = 0, dropoutLength_ = 1, nextDropout_ = 48000;
    float dropoutDepth_ = 1.0f;
    float smoothStep_ = 0.01f, envelopeAttack_ = 0.01f, envelopeRelease_ = 0.999f;
    float bassDb_ = 0.0f, trebleDb_ = 0.0f;
    float designedBass_ = -1000.0f, designedTreble_ = -1000.0f, designedAge_ = -1000.0f;
    double designedDelay_ = -1.0;
    bool primed_ = false;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<TapeEcho>();
}

} // namespace northlight::plugins::tapeecho
