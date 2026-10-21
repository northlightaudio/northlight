#include "plugins/Nebula.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/SynthVoices.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace aural::plugins::nebula
{

namespace
{
constexpr int haloPartials = maxPartials / 2;
constexpr int slots = maxPartials + haloPartials; // main partials, then the halo
constexpr int controlInterval = 32;
constexpr int tableSize = 4096;

// A sine table, read with linear interpolation (the partials are many).
const std::array<float, tableSize + 1>& sineTable()
{
    static const auto table = []
    {
        std::array<float, tableSize + 1> t{};
        for (int i = 0; i <= tableSize; ++i)
            t[static_cast<size_t>(i)] =
                static_cast<float>(std::sin(2.0 * std::numbers::pi * static_cast<double>(i) / tableSize));
        return t;
    }();
    return table;
}

inline float sineAt(const std::array<float, tableSize + 1>& table, double phase) noexcept
{
    const double x = phase * tableSize;
    const int i = static_cast<int>(x);
    const float f = static_cast<float>(x - i);
    return table[static_cast<size_t>(i)] +
           (table[static_cast<size_t>(i) + 1] - table[static_cast<size_t>(i)]) * f;
}

// The spectrum's shape: a partial's level from its number, its frequency and
// the parameters (the same for the voices and the editor's picture).
float partialLevel(const float* p, int k, float hz, float motion) noexcept
{
    // Brightness tilts the spectrum from steep (dark) to nearly flat (bright);
    // the motion sweeps it.
    const float bright = std::clamp(p[brightness] + motion * p[motionDepth] * 0.35f, 0.0f, 1.0f);
    const float slope = 2.4f - 2.1f * bright;
    float level = std::pow(static_cast<float>(k), -slope);
    // Odd/even: negative empties the even partials (hollow), positive lifts them.
    if (k % 2 == 0)
        level *= std::clamp(1.0f + p[oddEven], 0.0f, 2.0f);
    // The formant: a bump around a vowel's frequency, a third of the way up
    // and down in octaves.
    const float centre =
        250.0f * std::pow(12.0f, std::clamp(p[vowel] + motion * p[motionDepth] * 0.08f, 0.0f, 1.0f));
    const float octaves = std::log2(std::max(1.0f, hz) / centre);
    const float bump = std::exp(-octaves * octaves / 0.35f);
    level *= (1.0f - p[formant]) + p[formant] * (0.15f + 2.2f * bump);
    return level;
}

inline float ratioOf(const float* p, int k) noexcept
{
    // Stretch pulls the partials sharp, the further up the more (a bell's spectrum).
    return static_cast<float>(k) * std::pow(static_cast<float>(k), p[stretch] * 0.12f);
}

struct Rng
{
    std::uint32_t state = 0x7F4A7C15u;
    float next() noexcept // -1..1
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<double>(state >> 8) / 8388608.0 - 1.0);
    }
};

using synth::Envelope;
using synth::Stage;

struct Voice
{
    int note = -1;
    float velocity = 1.0f;
    std::uint32_t age = 0;
    Envelope level; // the voice's level
    double hz = 220.0;
    std::array<std::array<double, slots>, 2> phase{}; // two sines per partial
    std::array<double, slots> increment1{}, increment2{};
    std::array<float, slots> amp{}, ampStep{};
    std::array<float, slots> driftNow{}, driftTarget{};
    double subPhase = 0.0;
    float breathLow = 0.0f, breathBand = 0.0f;
    bool active() const noexcept { return level.active(); }
};

// Space: an eight-line feedback delay network with damping.
class Space
{
public:
    void prepare(double sampleRate)
    {
        rate_ = sampleRate;
        for (size_t i = 0; i < lines_.size(); ++i)
        {
            const auto length = static_cast<size_t>(baseLengths[i] * 1.6 * sampleRate / 48000.0) + 4;
            lines_[i].assign(length, 0.0f);
            positions_[i] = 0;
        }
        filters_.fill(0.0f);
    }
    void set(float size) noexcept
    {
        const double seconds = 1.8 + size * 10.0;
        for (size_t i = 0; i < lines_.size(); ++i)
        {
            lengths_[i] = std::max<int>(
                8,
                std::min<int>(static_cast<int>(lines_[i].size()) - 2,
                              static_cast<int>(baseLengths[i] * (0.6 + size) * rate_ / 48000.0)));
            gains_[i] = static_cast<float>(std::pow(10.0, -3.0 * lengths_[i] / (rate_ * seconds)));
        }
    }
    void process(float inL, float inR, float& outL, float& outR) noexcept
    {
        std::array<float, 8> taps{};
        for (size_t i = 0; i < 8; ++i)
        {
            const auto& line = lines_[i];
            int read = positions_[i] - lengths_[i];
            if (read < 0)
                read += static_cast<int>(line.size());
            // A gentle low-pass in each line: the tail darkens as it decays.
            filters_[i] += 0.55f * (line[static_cast<size_t>(read)] - filters_[i]);
            taps[i] = filters_[i] * gains_[i];
        }
        // Householder: every line feeds every other.
        float sum = 0.0f;
        for (float t : taps)
            sum += t;
        sum *= 0.25f;
        outL = outR = 0.0f;
        for (size_t i = 0; i < 8; ++i)
        {
            const float in = (i % 2 == 0 ? inL : inR) * 0.35f;
            auto& line = lines_[i];
            line[static_cast<size_t>(positions_[i])] = in + taps[i] - sum;
            if (++positions_[i] >= static_cast<int>(line.size()))
                positions_[i] = 0;
            (i % 2 == 0 ? outL : outR) += taps[i];
        }
        outL *= 0.5f;
        outR *= 0.5f;
    }

private:
    static constexpr std::array<double, 8> baseLengths{1031, 1327, 1523, 1871, 2053, 2311, 2647, 2953};
    double rate_ = 48000.0;
    std::array<std::vector<float>, 8> lines_;
    std::array<int, 8> positions_{}, lengths_{};
    std::array<float, 8> gains_{}, filters_{};
};

class Nebula final : public MeteredBuiltIn
{
public:
    Nebula() : MeteredBuiltIn(type, nebula::parameterInfo(), true), table_(sineTable()) {}
    bool acceptsMidi() const noexcept override { return true; }

private:
    void prepareBuiltIn(double sampleRate, int) override
    {
        for (auto& v : voices_)
            v = Voice{};
        age_ = 0;
        motionPhase_ = 0.0;
        space_.prepare(sampleRate);
        toneL_ = toneR_ = 0.0f;
        countdown_ = 0;
        expression_.reset();
    }

    void noteOn(int note, int vel) noexcept
    {
        Voice* voice = &synth::allocateVoice(voices_.data(), static_cast<int>(voices_.size()), note);
        const bool fresh = !voice->active();
        voice->note = note;
        const float sensitivity = parameter(velocity);
        voice->velocity = 1.0f - sensitivity + sensitivity * static_cast<float>(vel) / 127.0f;
        voice->age = ++age_;
        voice->level.start();
        if (fresh)
        {
            voice->level.value = 0.0f;
            // Random phases and drifts: no two notes start alike.
            for (auto& side : voice->phase)
                for (auto& ph : side)
                    ph = 0.5 * (rng_.next() + 1.0f);
            for (int i = 0; i < slots; ++i)
            {
                voice->driftNow[static_cast<size_t>(i)] = rng_.next();
                voice->driftTarget[static_cast<size_t>(i)] = rng_.next();
                voice->amp[static_cast<size_t>(i)] = 0.0f;
            }
        }
    }

    void noteOff(int note) noexcept
    {
        for (auto& v : voices_)
            if (v.note == note && v.active())
                v.level.release();
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
                    v.level.release();
            });
    }

    // Once per control interval: each voice's partial frequencies and levels.
    void control(const float* p) noexcept
    {
        const double sr = sampleRate_;
        motion_ = static_cast<float>(std::sin(2.0 * std::numbers::pi * motionPhase_));
        const int partialCount = std::clamp(static_cast<int>(std::lround(p[partials])), 1, maxPartials);
        const float spreadCents = p[spread];
        const float driftCents = p[drift] * 18.0f;
        const float haloLevel = p[halo];
        // The pitch bend and the mod wheel's vibrato ride on the octave.
        const double octaveRatio = std::pow(2.0, std::round(p[octave])) *
                                   synth::Expression::ratio(expression_.advance(controlInterval / sr));
        for (auto& v : voices_)
        {
            if (!v.active())
                continue;
            v.hz = 440.0 * std::pow(2.0, (v.note - 69) / 12.0) * octaveRatio;
            float total = 0.0f;
            std::array<float, slots> target{};
            for (int s = 0; s < slots; ++s)
            {
                const bool isHalo = s >= maxPartials;
                const int k = isHalo ? s - maxPartials + 1 : s + 1;
                if ((!isHalo && k > partialCount) || (isHalo && k > (partialCount + 1) / 2))
                    continue;
                const float ratio = ratioOf(p, k) * (isHalo ? 2.0f : 1.0f);
                const double partialHz = v.hz * ratio;
                if (partialHz > sr * 0.45)
                    continue;
                // The drift wanders towards a new target now and then.
                auto& now = v.driftNow[static_cast<size_t>(s)];
                auto& to = v.driftTarget[static_cast<size_t>(s)];
                now += (to - now) * 0.004f;
                if (std::abs(to - now) < 0.05f)
                    to = rng_.next();
                const double cents = now * driftCents;
                const double a = std::pow(2.0, (cents + spreadCents * 0.5) / 1200.0);
                const double b = std::pow(2.0, (cents - spreadCents * 0.5) / 1200.0);
                v.increment1[static_cast<size_t>(s)] = partialHz * a / sr;
                v.increment2[static_cast<size_t>(s)] = partialHz * b / sr;
                float level = partialLevel(p, k, static_cast<float>(partialHz), motion_);
                if (isHalo)
                    level *= haloLevel * 0.6f;
                target[static_cast<size_t>(s)] = level;
                total += level;
            }
            const float norm = total > 0.0f ? 0.5f / std::sqrt(total) : 0.0f;
            for (int s = 0; s < slots; ++s)
            {
                const float t = target[static_cast<size_t>(s)] * norm;
                v.ampStep[static_cast<size_t>(s)] = (t - v.amp[static_cast<size_t>(s)]) / controlInterval;
            }
        }
        // Each partial's pan: alternate sides, wider the higher it is, swaying with the motion.
        const float w = p[width] * 0.01f;
        for (int s = 0; s < slots; ++s)
        {
            const float side = s % 2 == 0 ? -1.0f : 1.0f;
            const float pos =
                std::clamp(side * w * (0.35f + 0.65f * static_cast<float>(s % maxPartials) / maxPartials) +
                               motion_ * p[motionDepth] * 0.15f * side,
                           -1.0f,
                           1.0f);
            panL_[static_cast<size_t>(s)] = std::sqrt(0.5f * (1.0f - pos));
            panR_[static_cast<size_t>(s)] = std::sqrt(0.5f * (1.0f + pos));
        }
        space_.set(p[space]);
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        std::array<float, count> p{};
        for (int i = 0; i < count; ++i)
            p[static_cast<size_t>(i)] = parameter(i);
        const float sr = static_cast<float>(sampleRate_);
        const float attackStep = 1.0f / std::max(1.0f, p[attack] * 0.001f * sr);
        const auto coefficient = [sr](float ms)
        { return std::exp(std::log(0.001f) / std::max(1.0f, ms * 0.001f * sr)); };
        const float decayCoef = coefficient(p[decay]), releaseCoef = coefficient(p[release]);
        const float sustainLevel = p[sustain];
        const float subLevel = p[sub] * 0.4f, breathLevel = p[breath] * 0.25f;
        const double motionStep = p[motionRate] / sampleRate_;
        const float toneCoef =
            1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * std::min(p[tone], sr * 0.45f) / sr);
        const float mix = p[spaceMix];
        const float outGain = dbToGain(p[level]);
        const int partialCount = std::clamp(static_cast<int>(std::lround(p[partials])), 1, maxPartials);
        const int used = maxPartials + (partialCount + 1) / 2;
        const auto& table = table_;

        size_t next = firstChunkEvent();
        const engine::MidiEventList* events = midi_;
        for (int i = 0; i < numSamples; ++i)
        {
            while (events != nullptr && next < events->size() &&
                   std::clamp((*events)[next].sampleOffset - chunkStart_, 0, numSamples) <= i)
                handle((*events)[next++]);
            if (countdown_ <= 0)
            {
                control(p.data());
                countdown_ = controlInterval;
            }
            --countdown_;
            motionPhase_ += motionStep;
            if (motionPhase_ >= 1.0)
                motionPhase_ -= 1.0;

            float l = 0.0f, r = 0.0f;
            for (auto& v : voices_)
            {
                if (!v.active())
                    continue;
                const float env = v.level.step(attackStep, decayCoef, sustainLevel, releaseCoef);
                if (!v.level.active())
                {
                    v.note = -1;
                    continue;
                }
                float vl = 0.0f, vr = 0.0f;
                for (int s = 0; s < used; ++s)
                {
                    const auto su = static_cast<size_t>(s);
                    if (s >= partialCount && s < maxPartials)
                    {
                        s = maxPartials - 1; // skip to the halo
                        continue;
                    }
                    v.amp[su] += v.ampStep[su];
                    const float a = v.amp[su];
                    if (a <= 1.0e-6f)
                        continue;
                    auto& p1 = v.phase[0][su];
                    auto& p2 = v.phase[1][su];
                    const float x = (sineAt(table, p1) + sineAt(table, p2)) * a;
                    p1 += v.increment1[su];
                    if (p1 >= 1.0)
                        p1 -= 1.0;
                    p2 += v.increment2[su];
                    if (p2 >= 1.0)
                        p2 -= 1.0;
                    vl += x * panL_[su];
                    vr += x * panR_[su];
                }
                // The sub under it, and breath: noise in a band around the note.
                v.subPhase += v.hz * 0.5 / sampleRate_;
                if (v.subPhase >= 1.0)
                    v.subPhase -= 1.0;
                const float subSample = sineAt(table, v.subPhase) * subLevel;
                const float f =
                    std::min(1.2f,
                             2.0f * std::sin(std::numbers::pi_v<float> *
                                             std::min(static_cast<float>(v.hz) * 3.0f, sr * 0.2f) / sr));
                const float noise = rng_.next();
                v.breathLow += f * v.breathBand;
                const float high = noise - v.breathLow - 1.2f * v.breathBand;
                v.breathBand += f * high;
                const float centre = subSample + v.breathBand * breathLevel;
                const float gain = env * v.velocity;
                l += (vl + centre * 0.7071f) * gain;
                r += (vr + centre * 0.7071f) * gain;
            }
            toneL_ += toneCoef * (l - toneL_);
            toneR_ += toneCoef * (r - toneR_);
            float wetL = 0.0f, wetR = 0.0f;
            space_.process(toneL_, toneR_, wetL, wetR);
            left[i] = (toneL_ * (1.0f - 0.5f * mix) + wetL * mix * 1.6f) * outGain;
            right[i] = (toneR_ * (1.0f - 0.5f * mix) + wetR * mix * 1.6f) * outGain;
        }
        while (events != nullptr && next < events->size() && inChunk((*events)[next], numSamples))
            handle((*events)[next++]);
        int active = 0;
        for (const auto& v : voices_)
            if (v.active())
                ++active;
        blockReduction_ = static_cast<float>(active);
        blockExtra_ = 0.5f * (motion_ + 1.0f) + 1.0e-3f;
    }

    const std::array<float, tableSize + 1>& table_;
    std::array<Voice, voiceCount> voices_{};
    std::array<float, slots> panL_{}, panR_{};
    std::uint32_t age_ = 0;
    Rng rng_;
    double motionPhase_ = 0.0;
    float motion_ = 0.0f;
    int countdown_ = 0;
    synth::Expression expression_;
    Space space_;
    float toneL_ = 0.0f, toneR_ = 0.0f;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    AURAL_ASSERT_NOT_REALTIME("nebula::create");
    return std::make_unique<Nebula>();
}

std::vector<engine::ParameterInfo> parameterInfo()
{
    std::vector<engine::ParameterInfo> p(count);
    p[partials] = {"Partials", "", 4.0f, 24.0f, 14.0f, false, {}};
    p[brightness] = {"Brightness", "", 0.0f, 1.0f, 0.45f, false, {}};
    p[oddEven] = {"Odd/even", "", -1.0f, 1.0f, 0.0f, false, {}};
    p[stretch] = {"Stretch", "", 0.0f, 1.0f, 0.0f, false, {}};
    p[vowel] = {"Vowel", "", 0.0f, 1.0f, 0.4f, false, {}};
    p[formant] = {"Formant", "", 0.0f, 1.0f, 0.35f, false, {}};
    p[drift] = {"Drift", "", 0.0f, 1.0f, 0.5f, false, {}};
    p[spread] = {"Spread", "ct", 0.0f, 30.0f, 9.0f, false, {}};
    p[width] = {"Width", "%", 0.0f, 100.0f, 80.0f, false, {}};
    p[halo] = {"Halo", "", 0.0f, 1.0f, 0.3f, false, {}};
    p[sub] = {"Sub", "", 0.0f, 1.0f, 0.15f, false, {}};
    p[breath] = {"Breath", "", 0.0f, 1.0f, 0.1f, false, {}};
    p[attack] = {"Attack", "ms", 1.0f, 10000.0f, 1200.0f, true, {}};
    p[decay] = {"Decay", "ms", 1.0f, 10000.0f, 2000.0f, true, {}};
    p[sustain] = {"Sustain", "", 0.0f, 1.0f, 0.85f, false, {}};
    p[release] = {"Release", "ms", 1.0f, 15000.0f, 3500.0f, true, {}};
    p[motionRate] = {"Motion rate", "Hz", 0.02f, 8.0f, 0.15f, true, {}};
    p[motionDepth] = {"Motion", "", 0.0f, 1.0f, 0.4f, false, {}};
    p[space] = {"Space", "", 0.0f, 1.0f, 0.6f, false, {}};
    p[spaceMix] = {"Space mix", "", 0.0f, 1.0f, 0.45f, false, {}};
    p[tone] = {"Tone", "Hz", 400.0f, 20000.0f, 9000.0f, true, {}};
    p[octave] = {"Octave", "", -2.0f, 2.0f, 0.0f, false, {}};
    p[velocity] = {"Velocity", "", 0.0f, 1.0f, 0.4f, false, {}};
    p[level] = {"Level", "dB", -60.0f, 6.0f, -4.0f, false, {}};
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
        preset("Nebula", {});
        preset("Choir of Dust",
               {{partials, 18.0f},
                {brightness, 0.35f},
                {vowel, 0.28f},
                {formant, 0.8f},
                {breath, 0.3f},
                {drift, 0.7f},
                {spread, 12.0f},
                {halo, 0.15f},
                {attack, 1800.0f},
                {release, 4500.0f},
                {motionDepth, 0.55f},
                {motionRate, 0.09f}});
        preset("Glass Halo",
               {{partials, 10.0f},
                {brightness, 0.7f},
                {stretch, 0.45f},
                {oddEven, 0.4f},
                {formant, 0.1f},
                {halo, 0.85f},
                {sub, 0.0f},
                {drift, 0.3f},
                {spread, 5.0f},
                {attack, 600.0f},
                {sustain, 0.6f},
                {release, 6000.0f},
                {space, 0.85f},
                {spaceMix, 0.6f}});
        preset("Hollow Moon",
               {{partials, 16.0f},
                {oddEven, -1.0f},
                {brightness, 0.4f},
                {formant, 0.2f},
                {sub, 0.35f},
                {drift, 0.4f},
                {attack, 2500.0f},
                {release, 5000.0f},
                {motionDepth, 0.25f},
                {tone, 5000.0f}});
        preset("Solar Breath",
               {{partials, 22.0f},
                {brightness, 0.75f},
                {vowel, 0.6f},
                {formant, 0.5f},
                {breath, 0.6f},
                {halo, 0.5f},
                {spread, 16.0f},
                {width, 100.0f},
                {attack, 900.0f},
                {motionRate, 0.3f},
                {motionDepth, 0.7f},
                {spaceMix, 0.55f}});
        preset("Deep Field",
               {{partials, 8.0f},
                {brightness, 0.2f},
                {octave, -1.0f},
                {sub, 0.5f},
                {formant, 0.0f},
                {drift, 0.6f},
                {spread, 20.0f},
                {attack, 3500.0f},
                {release, 8000.0f},
                {space, 1.0f},
                {spaceMix, 0.7f},
                {tone, 3200.0f},
                {level, -2.0f}});
        preset("Ice Bells",
               {{partials, 12.0f},
                {brightness, 0.85f},
                {stretch, 0.8f},
                {formant, 0.0f},
                {halo, 0.4f},
                {sub, 0.0f},
                {breath, 0.0f},
                {drift, 0.2f},
                {spread, 3.0f},
                {attack, 5.0f},
                {decay, 3500.0f},
                {sustain, 0.0f},
                {release, 4000.0f},
                {velocity, 0.8f},
                {spaceMix, 0.5f}});
        preset("Drifting Veil",
               {{partials, 20.0f},
                {brightness, 0.5f},
                {vowel, 0.15f},
                {formant, 0.6f},
                {drift, 1.0f},
                {spread, 25.0f},
                {width, 100.0f},
                {halo, 0.35f},
                {attack, 4000.0f},
                {release, 9000.0f},
                {motionRate, 0.05f},
                {motionDepth, 0.9f},
                {space, 0.9f},
                {spaceMix, 0.65f}});
        return made;
    }();
    return list;
}

std::vector<Partial> spectrum(const float* params, float noteHz, float motion)
{
    std::vector<Partial> out;
    const int partialCount = std::clamp(static_cast<int>(std::lround(params[partials])), 1, maxPartials);
    float top = 1.0e-6f;
    for (int k = 1; k <= partialCount; ++k)
    {
        const float ratio = ratioOf(params, k);
        out.push_back({ratio, partialLevel(params, k, noteHz * ratio, motion), false});
    }
    for (int k = 1; k <= (partialCount + 1) / 2; ++k)
    {
        const float ratio = ratioOf(params, k) * 2.0f;
        out.push_back({ratio, partialLevel(params, k, noteHz * ratio, motion) * params[halo] * 0.6f, true});
    }
    for (const auto& p : out)
        top = std::max(top, p.level);
    for (auto& p : out)
        p.level /= top;
    return out;
}

} // namespace aural::plugins::nebula
