#include "plugins/Bloom.h"

#include "engine/Feedback.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BeatSync.h"
#include "plugins/Biquad.h"
#include "plugins/ProcessorBase.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <string>
#include <vector>

namespace northlight::plugins::bloom
{

namespace
{
constexpr int lineCount = static_cast<int>(lineMs.size());
constexpr float pi = std::numbers::pi_v<float>;
// Highs above the damping (and lows below the low cut) decay this many times
// faster than the decay: a shelf in each line whose depth follows the line's
// gain, so the ratio holds at every decay and vanishes as the decay grows long.
constexpr float bandDecayRatio = 4.0f;
constexpr float speedOfSound = 343.0f;
// The diffused input in the tail before the network's first pass returns.
constexpr float earlyGain = 0.6f;
// The size whose level the others are matched to (the default).
constexpr float referenceSizePercent = 60.0f;
// Below this the shimmer's heads add in phase (a gain above one), so the shifted path is cut there.
constexpr double shiftLowCutHz = 60.0;
// Full shimmer turns each pass this far (radians) from the unshifted sound to the shifted.
constexpr float maxShimmerAngle = 0.7f;

float meanLineSeconds() noexcept
{
    float sum = 0.0f;
    for (const float ms : lineMs)
        sum += ms;
    return sum * 0.001f / static_cast<float>(lineCount);
}

float meanAllpassSeconds() noexcept
{
    float sum = 0.0f;
    for (const float ms : loopAllpassMs)
        sum += ms;
    return sum * 0.001f / static_cast<float>(lineCount);
}

int choiceIndex(float value, int count) noexcept
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

// The one-pole coefficient for a corner frequency.
float onePole(float hz, double sampleRate) noexcept
{
    return 1.0f - std::exp(-2.0f * pi * hz / static_cast<float>(sampleRate));
}
} // namespace

float sizeScale(float percent) noexcept
{
    const float f = std::clamp(percent, 0.0f, 100.0f) * 0.01f;
    return minScale * std::pow(maxScale / minScale, f);
}

float sizeMetres(float percent) noexcept
{
    return meanLineSeconds() * sizeScale(percent) * speedOfSound;
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
    constexpr double rate = 48000.0;
    const double length = meanLineSeconds() * sizeScale(sizePercent) + meanAllpassSeconds();
    const double rt = std::clamp(static_cast<double>(decaySeconds), 0.01, 1.0e6);
    const double g = std::pow(10.0, -3.0 * length / rt);
    const double k = std::pow(g, bandDecayRatio - 1.0);
    const std::complex<double> z1 = std::polar(1.0, -2.0 * std::numbers::pi * hz / rate);
    const auto lowPass = [&](float corner)
    {
        const double a = onePole(corner, rate);
        return a / (1.0 - (1.0 - a) * z1);
    };
    const auto high = lowPass(dampingHz);
    const auto low = lowPass(lowCutHz);
    const double shelves = std::abs(high + k * (1.0 - high)) * std::abs((1.0 - low) + k * low);
    const double passDb = 20.0 * std::log10(std::max(1.0e-9, g * shelves));
    return static_cast<float>(passDb < -1.0e-9 ? -60.0 * length / passDb : rt);
}

namespace
{
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
    return {{"Decay", "s", minDecaySeconds, maxDecaySeconds, 6.0f, true},
            {"Size", "%", 0.0f, 100.0f, 60.0f, false},
            choice("Freeze", {"Off", "On"}, 0.0f),
            {"Shimmer", "%", 0.0f, 100.0f, 0.0f, false},
            choice("Pitch", {"-12", "+5", "+7", "+12", "+19", "+12 / -12"}, 3.0f),
            {"Swell", "ms", swellOffMs, maxSwellMs, swellOffMs, true},
            {"Pre-delay", "ms", 0.0f, maxPreDelayMs, 20.0f, false},
            choice("Pre-delay sync", sync::choices(), 0.0f),
            {"Diffusion", "%", 0.0f, 100.0f, 80.0f, false},
            {"Modulation", "%", 0.0f, 100.0f, 30.0f, false},
            {"Rate", "Hz", 0.05f, 5.0f, 0.5f, true},
            {"Low cut", "Hz", 20.0f, 1000.0f, 80.0f, true},
            {"Damping", "Hz", 500.0f, 20000.0f, 6000.0f, true},
            {"Tilt", "dB", -6.0f, 6.0f, 0.0f, false},
            {"Duck", "%", 0.0f, 100.0f, 0.0f, false},
            {"Duck release", "ms", 50.0f, 3000.0f, 400.0f, true},
            {"Gate", "ms", gateOffMs, maxGateMs, gateOffMs, true},
            {"Width", "%", 0.0f, 100.0f, 100.0f, false},
            {"Mix", "%", 0.0f, 100.0f, 35.0f, false}};
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
    std::uint32_t seed = 0x9e3779b9u;
    const auto next = [&seed]
    {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / 16777216.0f; // 0..1
    };
    for (auto& p : patterns)
    {
        float energy = 0.0f;
        for (int k = 0; k < swellTaps; ++k)
        {
            const float at = (static_cast<float>(k) + 0.15f + 0.7f * next()) / static_cast<float>(swellTaps);
            const float g = std::pow(at, 1.6f) * (next() < 0.5f ? -1.0f : 1.0f);
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

// A Schroeder allpass with a fixed length.
struct Allpass
{
    std::vector<float> buffer;
    int pos = 0;

    float process(float x, float g) noexcept
    {
        const float delayed = buffer[static_cast<size_t>(pos)];
        const float w = x + g * delayed;
        buffer[static_cast<size_t>(pos)] = w;
        if (++pos == static_cast<int>(buffer.size()))
            pos = 0;
        return delayed - g * w;
    }
    void prepare(float seconds, double sampleRate)
    {
        buffer.assign(static_cast<size_t>(std::max(1.0, seconds * sampleRate)), 0.0f);
        pos = 0;
    }
    void clear() noexcept { std::fill(buffer.begin(), buffer.end(), 0.0f); }
};

int powerOfTwoAtLeast(double n) noexcept
{
    int size = 1;
    while (size < n)
        size <<= 1;
    return size;
}

// A cubic Hermite read `delay` samples behind `write` (delay >= 3).
inline float readHermite(const float* buffer, int mask, int write, float delay) noexcept
{
    const float position = static_cast<float>(write) - delay;
    const float floor = std::floor(position);
    const int i = static_cast<int>(floor);
    const float t = position - floor;
    const float x0 = buffer[(i - 1) & mask], x1 = buffer[i & mask];
    const float x2 = buffer[(i + 1) & mask], x3 = buffer[(i + 2) & mask];
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
}

inline float readLinear(const float* buffer, int mask, int write, float delay) noexcept
{
    const float position = static_cast<float>(write) - delay;
    const float floor = std::floor(position);
    const int i = static_cast<int>(floor);
    const float t = position - floor;
    return buffer[i & mask] + t * (buffer[(i + 1) & mask] - buffer[i & mask]);
}

// sin(pi x) for x in 0..1 (Bhaskara's approximation, within 0.2%): the shimmer's
// grain window, whose two heads half a grain apart sum to constant power.
inline float grainWindow(float x) noexcept
{
    const float p = x * (1.0f - x);
    return 16.0f * p / (5.0f - 4.0f * p);
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
        const int inputSize = powerOfTwoAtLeast(inputSeconds * sampleRate + 8.0);
        for (auto& b : input_)
            b.assign(static_cast<size_t>(inputSize), 0.0f);
        inputMask_ = inputSize - 1;
        inputWrite_ = 0;

        // The lines: the longest line at the largest size, the deepest
        // modulation and the shimmer's window.
        const double lineSeconds =
            lineMs.back() * 0.001 * maxScale + 2.0 * maxModSeconds + shimmerWindowSeconds + 0.002;
        const int lineSize = powerOfTwoAtLeast(lineSeconds * sampleRate + 8.0);
        for (auto& line : lines_)
        {
            line.buffer.assign(static_cast<size_t>(lineSize), 0.0f);
            line.high = line.low = 0.0f;
            line.guard.reset();
            line.shiftLowCut.setHighPass(sampleRate, shiftLowCutHz);
            line.shiftLowCut.reset();
        }
        guardPitch_ = -1.0f; // designed again at this rate
        lineMask_ = lineSize - 1;
        lineWrite_ = 0;
        for (int i = 0; i < lineCount; ++i)
        {
            lines_[static_cast<size_t>(i)].allpass.prepare(loopAllpassMs[static_cast<size_t>(i)] * 0.001f,
                                                           sampleRate);
            // Each line's modulation at its own rate and phase, its grain at its own phase.
            lines_[static_cast<size_t>(i)].lfoCos = std::cos(2.0f * pi * static_cast<float>(i) / lineCount);
            lines_[static_cast<size_t>(i)].lfoSin = std::sin(2.0f * pi * static_cast<float>(i) / lineCount);
            lines_[static_cast<size_t>(i)].grain = static_cast<float>(i) / lineCount;
        }
        constexpr std::array<float, 4> leftMs{1.7f, 2.9f, 5.3f, 7.9f}, rightMs{1.9f, 3.1f, 5.9f, 8.3f};
        for (size_t i = 0; i < diffusers_[0].size(); ++i)
        {
            diffusers_[0][i].prepare(leftMs[i] * 0.001f, sampleRate);
            diffusers_[1][i].prepare(rightMs[i] * 0.001f, sampleRate);
        }
        dcLow_ = {0.0f, 0.0f};
        tiltLow_ = {0.0f, 0.0f};
        energy_ = 0.0f;
        governor_ = 1.0f;
        duckEnvelope_ = keyEnvelope_ = keyPeak_ = 0.0f;
        gateHold_ = 0;
        gateGain_ = 1.0f;
        tailSquare_ = drySquare_ = 0.0f;
        silentSamples_ = 0;
        idle_ = false;
        primed_ = false; // the first block takes the parameters as they are (they arrive after prepare)

        const auto rate = static_cast<float>(sampleRate);
        const auto smoothing = [rate](float seconds) { return 1.0f - std::exp(-1.0f / (seconds * rate)); };
        sizeStep_ = smoothing(0.25f);   // a size change glides, like tape
        freezeStep_ = smoothing(0.06f); // freeze fades in and out over about 150 ms
        glideStep_ = smoothing(0.05f);  // the pre-delay
        swellStep_ = smoothing(0.2f);   // the swell's spread
        paramStep_ = smoothing(0.02f);  // the levels
        energyStep_ = smoothing(0.02f); // the governor's measure
        governorDown_ = smoothing(0.005f);
        governorUp_ = smoothing(0.3f);
        duckAttack_ = smoothing(0.005f);
        keyRelease_ = smoothing(0.02f);
        keyPeakFall_ = std::pow(10.0f, -12.0f / 20.0f / rate); // the key's reference falls 12 dB a second
        gateOpen_ = smoothing(0.001f);
        gateClose_ = smoothing(0.004f);
        rmsStep_ = smoothing(0.05f);
        dcStep_ = onePole(15.0f, sampleRate);
        tiltStep_ = onePole(tiltHz, sampleRate);
        windowSamples_ = shimmerWindowSeconds * rate;
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
        t.duckRelease = 1.0f - std::exp(-1.0f / (std::max(0.01f, parameter(duckRelease) * 0.001f) * rate));
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
        idle_ = silentSamples_ > inputMask_ + 1 && energy_ < 1.0e-14f;
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
        const float diffuserGain = 0.7f * t.diffusion;
        const float loopAllpassGain = 0.5f * t.diffusion;
        const float dampStep = onePole(std::clamp(parameter(damping), 500.0f, 20000.0f), sampleRate_);
        const float lowStep = onePole(std::clamp(parameter(lowCut), 20.0f, 1000.0f), sampleRate_);
        const float rt = std::clamp(parameter(decay), minDecaySeconds, maxDecaySeconds);
        const float lfoHz = std::max(0.01f, parameter(modRate));
        float ratio[lineCount], grainStep[lineCount], lfoRotCos[lineCount], lfoRotSin[lineCount];
        bool up[lineCount];
        constexpr std::array<float, lineCount> lfoSpread{
            1.0f, 1.13f, 0.87f, 1.29f, 0.71f, 1.07f, 0.93f, 1.21f};
        const bool pitchMoved = parameter(pitch) != guardPitch_;
        guardPitch_ = parameter(pitch);
        for (int i = 0; i < lineCount; ++i)
        {
            ratio[i] = pitchRatio(parameter(pitch), i);
            up[i] = ratio[i] >= 1.0f;
            // What a line holds is band-limited below where shifting it up would
            // fold it back down (and pile up at the top for ever).
            if (pitchMoved)
            {
                auto& guard = lines_[static_cast<size_t>(i)].guard;
                if (up[i])
                    guard.setLowPass(sampleRate_, std::min(18000.0, 0.4 * sampleRate_ / ratio[i]));
                else
                    guard.setLowPass(sampleRate_, std::min(18000.0, 0.4 * sampleRate_));
            }
            grainStep[i] = std::abs(ratio[i] - 1.0f) / windowSamples_;
            const float w = 2.0f * pi * lfoHz * lfoSpread[static_cast<size_t>(i)] / rate;
            lfoRotCos[i] = std::cos(w);
            lfoRotSin[i] = std::sin(w);
        }
        // The lines' gains for the decay, at the start and end of the block (the
        // size glides, so the lengths do too).
        const float allpassSamples[lineCount] = {loopAllpassMs[0] * 0.001f * rate,
                                                 loopAllpassMs[1] * 0.001f * rate,
                                                 loopAllpassMs[2] * 0.001f * rate,
                                                 loopAllpassMs[3] * 0.001f * rate,
                                                 loopAllpassMs[4] * 0.001f * rate,
                                                 loopAllpassMs[5] * 0.001f * rate,
                                                 loopAllpassMs[6] * 0.001f * rate,
                                                 loopAllpassMs[7] * 0.001f * rate};
        float baseSamples[lineCount];
        for (int i = 0; i < lineCount; ++i)
            baseSamples[i] = lineMs[static_cast<size_t>(i)] * 0.001f * rate;
        const float shimmerTarget = t.shimmer * (1.0f - t.freeze);
        const bool shimmering = shimmerTarget > 1.0e-4f || shimmer_ > 1.0e-4f;
        const float* inL = input_[0].data();
        const float* inR = input_[1].data();
        float lineGain[lineCount], bandGain[lineCount];
        {
            // The decay at the size this block ends near.
            const float scale = scale_ + (t.scale - scale_) * std::min(1.0f, sizeStep_ * numSamples);
            for (int i = 0; i < lineCount; ++i)
            {
                const float length = baseSamples[i] * scale + allpassSamples[i] + modSamples_;
                lineGain[i] = std::pow(10.0f, -3.0f * length / (rate * rt));
                bandGain[i] = std::pow(lineGain[i], bandDecayRatio - 1.0f);
            }
        }

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
            float sendL = readLinear(inL, inputMask_, inputWrite_, pre);
            float sendR = readLinear(inR, inputMask_, inputWrite_, pre);
            // The swell: the input again through the rising cloud of taps.
            if (swellMix_ > 1.0e-4f)
            {
                float cloudL = 0.0f, cloudR = 0.0f;
                for (int k = 0; k < swellTaps; ++k)
                {
                    const auto kk = static_cast<size_t>(k);
                    cloudL +=
                        patterns_[0].gain[kk] *
                        readLinear(inL, inputMask_, inputWrite_, pre + patterns_[0].position[kk] * swell_);
                    cloudR +=
                        patterns_[1].gain[kk] *
                        readLinear(inR, inputMask_, inputWrite_, pre + patterns_[1].position[kk] * swell_);
                }
                sendL += (cloudL - sendL) * swellMix_;
                sendR += (cloudR - sendR) * swellMix_;
            }
            inputWrite_ = (inputWrite_ + 1) & inputMask_;
            // Diffusion.
            for (auto& d : diffusers_[0])
                sendL = d.process(sendL, diffuserGain);
            for (auto& d : diffusers_[1])
                sendR = d.process(sendR, diffuserGain);
            const float inject = inputGain_ * thaw;
            sendL *= inject;
            sendR *= inject;

            // The network.
            const float sweep = std::max(modSamples_, frozenModSeconds * rate * freeze_);
            float outs[lineCount];
            float energy = 0.0f, sum = 0.0f;
            for (int i = 0; i < lineCount; ++i)
            {
                auto& line = lines_[static_cast<size_t>(i)];
                // The modulation: a sine per line, kept on the unit circle.
                const float c = line.lfoCos * lfoRotCos[i] - line.lfoSin * lfoRotSin[i];
                line.lfoSin = line.lfoSin * lfoRotCos[i] + line.lfoCos * lfoRotSin[i];
                line.lfoCos = c;
                const float delay = std::max(4.0f, baseSamples[i] * scale_ + sweep * (1.0f + line.lfoSin));
                const float* buffer = line.buffer.data();
                float v = readHermite(buffer, lineMask_, lineWrite_, delay);
                if (shimmering)
                {
                    // Two heads sweeping the grain window, half a grain apart:
                    // the line read faster (up) or slower (down) than it is written.
                    line.grain += grainStep[i];
                    if (line.grain >= 1.0f)
                        line.grain -= 1.0f;
                    const float a = line.grain;
                    const float b = a < 0.5f ? a + 0.5f : a - 0.5f;
                    const float offsetA = up[i] ? windowSamples_ * (1.0f - a) : windowSamples_ * a;
                    const float offsetB = up[i] ? windowSamples_ * (1.0f - b) : windowSamples_ * b;
                    // The lowest frequencies come out of the two heads in phase,
                    // louder than they went in, and shifting never moves them on:
                    // they would grow for ever, so they stay out of the shifted path.
                    const float shifted = line.shiftLowCut.process(
                        grainWindow(a) * readHermite(buffer, lineMask_, lineWrite_, delay + offsetA) +
                        grainWindow(b) * readHermite(buffer, lineMask_, lineWrite_, delay + offsetB));
                    v = v * keep + shifted * shift;
                }
                // Damping and low cut: shelves whose depth follows the line's
                // gain (none while frozen).
                const float gain = (lineGain[i] + (1.0f - lineGain[i]) * freeze_) * governor_;
                const float band = bandGain[i] + (1.0f - bandGain[i]) * freeze_;
                line.high += dampStep * (v - line.high);
                v = line.high + band * (v - line.high);
                line.low += lowStep * (v - line.low);
                v = (v - line.low) + band * line.low;
                v = line.allpass.process(v * gain, loopAllpassGain);
                outs[i] = v;
                sum += v;
                energy += v * v;
            }
            // Householder feedback, the input into alternate lines (and into the
            // tail at once, as its first, diffused reflections).
            const float householder = sum * (2.0f / lineCount);
            float tailL = 0.0f, tailR = 0.0f;
            for (int i = 0; i < lineCount; ++i)
            {
                auto& line = lines_[static_cast<size_t>(i)];
                const float sign = (i / 2) % 2 == 0 ? 1.0f : -1.0f;
                const float in = (i % 2 == 0 ? sendL : sendR) * sign;
                float write = outs[i] - householder + in;
                if (shimmering)
                    write += (line.guard.process(write) - write) * shimmer_;
                line.buffer[static_cast<size_t>(lineWrite_)] = std::clamp(write, -8.0f, 8.0f);
                (i % 2 == 0 ? tailL : tailR) += outs[i] * sign;
            }
            lineWrite_ = (lineWrite_ + 1) & lineMask_;
            tailL += earlyGain * sendL;
            tailR += earlyGain * sendR;

            // The governor: above the ceiling, the loop's gain comes down until
            // the energy is back under it.
            energy_ += (energy * (1.0f / lineCount) - energy_) * energyStep_;
            const float rms = std::sqrt(energy_);
            const float target = rms > energyCeiling ? energyCeiling / rms : 1.0f;
            governor_ += (target - governor_) * (target < governor_ ? governorDown_ : governorUp_);

            // The tail: tilt, width.
            tailL *= 0.5f;
            tailR *= 0.5f;
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
            float duckDb = 0.0f;
            if (t.duckDepthDb > 0.0f)
            {
                const float envelopeDb = gainToDb(duckEnvelope_);
                duckDb = t.duckDepthDb * std::clamp((envelopeDb + 54.0f) / 36.0f, 0.0f, 1.0f);
            }
            const float duckGain = duckDb > 0.0f ? std::exp(-duckDb * 0.11512925f) : 1.0f;

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
        if (!std::isfinite(energy_) || !std::isfinite(governor_))
            clearState();
    }

    // Tap 1, which the card's tail display reads: the tail on its own (before
    // the mix), so it shows at any mix.
    void tapTail(int numSamples) noexcept { feedback_.tap(1, wetLeft_.data(), wetRight_.data(), numSamples); }

    void clearState() noexcept
    {
        for (auto& b : input_)
            std::fill(b.begin(), b.end(), 0.0f);
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

    struct Line
    {
        std::vector<float> buffer;
        Allpass allpass;
        float high = 0.0f, low = 0.0f; // the damping's and the low cut's one-pole memories
        float lfoCos = 1.0f, lfoSin = 0.0f;
        float grain = 0.0f; // the shimmer's grain phase, 0..1
        Biquad guard;       // the shimmer's anti-alias low-pass on what is written
        Biquad shiftLowCut; // the shifted path's high-pass
    };

    engine::ProcessorFeedback feedback_;
    std::array<SwellPattern, 2> patterns_;
    std::array<std::vector<float>, 2> input_;
    int inputMask_ = 0, inputWrite_ = 0;
    std::array<Line, lineCount> lines_;
    int lineMask_ = 0, lineWrite_ = 0;
    std::array<std::array<Allpass, 4>, 2> diffusers_;
    std::vector<float> wetLeft_, wetRight_, bypassLeft_, bypassRight_;
    std::array<float, 2> dcLow_{}, tiltLow_{};
    // Smoothed controls.
    float scale_ = 1.0f, freeze_ = 0.0f, shimmer_ = 0.0f, swell_ = 0.0f, swellMix_ = 0.0f, pre_ = 0.0f;
    float modSamples_ = 0.0f, inputGain_ = 1.0f, wet_ = 0.0f, width_ = 1.0f;
    float tiltLowGain_ = 1.0f, tiltHighGain_ = 1.0f;
    // Followers.
    float energy_ = 0.0f, governor_ = 1.0f;
    float duckEnvelope_ = 0.0f, keyEnvelope_ = 0.0f, keyPeak_ = 0.0f, gateGain_ = 1.0f;
    int gateHold_ = 0;
    float tailSquare_ = 0.0f, drySquare_ = 0.0f;
    int silentSamples_ = 0;
    bool idle_ = false, primed_ = false;
    // Coefficients for the rate.
    float sizeStep_ = 0.0f, freezeStep_ = 0.0f, glideStep_ = 0.0f, swellStep_ = 0.0f, paramStep_ = 0.0f;
    float energyStep_ = 0.0f, governorDown_ = 0.0f, governorUp_ = 0.0f, duckAttack_ = 0.0f;
    float keyRelease_ = 0.0f, keyPeakFall_ = 1.0f, gateOpen_ = 0.0f, gateClose_ = 0.0f, rmsStep_ = 0.0f;
    float dcStep_ = 0.0f, tiltStep_ = 0.0f, windowSamples_ = 1.0f;
    float guardPitch_ = -1.0f; // the pitch the guards were designed for
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    return std::make_unique<Bloom>();
}

} // namespace northlight::plugins::bloom
