#include "plugins/Starbit.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/SynthVoices.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace aural::plugins::starbit
{

namespace
{
constexpr int controlInterval = 32;
constexpr std::array<float, 4> duties{0.125f, 0.25f, 0.5f, 0.75f};

// The arpeggio shapes, as semitone offsets from the note: a fixed table, so
// the audio thread reads it without anything being built (or allocated) there.
struct Shape
{
    int count;
    std::array<int, 4> steps;
};
constexpr std::array<Shape, 8> shapes{{
    {0, {}},
    {0, {}},
    {2, {0, 12, 0, 0}},
    {3, {0, 4, 7, 0}},
    {3, {0, 3, 7, 0}},
    {3, {0, 7, 12, 0}},
    {4, {0, 4, 7, 11}},
    {4, {0, 3, 7, 10}},
}};

// The default custom wave: a 4-bit wobble that sounds like a chip's wave channel.
constexpr std::array<int, waveSteps> defaultWave{8, 11, 13, 14, 15, 15, 14, 12, 10, 9, 9, 10, 12, 13, 13, 12,
                                                 9, 6,  4,  3,  3,  4,  6,  7,  7,  6, 4, 2,  1,  0,  1,  4};

using synth::Envelope;
using synth::Stage;

struct Voice
{
    int note = -1;
    float velocity = 1.0f;
    std::uint32_t age = 0;
    Envelope amp; // the voice's volume, counted linearly as a chip does
    double phase = 0.0, increment = 0.0;
    double currentSemis = 60.0; // where the pitch is (gliding to the note)
    double sinceOn = 0.0;       // seconds since the note began
    std::uint16_t lfsr = 1;
    double noiseClock = 0.0;
    float noiseOut = 1.0f;
    bool active() const noexcept { return amp.active(); }
};

class Starbit final : public MeteredBuiltIn
{
public:
    Starbit() : MeteredBuiltIn(type, starbit::parameterInfo(), true) {}
    bool acceptsMidi() const noexcept override { return true; }

private:
    void prepareBuiltIn(double sampleRate, int) override
    {
        for (auto& v : voices_)
            v = Voice{};
        held_.fill(-1);
        heldCount_ = 0;
        arpPhase_ = 0.0;
        arpStep_ = 0;
        holdL_ = holdR_ = 0.0f;
        holdPhase_ = 1.0;
        echo_.assign(static_cast<size_t>(sampleRate * 0.25) + 4, 0.0f);
        echoPos_ = 0;
        countdown_ = 0;
        age_ = 0;
    }

    bool mono() const noexcept
    {
        return parameter(voices) < 0.5f ||
               static_cast<int>(std::lround(parameter(arp))) == arpChord; // a chord arp plays one voice
    }

    // Starts (or, legato, moves) a voice to a note. A legato move keeps the
    // envelope; with glide it slides there, otherwise it sweeps in afresh.
    void startVoice(Voice& v, int note, int vel, bool legato) noexcept
    {
        const float sensitivity = parameter(velocity);
        v.velocity = 1.0f - sensitivity + sensitivity * static_cast<float>(vel) / 127.0f;
        v.note = note;
        v.age = ++age_;
        if (!(legato && parameter(glide) > 0.5f))
        {
            v.currentSemis = note + parameter(sweep);
            v.sinceOn = 0.0;
        }
        if (!legato)
        {
            v.amp.start();
            v.amp.value = 0.0f; // a chip retriggers its volume
        }
        else if (v.amp.stage == Stage::release || !v.amp.active())
            v.amp.start();
    }

    void noteOn(int note, int vel) noexcept
    {
        // The held notes, newest last (for mono priority and the chord arp).
        if (heldCount_ < static_cast<int>(held_.size()))
            held_[static_cast<size_t>(heldCount_++)] = note;
        lastVelocity_ = vel;
        if (mono())
        {
            auto& v = voices_[0];
            startVoice(v, note, vel, v.active() && v.amp.stage != Stage::release);
            return;
        }
        Voice* voice = &synth::allocateVoice(voices_.data(), polyVoices, note);
        voice->amp.stage = Stage::off; // a fresh start
        startVoice(*voice, note, vel, false);
    }

    void noteOff(int note) noexcept
    {
        int write = 0;
        for (int i = 0; i < heldCount_; ++i)
            if (held_[static_cast<size_t>(i)] != note)
                held_[static_cast<size_t>(write++)] = held_[static_cast<size_t>(i)];
        heldCount_ = write;
        if (mono())
        {
            auto& v = voices_[0];
            if (heldCount_ > 0)
            {
                // Back to the newest note still held (legato).
                if (v.note == note)
                    startVoice(v, held_[static_cast<size_t>(heldCount_ - 1)], lastVelocity_, true);
            }
            else if (v.active())
                v.amp.release();
            return;
        }
        for (int i = 0; i < polyVoices; ++i)
            if (voices_[static_cast<size_t>(i)].note == note && voices_[static_cast<size_t>(i)].active())
                voices_[static_cast<size_t>(i)].amp.release();
    }

    void handle(const engine::MidiEvent& e) noexcept
    {
        synth::dispatch(
            e,
            [this](int note, int velocity) { noteOn(note, velocity); },
            [this](int note) { noteOff(note); },
            [this]
            {
                heldCount_ = 0;
                for (auto& v : voices_)
                    v.amp.release();
            });
    }

    // Once per control interval: each voice's pitch (arp, vibrato, sweep, glide).
    void control(const float* p, double seconds) noexcept
    {
        const int shape = std::clamp(static_cast<int>(std::lround(p[arp])), 0, 7);
        arpPhase_ += seconds * p[arpRate];
        while (arpPhase_ >= 1.0)
        {
            arpPhase_ -= 1.0;
            ++arpStep_;
        }
        const auto& steps = shapes[static_cast<size_t>(shape)];
        const double transpose = std::round(p[octave]) * 12.0 + std::round(p[coarse]);
        const double glideMs = p[glide];
        const double sweepMs = std::max(1.0, static_cast<double>(p[sweepTime]));
        for (int i = 0; i < polyVoices; ++i)
        {
            auto& v = voices_[static_cast<size_t>(i)];
            if (!v.active())
                continue;
            v.sinceOn += seconds;
            int note = v.note;
            if (shape == arpChord && heldCount_ > 0)
                note = held_[static_cast<size_t>(arpStep_ % heldCount_)];
            const double offset =
                steps.count == 0 ? 0.0 : steps.steps[static_cast<size_t>(arpStep_ % steps.count)];
            // Towards the note: the sweep while it lasts, then the glide (or a jump).
            const double target = note;
            if (v.currentSemis != target)
            {
                double perSecond = 1.0e9;
                if (v.sinceOn * 1000.0 < sweepMs && p[sweep] != 0.0f)
                    perSecond = std::abs(p[sweep]) / (sweepMs * 0.001);
                else if (glideMs > 0.5)
                    perSecond = 12.0 / (glideMs * 0.001);
                const double step = perSecond * seconds;
                v.currentSemis = v.currentSemis < target ? std::min(target, v.currentSemis + step)
                                                         : std::max(target, v.currentSemis - step);
            }
            // Vibrato after its delay, fading in over a quarter second.
            double vibrato = 0.0;
            const double delay = p[vibratoDelay] * 0.001;
            if (v.sinceOn > delay)
            {
                const double fade = std::min(1.0, (v.sinceOn - delay) / 0.25);
                vibrato = p[vibratoDepth] * fade *
                          std::sin(2.0 * std::numbers::pi * p[vibratoRate] * (v.sinceOn - delay));
            }
            const double semis = v.currentSemis + offset + transpose + vibrato;
            const double hz = 440.0 * std::pow(2.0, (semis - 69.0) / 12.0);
            v.increment = std::min(0.45, hz / sampleRate_);
        }
        // The duty sweep: through the four duties at up to 30 steps a second.
        dutyPhase_ += seconds * p[dutySweep] * 30.0;
        if (dutyPhase_ >= 4.0)
            dutyPhase_ -= 4.0;
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        std::array<float, count> p{};
        for (int i = 0; i < count; ++i)
            p[static_cast<size_t>(i)] = parameter(i);
        const float sr = static_cast<float>(sampleRate_);
        const auto attackStep = 1.0f / std::max(1.0f, p[attack] * 0.001f * sr);
        const auto decayStep = 1.0f / std::max(1.0f, p[decay] * 0.001f * sr);
        const auto releaseStep = 1.0f / std::max(1.0f, p[release] * 0.001f * sr);
        const float sustainLevel = p[sustain];
        const int shape = std::clamp(static_cast<int>(std::lround(p[wave])), 0, 4);
        const int baseDuty = std::clamp(static_cast<int>(std::lround(p[duty])), 0, 3);
        const bool shortNoise = p[noiseMode] >= 0.5f;
        const float levels = std::exp2(std::clamp(std::round(p[bits]), 2.0f, 16.0f)) * 0.5f;
        const double holdStep = std::clamp(static_cast<double>(p[rate]), 1000.0, sampleRate_) / sampleRate_;
        const float echoMix = p[echo];
        const float outGain = dbToGain(p[level]) * 0.3f;
        std::array<int, waveSteps> table{};
        for (int s = 0; s < waveSteps; ++s)
            table[static_cast<size_t>(s)] =
                std::clamp(static_cast<int>(std::lround(p[firstWaveStep + s])), 0, 15);
        const int voiceLimit = mono() ? 1 : polyVoices;

        size_t next = firstChunkEvent();
        const engine::MidiEventList* events = midi_;
        for (int i = 0; i < numSamples; ++i)
        {
            while (events != nullptr && next < events->size() &&
                   std::clamp((*events)[next].sampleOffset - chunkStart_, 0, numSamples) <= i)
                handle((*events)[next++]);
            if (countdown_ <= 0)
            {
                control(p.data(), controlInterval / sampleRate_);
                countdown_ = controlInterval;
            }
            --countdown_;
            const float dutyNow = duties[static_cast<size_t>((baseDuty + static_cast<int>(dutyPhase_)) % 4)];

            float sum = 0.0f;
            for (int vi = 0; vi < voiceLimit; ++vi)
            {
                auto& v = voices_[static_cast<size_t>(vi)];
                if (!v.active())
                    continue;
                const float env = v.amp.stepLinear(attackStep, decayStep, sustainLevel, releaseStep);
                if (!v.amp.active())
                {
                    v.note = -1;
                    continue;
                }
                float x = 0.0f;
                const auto ph = static_cast<float>(v.phase);
                switch (shape)
                {
                    case pulse:
                        x = ph < dutyNow ? 1.0f : -1.0f;
                        break;
                    case triangle:
                    {
                        // 32 steps up and down, 4 bits.
                        const int step = static_cast<int>(ph * 32.0f);
                        const int levelStep = step < 16 ? step : 31 - step;
                        x = static_cast<float>(levelStep) / 7.5f - 1.0f;
                        break;
                    }
                    case saw:
                        x = static_cast<float>(static_cast<int>(ph * 16.0f)) / 7.5f - 1.0f;
                        break;
                    case noise:
                    {
                        // An LFSR clocked 16 times a cycle: long (15-bit) or short
                        // (the 93-step loop's metallic tone).
                        v.noiseClock += v.increment * 16.0;
                        while (v.noiseClock >= 1.0)
                        {
                            v.noiseClock -= 1.0;
                            const auto bit =
                                static_cast<std::uint16_t>((v.lfsr ^ (v.lfsr >> (shortNoise ? 6 : 1))) & 1u);
                            v.lfsr = static_cast<std::uint16_t>((v.lfsr >> 1) | (bit << 14));
                            v.noiseOut = (v.lfsr & 1u) != 0 ? 1.0f : -1.0f;
                        }
                        x = v.noiseOut;
                        break;
                    }
                    default:
                        x = static_cast<float>(
                                table[static_cast<size_t>(std::min(31, static_cast<int>(ph * 32.0f)))]) /
                                7.5f -
                            1.0f;
                        break;
                }
                v.phase += v.increment;
                if (v.phase >= 1.0)
                    v.phase -= 1.0;
                // The volume in 16 steps, as the chips had it.
                const float volume = std::round(env * v.velocity * 15.0f) / 15.0f;
                sum += x * volume;
            }
            // Lo-fi: hold each sample for the slower rate, then fewer bits.
            holdPhase_ += holdStep;
            if (holdPhase_ >= 1.0)
            {
                holdPhase_ -= 1.0;
                const float crushed = std::round(std::clamp(sum * 0.5f, -1.0f, 1.0f) * levels) / levels;
                holdL_ = holdR_ = crushed * 2.0f;
            }
            float l = holdL_, r = holdR_;
            // The echo: three sixteenths at 120 bpm, a little to the right.
            if (echoMix > 0.001f && !echo_.empty())
            {
                const auto length = static_cast<int>(echo_.size());
                const int delay = std::min(length - 1, static_cast<int>(sampleRate_ * 0.1875));
                int read = echoPos_ - delay;
                if (read < 0)
                    read += length;
                const float delayed = echo_[static_cast<size_t>(read)];
                echo_[static_cast<size_t>(echoPos_)] = l + delayed * 0.45f;
                if (++echoPos_ >= length)
                    echoPos_ = 0;
                l += delayed * echoMix * 0.6f;
                r += delayed * echoMix;
            }
            left[i] = l * outGain;
            right[i] = r * outGain;
        }
        while (events != nullptr && next < events->size() && inChunk((*events)[next], numSamples))
            handle((*events)[next++]);
        int active = 0;
        for (const auto& v : voices_)
            if (v.active())
                ++active;
        // For the editor: the voices sounding and the arpeggio's step (0..7, as 0..1).
        blockReduction_ = static_cast<float>(active);
        blockExtra_ = static_cast<float>(arpStep_ % 8) / 8.0f + 0.01f;
    }

    std::array<Voice, polyVoices> voices_{};
    std::array<int, 16> held_{};
    int heldCount_ = 0, lastVelocity_ = 100;
    std::uint32_t age_ = 0;
    double arpPhase_ = 0.0, dutyPhase_ = 0.0;
    int arpStep_ = 0;
    int countdown_ = 0;
    double holdPhase_ = 1.0;
    float holdL_ = 0.0f, holdR_ = 0.0f;
    std::vector<float> echo_;
    int echoPos_ = 0;
};
} // namespace

std::unique_ptr<engine::InsertProcessor> create()
{
    AURAL_ASSERT_NOT_REALTIME("starbit::create");
    return std::make_unique<Starbit>();
}

std::vector<engine::ParameterInfo> parameterInfo()
{
    std::vector<engine::ParameterInfo> p(count);
    p[wave] = {"Wave", "", 0.0f, 4.0f, 0.0f, false, {"Pulse", "Triangle", "Saw", "Noise", "Wave"}};
    p[duty] = {"Duty", "", 0.0f, 3.0f, 1.0f, false, {"12.5%", "25%", "50%", "75%"}};
    p[dutySweep] = {"Duty sweep", "", 0.0f, 1.0f, 0.0f, false, {}};
    p[noiseMode] = {"Noise mode", "", 0.0f, 1.0f, 0.0f, false, {"Long", "Short"}};
    p[octave] = {"Octave", "", -3.0f, 3.0f, 0.0f, false, {}};
    p[coarse] = {"Coarse", "st", -12.0f, 12.0f, 0.0f, false, {}};
    p[attack] = {"Attack", "ms", 0.0f, 2000.0f, 0.0f, false, {}};
    p[decay] = {"Decay", "ms", 1.0f, 3000.0f, 300.0f, true, {}};
    p[sustain] = {"Sustain", "", 0.0f, 1.0f, 0.6f, false, {}};
    p[release] = {"Release", "ms", 1.0f, 3000.0f, 60.0f, true, {}};
    p[arp] = {"Arpeggio",
              "",
              0.0f,
              7.0f,
              0.0f,
              false,
              {"Off", "Chord", "Octave", "Major", "Minor", "Power", "Maj7", "Min7"}};
    p[arpRate] = {"Arp rate", "Hz", 2.0f, 60.0f, 20.0f, false, {}};
    p[vibratoDepth] = {"Vibrato", "st", 0.0f, 1.0f, 0.0f, false, {}};
    p[vibratoRate] = {"Vibrato rate", "Hz", 0.5f, 15.0f, 6.0f, false, {}};
    p[vibratoDelay] = {"Vibrato delay", "ms", 0.0f, 1000.0f, 200.0f, false, {}};
    p[sweep] = {"Sweep", "st", -24.0f, 24.0f, 0.0f, false, {}};
    p[sweepTime] = {"Sweep time", "ms", 1.0f, 1000.0f, 80.0f, true, {}};
    p[glide] = {"Glide", "ms", 0.0f, 1000.0f, 0.0f, false, {}};
    p[voices] = {"Voices", "", 0.0f, 1.0f, 1.0f, false, {"Mono", "Poly"}};
    p[bits] = {"Bits", "", 2.0f, 16.0f, 8.0f, false, {}};
    p[rate] = {"Rate", "Hz", 2000.0f, 48000.0f, 24000.0f, true, {}};
    p[echo] = {"Echo", "", 0.0f, 1.0f, 0.0f, false, {}};
    p[velocity] = {"Velocity", "", 0.0f, 1.0f, 0.3f, false, {}};
    p[level] = {"Level", "dB", -60.0f, 6.0f, -6.0f, false, {}};
    for (int s = 0; s < waveSteps; ++s)
        p[static_cast<size_t>(firstWaveStep + s)] = {"Wave step " + std::to_string(s + 1),
                                                     "",
                                                     0.0f,
                                                     15.0f,
                                                     static_cast<float>(defaultWave[static_cast<size_t>(s)]),
                                                     false,
                                                     {}};
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
        // Values as doubles: the wave and arpeggio enumerators go in as they are.
        const auto preset = [&](const char* name, std::initializer_list<std::pair<int, double>> values)
        {
            Preset p{name, base};
            for (const auto& [param, value] : values)
                p.values[static_cast<size_t>(param)] = static_cast<float>(value);
            made.push_back(std::move(p));
        };
        preset("Starbit", {});
        preset("Hero Lead",
               {{wave, pulse},
                {duty, 2.0f},
                {voices, 0.0f},
                {glide, 40.0f},
                {vibratoDepth, 0.35f},
                {vibratoDelay, 250.0f},
                {decay, 600.0f},
                {sustain, 0.75f},
                {echo, 0.3f}});
        preset("Chord Arp",
               {{wave, pulse},
                {duty, 1.0f},
                {arp, arpChord},
                {arpRate, 24.0f},
                {sustain, 0.7f},
                {release, 80.0f}});
        preset("Major Sparkle",
               {{wave, pulse},
                {duty, 0.0f},
                {arp, arpMajor},
                {arpRate, 30.0f},
                {decay, 400.0f},
                {sustain, 0.3f},
                {echo, 0.4f}});
        preset("Tri Bass",
               {{wave, triangle},
                {octave, -1.0f},
                {voices, 0.0f},
                {decay, 1.0f},
                {sustain, 1.0f},
                {release, 30.0f},
                {glide, 20.0f}});
        preset("Wave Pad",
               {{wave, custom},
                {attack, 250.0f},
                {decay, 800.0f},
                {sustain, 0.7f},
                {release, 600.0f},
                {vibratoDepth, 0.15f},
                {vibratoRate, 4.5f},
                {echo, 0.35f}});
        preset("Laser Zap",
               {{wave, pulse},
                {duty, 0.0f},
                {dutySweep, 0.7f},
                {sweep, 24.0f},
                {sweepTime, 180.0f},
                {decay, 220.0f},
                {sustain, 0.0f},
                {release, 40.0f}});
        preset("Noise Snare",
               {{wave, noise},
                {noiseMode, 0.0f},
                {decay, 140.0f},
                {sustain, 0.0f},
                {release, 30.0f},
                {sweep, 5.0f},
                {sweepTime, 60.0f},
                {voices, 1.0f}});
        preset("Metal Hat",
               {{wave, noise},
                {noiseMode, 1.0f},
                {octave, 2.0f},
                {decay, 60.0f},
                {sustain, 0.0f},
                {release, 20.0f}});
        preset("Crunchy Saw",
               {{wave, saw}, {bits, 4.0f}, {rate, 8000.0f}, {decay, 500.0f}, {sustain, 0.6f}, {echo, 0.2f}});
        return made;
    }();
    return list;
}

std::vector<int> arpSteps(int shape)
{
    const auto& s = shapes[static_cast<size_t>(std::clamp(shape, 0, 7))];
    return {s.steps.begin(), s.steps.begin() + s.count};
}

std::array<int, waveSteps> waveShape(const float* params)
{
    std::array<int, waveSteps> out{};
    const int shape = std::clamp(static_cast<int>(std::lround(params[wave])), 0, 4);
    const float d =
        duties[static_cast<size_t>(std::clamp(static_cast<int>(std::lround(params[duty])), 0, 3))];
    std::uint16_t lfsr = 1;
    for (int s = 0; s < waveSteps; ++s)
    {
        const float ph = static_cast<float>(s) / waveSteps;
        switch (shape)
        {
            case pulse:
                out[static_cast<size_t>(s)] = ph < d ? 15 : 0;
                break;
            case triangle:
                out[static_cast<size_t>(s)] = s < 16 ? s : 31 - s;
                break;
            case saw:
                out[static_cast<size_t>(s)] = s / 2;
                break;
            case noise:
            {
                const auto bit = static_cast<std::uint16_t>((lfsr ^ (lfsr >> 1)) & 1u);
                lfsr = static_cast<std::uint16_t>((lfsr >> 1) | (bit << 14));
                out[static_cast<size_t>(s)] = (lfsr & 1u) != 0 ? 15 : 0;
                break;
            }
            default:
                out[static_cast<size_t>(s)] =
                    std::clamp(static_cast<int>(std::lround(params[firstWaveStep + s])), 0, 15);
                break;
        }
    }
    return out;
}

} // namespace aural::plugins::starbit
