#include "plugins/Aurora.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/SynthVoices.h"
#include "plugins/dsp/Random.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace northlight::plugins::aurora
{

namespace
{
constexpr double twoPi = 2.0 * std::numbers::pi;
constexpr int controlInterval = 16; // samples between filter and LFO updates

// PolyBLEP: the band-limiting correction at a discontinuity.
inline float polyBlep(double t, double dt) noexcept
{
    if (t < dt)
    {
        const double x = t / dt;
        return static_cast<float>(x + x - x * x - 1.0);
    }
    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt;
        return static_cast<float>(x * x + x + x + 1.0);
    }
    return 0.0f;
}

// One sample of shape `index` (0 sine, 1 triangle, 2 saw, 3 square), band-limited
// where it jumps.
inline float shapeSample(int index, double phase, double dt) noexcept
{
    switch (index)
    {
        case 0:
            return static_cast<float>(std::sin(twoPi * phase));
        case 1:
            return static_cast<float>(phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
        case 2:
            return static_cast<float>(2.0 * phase - 1.0) - polyBlep(phase, dt);
        default:
        {
            double t2 = phase + 0.5;
            if (t2 >= 1.0)
                t2 -= 1.0;
            return (phase < 0.5 ? 1.0f : -1.0f) + polyBlep(phase, dt) - polyBlep(t2, dt);
        }
    }
}

inline float morph(float shape, double phase, double dt) noexcept
{
    const float s = std::clamp(shape, 0.0f, 3.0f);
    const int i = std::min(2, static_cast<int>(s));
    const float t = s - static_cast<float>(i);
    const float a = shapeSample(i, phase, dt);
    if (t <= 0.0f)
        return a;
    return a + (shapeSample(i + 1, phase, dt) - a) * t;
}

// A topology-preserving state variable filter (one channel, 12 dB/octave).
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;
    // Returns low, band and high for one input.
    void run(float x, float g, float k, float& low, float& band, float& high) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + g * a1 * v3;
        const float v2 = ic2 + g * v1;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = x - k * v1 - v2;
    }
    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

using synth::Envelope;
using synth::Stage;

struct Voice
{
    int note = -1;
    float velocity = 0.0f;
    std::uint32_t age = 0;
    double frequency = 440.0, target = 440.0; // Hz (gliding towards target)
    std::array<double, maxUnison> phase1{}, phase2{};
    double subPhase = 0.0;
    Envelope amp, filter;
    std::array<Svf, 2> stage1, stage2; // per channel: the first and (for 24 dB) second stage
    float g = 0.5f, k = 1.4f;          // filter coefficients, per control interval
    bool active() const noexcept { return amp.stage != Stage::off; }
};

class Aurora final : public MeteredBuiltIn
{
public:
    // The output tap feeds the editor's scope.
    Aurora() : MeteredBuiltIn(type, aurora::parameterInfo(), true) {}

    bool acceptsMidi() const noexcept override { return true; }

private:
    void prepareBuiltIn(double sampleRate, int) override
    {
        for (auto& v : voices_)
            v = Voice{};
        age_ = 0;
        lfoPhase_ = 0.0;
        lfoHeld_ = 0.0f;
        lastFrequency_ = 0.0;
        expression_.reset();
        expressionRatio_ = 1.0;
        // Shimmer: up to 30 ms of modulated delay per side.
        const auto length = static_cast<size_t>(sampleRate * 0.03) + 4;
        for (auto& line : chorus_)
            line.assign(length, 0.0f);
        chorusWrite_ = 0;
        chorusPhase_ = 0.0;
    }

    static double noteHz(double note) noexcept { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

    void noteOn(int note, int velocity) noexcept
    {
        // The same note retriggers its voice; else a free one; else the oldest.
        Voice* voice = &synth::allocateVoice(voices_.data(), static_cast<int>(voices_.size()), note);
        const bool fresh = !voice->active();
        voice->note = note;
        voice->velocity = static_cast<float>(velocity) / 127.0f;
        voice->age = ++age_;
        voice->target = noteHz(note);
        // Glide from the last note played (when there was one and glide is set).
        const float glideMs = parameter(glide);
        voice->frequency = glideMs > 0.5f && lastFrequency_ > 0.0 ? lastFrequency_ : voice->target;
        lastFrequency_ = voice->target;
        if (fresh)
        {
            // Free-running unison: each voice starts at its own phase, so the stack
            // sounds wide from the first cycle.
            for (int u = 0; u < maxUnison; ++u)
            {
                voice->phase1[static_cast<size_t>(u)] = rng_.unitDouble();
                voice->phase2[static_cast<size_t>(u)] = rng_.unitDouble();
            }
            voice->subPhase = 0.0;
            for (auto& s : voice->stage1)
                s.reset();
            for (auto& s : voice->stage2)
                s.reset();
            voice->amp.value = 0.0f;
            voice->filter.value = 0.0f;
        }
        voice->amp.start();
        voice->filter.start();
    }

    void noteOff(int note) noexcept
    {
        for (auto& v : voices_)
            if (v.note == note && v.active() && v.amp.stage != Stage::release)
            {
                v.amp.release();
                v.filter.release();
            }
    }

    void handle(const engine::MidiEvent& e) noexcept
    {
        synth::dispatch(
            e,
            expression_,
            [this](int note, int velocity) { noteOn(note, velocity); },
            [this](int note) { noteOff(note); },
            [this]
            {
                for (auto& v : voices_)
                {
                    v.amp.release();
                    v.filter.release();
                }
            });
    }

    float lfoValue(int shape) noexcept
    {
        const double p = lfoPhase_;
        switch (shape)
        {
            case 0:
                return static_cast<float>(std::sin(twoPi * p));
            case 1:
                return static_cast<float>(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p);
            case 2:
                return static_cast<float>(2.0 * p - 1.0);
            case 3:
                return p < 0.5 ? 1.0f : -1.0f;
            default:
                return lfoHeld_;
        }
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        const float sr = static_cast<float>(sampleRate_);
        // Parameters, once per chunk.
        const float shape1 = parameter(osc1Shape), shape2 = parameter(osc2Shape);
        const double ratio1 = std::pow(2.0, std::round(parameter(osc1Octave)));
        const double ratio2 =
            std::pow(2.0,
                     std::round(parameter(osc2Octave)) + std::round(parameter(osc2Semi)) / 12.0 +
                         parameter(osc2Fine) / 1200.0);
        const float level1 = parameter(osc1Level), level2 = parameter(osc2Level);
        const int voicesPerOsc = std::clamp(static_cast<int>(std::lround(parameter(unison))), 1, maxUnison);
        const float spreadCents = parameter(spread);
        const float stereoWidth = parameter(width) * 0.01f;
        const float subLevel = parameter(sub), noiseLevel = parameter(noise);
        const int mode = std::clamp(static_cast<int>(std::lround(parameter(filterMode))), 0, 3);
        const float baseCutoff = parameter(cutoff);
        const float res = parameter(resonance);
        const float driveAmount = parameter(drive);
        const float envOctaves = parameter(filterEnv) * 5.0f;
        const float keyAmount = parameter(keyTrack);
        const auto attackStep = [sr](float ms) { return 1.0f / std::max(1.0f, ms * 0.001f * sr); };
        // Exponential: within 60 dB (a factor of 1000) over the time.
        const auto coefficient = [sr](float ms) { return dsp::decay60ForMs(ms, sr); };
        const float ampA = attackStep(parameter(ampAttack)), ampD = coefficient(parameter(ampDecay));
        const float ampS = parameter(ampSustain), ampR = coefficient(parameter(ampRelease));
        const float filA = attackStep(parameter(filterAttack)), filD = coefficient(parameter(filterDecay));
        const float filS = parameter(filterSustain), filR = coefficient(parameter(filterRelease));
        const int lfoKind = std::clamp(static_cast<int>(std::lround(parameter(lfoShape))), 0, 4);
        const double lfoStep = parameter(lfoRate) / sampleRate_;
        const float lfoToPitch = parameter(lfoPitch), lfoToCutoff = parameter(lfoCutoff);
        const float lfoToAmp = parameter(lfoAmp);
        const float glideMs = parameter(glide);
        const double glideCoef =
            glideMs > 0.5f ? std::exp(-1.0 / (static_cast<double>(glideMs) * 0.001 * sampleRate_)) : 0.0;
        const float shimmerMix = parameter(shimmer);
        const float velocityAmount = parameter(velocity);
        const float outGain = dbToGain(parameter(level));
        const float driveGain = 1.0f + driveAmount * 7.0f;
        const float driveNorm = 1.0f / std::tanh(driveGain);
        // Unison voices: detunes spread evenly across +-spread, pans across the width.
        std::array<double, maxUnison> detune{};
        std::array<float, maxUnison> panL{}, panR{};
        for (int u = 0; u < voicesPerOsc; ++u)
        {
            const float pos =
                voicesPerOsc == 1
                    ? 0.0f
                    : static_cast<float>(u) / static_cast<float>(voicesPerOsc - 1) * 2.0f - 1.0f;
            detune[static_cast<size_t>(u)] = std::pow(2.0, pos * spreadCents / 1200.0);
            const float pan = pos * stereoWidth;
            panL[static_cast<size_t>(u)] = std::sqrt(0.5f * (1.0f - pan));
            panR[static_cast<size_t>(u)] = std::sqrt(0.5f * (1.0f + pan));
        }
        const float unisonNorm = 1.0f / std::sqrt(static_cast<float>(voicesPerOsc));

        size_t next = firstChunkEvent();
        const engine::MidiEventList* events = midi_;
        int active = 0;
        for (int i = 0; i < numSamples; ++i)
        {
            while (events != nullptr && next < events->size() &&
                   std::clamp((*events)[next].sampleOffset - chunkStart_, 0, numSamples) <= i)
                handle((*events)[next++]);

            // Control rate: the LFO and the filters' coefficients.
            if (controlCountdown_ <= 0)
            {
                controlCountdown_ = controlInterval;
                lfo_ = lfoValue(lfoKind);
                // The pitch bend and the mod wheel's vibrato.
                expressionRatio_ =
                    synth::Expression::ratio(expression_.advance(controlInterval / sampleRate_));
                for (auto& v : voices_)
                {
                    if (!v.active())
                        continue;
                    const float velocityScale = 1.0f - velocityAmount + velocityAmount * v.velocity;
                    const float octaves = envOctaves * v.filter.value * velocityScale + lfoToCutoff * lfo_ +
                                          keyAmount * static_cast<float>((v.note - 60) / 12.0);
                    const float hz = std::clamp(baseCutoff * std::exp2(octaves), 20.0f, sr * 0.45f);
                    v.g = std::tan(static_cast<float>(std::numbers::pi) * hz / sr);
                    v.k = 2.0f - 1.96f * res;
                }
            }
            --controlCountdown_;
            lfoPhase_ += lfoStep;
            if (lfoPhase_ >= 1.0)
            {
                lfoPhase_ -= 1.0;
                lfoHeld_ = rng_.bipolar();
            }
            const double pitchLfo = lfoToPitch != 0.0f ? std::pow(2.0, lfo_ * lfoToPitch / 1200.0) : 1.0;

            float outL = 0.0f, outR = 0.0f;
            for (auto& v : voices_)
            {
                if (!v.active())
                    continue;
                if (glideCoef > 0.0)
                    v.frequency = v.target + (v.frequency - v.target) * glideCoef;
                else
                    v.frequency = v.target;
                const double base = v.frequency * pitchLfo * expressionRatio_ / sampleRate_;
                float l = 0.0f, r = 0.0f;
                for (int u = 0; u < voicesPerOsc; ++u)
                {
                    const auto su = static_cast<size_t>(u);
                    const double dt1 = std::min(0.45, base * ratio1 * detune[su]);
                    const double dt2 = std::min(0.45, base * ratio2 * detune[su]);
                    const float s = (level1 * morph(shape1, v.phase1[su], dt1) +
                                     level2 * morph(shape2, v.phase2[su], dt2)) *
                                    unisonNorm;
                    l += s * panL[su];
                    r += s * panR[su];
                    v.phase1[su] += dt1;
                    if (v.phase1[su] >= 1.0)
                        v.phase1[su] -= 1.0;
                    v.phase2[su] += dt2;
                    if (v.phase2[su] >= 1.0)
                        v.phase2[su] -= 1.0;
                }
                const float centre = subLevel * static_cast<float>(std::sin(twoPi * v.subPhase)) +
                                     noiseLevel * rng_.bipolar() * 0.5f;
                v.subPhase += std::min(0.45, base * ratio1 * 0.5);
                if (v.subPhase >= 1.0)
                    v.subPhase -= 1.0;
                l += centre * 0.7071f;
                r += centre * 0.7071f;

                // Drive into the filter, then one or two stages of the mode.
                const auto filter = [&](float x, size_t ch)
                {
                    x = std::tanh(x * driveGain) * driveNorm;
                    float low, band, high;
                    v.stage1[ch].run(x, v.g, v.k, low, band, high);
                    switch (mode)
                    {
                        case 0:
                            return low;
                        case 1:
                        {
                            float low2, band2, high2;
                            v.stage2[ch].run(low, v.g, v.k, low2, band2, high2);
                            return low2;
                        }
                        case 2:
                            return band;
                        default:
                            return high;
                    }
                };
                l = filter(l, 0);
                r = filter(r, 1);

                v.filter.step(filA, filD, filS, filR);
                const float env = v.amp.step(ampA, ampD, ampS, ampR);
                if (!v.active())
                {
                    v.note = -1;
                    continue;
                }
                const float velocityScale = 1.0f - velocityAmount + velocityAmount * v.velocity;
                const float tremolo = 1.0f - lfoToAmp * 0.5f * (1.0f - lfo_);
                const float gain = env * velocityScale * tremolo * 0.35f;
                outL += l * gain;
                outR += r * gain;
            }

            // Shimmer: two delay lines, modulated in opposite directions.
            if (shimmerMix > 0.001f && !chorus_[0].empty())
            {
                const auto length = static_cast<int>(chorus_[0].size());
                chorus_[0][static_cast<size_t>(chorusWrite_)] = outL;
                chorus_[1][static_cast<size_t>(chorusWrite_)] = outR;
                chorusPhase_ += 0.37 / sampleRate_;
                if (chorusPhase_ >= 1.0)
                    chorusPhase_ -= 1.0;
                const double m = std::sin(twoPi * chorusPhase_);
                float wet[2];
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double ms = 14.0 + (ch == 0 ? m : -m) * 6.0;
                    double read = chorusWrite_ - ms * 0.001 * sampleRate_;
                    while (read < 0.0)
                        read += length;
                    const int i0 = std::clamp(static_cast<int>(read), 0, length - 1);
                    const int i1 = (i0 + 1) % length;
                    const float frac = static_cast<float>(read - std::floor(read));
                    const auto& line = chorus_[static_cast<size_t>(ch)];
                    wet[ch] = line[static_cast<size_t>(i0)] +
                              (line[static_cast<size_t>(i1)] - line[static_cast<size_t>(i0)]) * frac;
                }
                // The wet sides cross over a little: the shimmer widens.
                outL = outL * (1.0f - 0.5f * shimmerMix) + (wet[0] * 0.8f + wet[1] * 0.2f) * shimmerMix;
                outR = outR * (1.0f - 0.5f * shimmerMix) + (wet[1] * 0.8f + wet[0] * 0.2f) * shimmerMix;
                if (++chorusWrite_ >= length)
                    chorusWrite_ = 0;
            }
            left[i] = outL * outGain;
            right[i] = outR * outGain;
        }
        while (events != nullptr && next < events->size() && inChunk((*events)[next], numSamples))
            handle((*events)[next++]);
        for (const auto& v : voices_)
            if (v.active())
                ++active;
        // For the editor: how many voices sound (the frame's "reduction" slot)
        // and the LFO, 0..1 (its extra).
        blockReduction_ = static_cast<float>(active);
        blockExtra_ = 0.5f * (lfo_ + 1.0f) + 1.0e-3f;
    }

    std::array<Voice, voiceCount> voices_{};
    std::uint32_t age_ = 0;
    dsp::Lcg rng_{0x2545F491u};
    synth::Expression expression_;
    double expressionRatio_ = 1.0;
    double lfoPhase_ = 0.0;
    float lfoHeld_ = 0.0f, lfo_ = 0.0f;
    int controlCountdown_ = 0;
    double lastFrequency_ = 0.0;
    std::array<std::vector<float>, 2> chorus_;
    int chorusWrite_ = 0;
    double chorusPhase_ = 0.0;
};
} // namespace

float shapeAt(float shape, float phase) noexcept
{
    return morph(shape, static_cast<double>(phase), 0.0);
}

std::unique_ptr<engine::InsertProcessor> create()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("aurora::create");
    return std::make_unique<Aurora>();
}

std::vector<engine::ParameterInfo> parameterInfo()
{
    using P = engine::ParameterInfo;
    std::vector<P> p(count);
    p[osc1Shape] = {"Osc 1 shape", "", 0.0f, 3.0f, 2.0f, false, {}};
    p[osc1Octave] = {"Osc 1 octave", "", -2.0f, 2.0f, 0.0f, false, {}};
    p[osc1Level] = {"Osc 1 level", "", 0.0f, 1.0f, 0.8f, false, {}};
    p[osc2Shape] = {"Osc 2 shape", "", 0.0f, 3.0f, 3.0f, false, {}};
    p[osc2Octave] = {"Osc 2 octave", "", -2.0f, 2.0f, 0.0f, false, {}};
    p[osc2Semi] = {"Osc 2 semitones", "", -12.0f, 12.0f, 7.0f, false, {}};
    p[osc2Fine] = {"Osc 2 fine", "ct", -50.0f, 50.0f, 6.0f, false, {}};
    p[osc2Level] = {"Osc 2 level", "", 0.0f, 1.0f, 0.4f, false, {}};
    p[unison] = {"Unison", "", 1.0f, 7.0f, 3.0f, false, {}};
    p[spread] = {"Spread", "ct", 0.0f, 50.0f, 14.0f, false, {}};
    p[width] = {"Width", "%", 0.0f, 100.0f, 70.0f, false, {}};
    p[sub] = {"Sub", "", 0.0f, 1.0f, 0.2f, false, {}};
    p[noise] = {"Noise", "", 0.0f, 1.0f, 0.0f, false, {}};
    p[filterMode] = {"Filter mode", "", 0.0f, 3.0f, 1.0f, false, {"LP12", "LP24", "BP", "HP"}};
    p[cutoff] = {"Cutoff", "Hz", 20.0f, 20000.0f, 2400.0f, true, {}};
    p[resonance] = {"Resonance", "", 0.0f, 1.0f, 0.25f, false, {}};
    p[drive] = {"Drive", "", 0.0f, 1.0f, 0.15f, false, {}};
    p[filterEnv] = {"Filter envelope", "", -1.0f, 1.0f, 0.35f, false, {}};
    p[keyTrack] = {"Key track", "", 0.0f, 1.0f, 0.5f, false, {}};
    p[ampAttack] = {"Attack", "ms", 1.0f, 5000.0f, 8.0f, true, {}};
    p[ampDecay] = {"Decay", "ms", 1.0f, 8000.0f, 600.0f, true, {}};
    p[ampSustain] = {"Sustain", "", 0.0f, 1.0f, 0.7f, false, {}};
    p[ampRelease] = {"Release", "ms", 1.0f, 10000.0f, 450.0f, true, {}};
    p[filterAttack] = {"Filter attack", "ms", 1.0f, 5000.0f, 4.0f, true, {}};
    p[filterDecay] = {"Filter decay", "ms", 1.0f, 8000.0f, 420.0f, true, {}};
    p[filterSustain] = {"Filter sustain", "", 0.0f, 1.0f, 0.3f, false, {}};
    p[filterRelease] = {"Filter release", "ms", 1.0f, 10000.0f, 500.0f, true, {}};
    p[lfoShape] = {"LFO shape", "", 0.0f, 4.0f, 0.0f, false, {"Sine", "Tri", "Saw", "Square", "Random"}};
    p[lfoRate] = {"LFO rate", "Hz", 0.05f, 20.0f, 0.6f, true, {}};
    p[lfoPitch] = {"LFO pitch", "ct", 0.0f, 100.0f, 0.0f, false, {}};
    p[lfoCutoff] = {"LFO cutoff", "oct", 0.0f, 4.0f, 0.3f, false, {}};
    p[lfoAmp] = {"LFO level", "", 0.0f, 1.0f, 0.0f, false, {}};
    p[glide] = {"Glide", "ms", 0.0f, 2000.0f, 0.0f, false, {}};
    p[shimmer] = {"Shimmer", "", 0.0f, 1.0f, 0.35f, false, {}};
    p[velocity] = {"Velocity", "", 0.0f, 1.0f, 0.6f, false, {}};
    p[level] = {"Level", "dB", -60.0f, 6.0f, -6.0f, false, {}};
    return p;
}

const std::vector<Preset>& presets()
{
    static const std::vector<Preset> list = []
    {
        std::vector<float> base;
        for (const auto& info : parameterInfo())
            base.push_back(info.defaultValue);
        std::vector<Preset> made;
        const auto preset = [&](const char* name, std::initializer_list<std::pair<Param, float>> values)
        {
            Preset p{name, base};
            for (const auto& [param, value] : values)
                p.values[static_cast<size_t>(param)] = value;
            made.push_back(std::move(p));
        };
        preset("Northern Lights", {});
        preset("Solar Wind",
               {{osc1Shape, 2.0f},
                {osc2Shape, 2.0f},
                {osc2Semi, 0.0f},
                {osc2Fine, -9.0f},
                {osc2Level, 0.7f},
                {unison, 7.0f},
                {spread, 28.0f},
                {width, 100.0f},
                {cutoff, 5200.0f},
                {resonance, 0.15f},
                {filterEnv, 0.2f},
                {ampAttack, 4.0f},
                {ampSustain, 0.85f},
                {ampRelease, 300.0f},
                {shimmer, 0.25f},
                {glide, 40.0f},
                {level, -9.0f}});
        preset("Glacier Keys", {{osc1Shape, 1.0f},     {osc2Shape, 0.0f},      {osc2Octave, 1.0f},
                                {osc2Semi, 0.0f},      {osc2Fine, 3.0f},       {osc2Level, 0.35f},
                                {unison, 2.0f},        {spread, 6.0f},         {width, 40.0f},
                                {sub, 0.0f},           {cutoff, 3200.0f},      {filterEnv, 0.45f},
                                {filterDecay, 700.0f}, {filterSustain, 0.15f}, {ampAttack, 2.0f},
                                {ampDecay, 1600.0f},   {ampSustain, 0.25f},    {ampRelease, 700.0f},
                                {shimmer, 0.5f},       {velocity, 0.8f}});
        preset("Polar Bass",
               {{osc1Shape, 2.4f}, {osc1Octave, -1.0f}, {osc2Shape, 3.0f},     {osc2Octave, -1.0f},
                {osc2Semi, 0.0f},  {osc2Fine, 4.0f},    {osc2Level, 0.5f},     {unison, 1.0f},
                {sub, 0.8f},       {filterMode, 1.0f},  {cutoff, 380.0f},      {resonance, 0.35f},
                {drive, 0.5f},     {filterEnv, 0.55f},  {filterDecay, 260.0f}, {filterSustain, 0.1f},
                {keyTrack, 0.3f},  {ampAttack, 2.0f},   {ampSustain, 1.0f},    {ampRelease, 120.0f},
                {shimmer, 0.0f},   {glide, 60.0f},      {level, -4.0f}});
        preset("Ice Pluck",
               {{osc1Shape, 2.6f},
                {osc2Shape, 1.0f},
                {osc2Octave, 1.0f},
                {osc2Semi, 0.0f},
                {osc2Level, 0.3f},
                {unison, 3.0f},
                {spread, 9.0f},
                {cutoff, 900.0f},
                {resonance, 0.4f},
                {filterEnv, 0.8f},
                {filterAttack, 1.0f},
                {filterDecay, 180.0f},
                {filterSustain, 0.0f},
                {ampAttack, 1.0f},
                {ampDecay, 380.0f},
                {ampSustain, 0.0f},
                {ampRelease, 300.0f},
                {shimmer, 0.6f}});
        preset("Starfield",
               {{osc1Shape, 0.4f},    {osc2Shape, 1.2f},  {osc2Octave, 1.0f},    {osc2Semi, 7.0f},
                {osc2Level, 0.5f},    {unison, 5.0f},     {spread, 18.0f},       {width, 100.0f},
                {noise, 0.18f},       {filterMode, 2.0f}, {cutoff, 1800.0f},     {resonance, 0.55f},
                {filterEnv, 0.0f},    {lfoShape, 4.0f},   {lfoRate, 3.5f},       {lfoCutoff, 1.4f},
                {ampAttack, 1400.0f}, {ampSustain, 1.0f}, {ampRelease, 3200.0f}, {shimmer, 0.85f},
                {level, -8.0f}});
        preset("Comet Lead", {{osc1Shape, 3.0f},  {osc2Shape, 2.0f},  {osc2Semi, 12.0f},    {osc2Fine, 0.0f},
                              {osc2Level, 0.45f}, {unison, 2.0f},     {spread, 8.0f},       {width, 30.0f},
                              {cutoff, 2600.0f},  {resonance, 0.45f}, {drive, 0.45f},       {filterEnv, 0.3f},
                              {lfoShape, 0.0f},   {lfoRate, 5.5f},    {lfoPitch, 18.0f},    {lfoCutoff, 0.0f},
                              {ampAttack, 6.0f},  {ampSustain, 0.9f}, {ampRelease, 220.0f}, {glide, 90.0f},
                              {shimmer, 0.2f}});
        preset("Aurora Drift",
               {{osc1Shape, 1.6f},
                {osc2Shape, 0.8f},
                {osc2Semi, 5.0f},
                {osc2Level, 0.55f},
                {unison, 4.0f},
                {spread, 22.0f},
                {width, 90.0f},
                {filterMode, 0.0f},
                {cutoff, 1500.0f},
                {resonance, 0.3f},
                {filterEnv, 0.15f},
                {lfoShape, 1.0f},
                {lfoRate, 0.12f},
                {lfoCutoff, 1.8f},
                {lfoAmp, 0.15f},
                {ampAttack, 900.0f},
                {ampSustain, 0.9f},
                {ampRelease, 2400.0f},
                {shimmer, 0.7f}});
        return made;
    }();
    return list;
}

} // namespace northlight::plugins::aurora
