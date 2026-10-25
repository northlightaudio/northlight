#include "plugins/Pulsar.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/MeteredBuiltIn.h"
#include "plugins/SynthVoices.h"
#include "plugins/dsp/PolyBlep.h"
#include "plugins/dsp/Random.h"
#include "plugins/dsp/Smoothing.h"
#include "plugins/dsp/Svf.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>
#include <tuple>

namespace northlight::plugins::pulsar
{

namespace
{
constexpr double twoPi = 2.0 * std::numbers::pi;

struct PadInfo
{
    const char* name;
    const char* shortName;
    const char* tone;
    const char* snap;
    int note;
    float baseHz;
    float tune, decayMs, tone0, snap0, level, pan; // defaults
};

// Base frequencies: a kick's body, a snare's shell, a clap's band centre, a
// hat's lowest square (the rest are the 808's ratios above it), a tom's body.
constexpr std::array<PadInfo, padCount> pads{{
    {"Kick", "KICK", "Click", "Punch", 36, 48.0f, 0.0f, 480.0f, 0.3f, 0.55f, 0.0f, 0.0f},
    {"Rim", "RIM", "Ring", "Click", 37, 1700.0f, 0.0f, 45.0f, 0.4f, 0.5f, -8.0f, 0.15f},
    {"Snare", "SNARE", "Snappy", "Crack", 38, 185.0f, 0.0f, 240.0f, 0.55f, 0.45f, -2.0f, 0.0f},
    {"Clap", "CLAP", "Bright", "Spread", 39, 1150.0f, 0.0f, 320.0f, 0.5f, 0.5f, -4.0f, -0.1f},
    {"Closed hat", "HAT", "Bright", "Noise", 42, 205.3f, 0.0f, 55.0f, 0.6f, 0.3f, -8.0f, 0.2f},
    {"Open hat", "OPEN", "Bright", "Noise", 46, 205.3f, 0.0f, 520.0f, 0.6f, 0.3f, -9.0f, 0.2f},
    {"Low tom", "TOM L", "Noise", "Punch", 45, 98.0f, 0.0f, 420.0f, 0.15f, 0.4f, -4.0f, -0.25f},
    {"High tom", "TOM H", "Noise", "Punch", 48, 165.0f, 0.0f, 340.0f, 0.15f, 0.4f, -5.0f, 0.25f},
}};

// The 808's hat oscillators: six squares at these ratios to the lowest.
constexpr std::array<double, 6> hatRatios{
    1.0, 1.4827, 1.8002, 2.546, 2.6303, 3.8968}; // 205, 304, 370, 523, 540, 800 Hz

// The kit's state-variable filter tuned in hertz, so a pad sounds the same at any
// sample rate, keeping its last outputs for the next stage to read.
struct TunedSvf
{
    float g = 0.1f, k = 1.4f;
    dsp::Svf state;
    float low = 0.0f, band = 0.0f, high = 0.0f;
    void set(double hz, float damping, double sampleRate) noexcept
    {
        const double f = std::clamp(hz, 10.0, sampleRate * 0.45);
        g = static_cast<float>(std::tan(std::numbers::pi * f / sampleRate));
        k = damping;
    }
    void run(float x) noexcept { state.run(x, g, k, low, band, high); }
    void reset() noexcept
    {
        state.reset();
        low = band = high = 0.0f;
    }
};

std::string lowerCase(const char* text)
{
    std::string s = text;
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline float coefficient(double sampleRate, double ms) noexcept
{
    // Exponential: down 60 dB over the time.
    return static_cast<float>(dsp::decay60ForMs(ms, sampleRate));
}

// Exponential with a time constant (down to 37% after `ms`).
inline float falloff(double sampleRate, double ms) noexcept
{
    return static_cast<float>(std::exp(-1000.0 / (std::max(0.01, ms) * sampleRate)));
}

// The soft clipping of an overdriven analogue voice: unity gain while quiet, the
// peaks rounded off (and harmonics added) the harder it is pushed.
inline float saturate(float x, float drive) noexcept
{
    return std::tanh(x * drive) / drive;
}

inline double wrap(double phase) noexcept
{
    return phase >= 1.0 ? phase - 1.0 : phase;
}

// When the clap's four bursts start, in spacings: a little uneven, like hands.
constexpr std::array<double, 4> clapBursts{0.0, 1.0, 2.1, 3.25};

// Samples between updates of a voice's filters (which follow the mod wheel).
constexpr int filterInterval = 16;

// One pad's voice, modelled on the TR-808's circuits: the kick and toms a
// resonator rung by a pulse (a sine that drops a little in pitch), the snare two
// shell modes and high-passed noise, the clap a filtered noise burst retriggered
// three times and a tail, the hats six square oscillators through a band-pass
// and a high-pass, the rim two short modes clipped together.
struct Voice
{
    int pad = 0;
    bool active = false;
    float velocity = 1.0f;
    float amp = 0.0f, ampCoef = 0.999f;     // the main envelope (the pad's decay)
    float body = 0.0f, bodyCoef = 0.999f;   // a second: a snare's shell, a rim's ring, a tom's noise
    float sweep = 0.0f, sweepCoef = 0.999f; // the pitch envelope
    float click = 0.0f, clickCoef = 0.9f;   // the attack
    double phase = 0.0, phase2 = 0.0;
    std::array<double, 6> hatPhase{};
    double time = 0.0; // seconds since the hit
    float choke = 1.0f, chokeCoef = 1.0f;
    TunedSvf f1, f2, f3;
    std::array<TunedSvf, 2> top; // the output's low-pass at 18 kHz (two stages)
    int untilUpdate = 0;
    float noiseGain = 1.0f; // white noise's level for the same density per hertz at any rate
    dsp::Lcg rng{0x9E3779B9u};

    void trigger(int which, float vel, const float* params, double sampleRate) noexcept
    {
        pad = which;
        active = true;
        velocity = vel;
        time = 0.0;
        amp = body = sweep = click = 1.0f;
        phase = phase2 = 0.0;
        hatPhase = {0.0, 0.31, 0.57, 0.12, 0.83, 0.44};
        choke = 1.0f;
        chokeCoef = 1.0f;
        f1.reset();
        f2.reset();
        f3.reset();
        for (auto& stage : top)
        {
            stage.reset();
            stage.set(18000.0, 1.3f, sampleRate);
        }
        untilUpdate = 0;
        noiseGain = static_cast<float>(std::sqrt(sampleRate / 48000.0));
        const float decayMs = params[paramIndex(pad, decay)];
        const float s = params[paramIndex(pad, snap)];
        ampCoef = coefficient(sampleRate, decayMs);
        switch (pad)
        {
            case kick: // more punch: a deeper, faster drop
                sweepCoef = falloff(sampleRate, 6.0 + 18.0 * (1.0 - s));
                clickCoef = falloff(sampleRate, 0.5);
                break;
            case lowTom:
            case highTom:
                sweepCoef = falloff(sampleRate, 12.0 + 40.0 * (1.0 - s));
                bodyCoef = falloff(sampleRate, 20.0);
                clickCoef = falloff(sampleRate, 0.4);
                break;
            case snare: // the shell rings shorter than the wires
                sweepCoef = falloff(sampleRate, 6.0);
                bodyCoef = coefficient(sampleRate, decayMs * 0.4);
                clickCoef = falloff(sampleRate, 2.0 + 2.0 * s);
                break;
            case rim: // the low mode dies first
                bodyCoef = coefficient(sampleRate, decayMs * 0.5);
                clickCoef = falloff(sampleRate, 0.6);
                break;
            default: // the clap and hats
                clickCoef = falloff(sampleRate, 2.5);
                break;
        }
    }

    // A fast fade (a closed hat choking an open one).
    void chokeNow(double sampleRate) noexcept { chokeCoef = coefficient(sampleRate, 25.0); }

    void setFilters(float t, float s, double hz, double sampleRate) noexcept
    {
        switch (pad)
        {
            case kick: // the click through a low-pass as bright as the tone
                f1.set(700.0 + 9000.0 * t * t, 1.2f, sampleRate);
                break;
            case lowTom:
            case highTom: // a breath of noise above the note
                f1.set(hz * 8.0, 1.0f, sampleRate);
                break;
            case snare: // the wires high-passed, then softened; the crack a band
                f1.set(1300.0, 1.1f, sampleRate);
                f2.set(4500.0 + 8000.0 * t, 1.2f, sampleRate);
                f3.set(3200.0 + 2500.0 * s, 0.9f, sampleRate);
                break;
            case clap: // a band around 1 kHz; the brightness a high-pass
                f1.set(hz * (0.7 + 0.9 * t), 0.55f, sampleRate);
                f2.set(5000.0, 1.0f, sampleRate);
                break;
            case closedHat:
            case openHat:
            {
                // The band follows the tuning a little; the high-pass opens with the tone.
                const double follow = std::sqrt(hz / pads[static_cast<size_t>(closedHat)].baseHz);
                f1.set((6500.0 + 4500.0 * t) * follow, 0.7f, sampleRate);
                f2.set((4000.0 + 4000.0 * t) * follow, 1.1f, sampleRate);
                break;
            }
            case rim:
                f1.set(400.0, 1.0f, sampleRate);
                break;
            default:
                break;
        }
    }

    // `pitch` scales the pad's tuning (the pitch bend); `brighten` 0..1 opens
    // its tone towards full (the mod wheel).
    float next(const float* params, double sampleRate, double pitch = 1.0, float brighten = 0.0f) noexcept
    {
        if (!active)
            return 0.0f;
        const auto& info = pads[static_cast<size_t>(pad)];
        const float baseTone = params[paramIndex(pad, tone)];
        const float t = brighten > 0.0f ? baseTone + (1.0f - baseTone) * brighten : baseTone;
        const float s = params[paramIndex(pad, snap)];
        const double hz = info.baseHz * std::pow(2.0, params[paramIndex(pad, tune)] / 12.0) * pitch;
        const double dt = 1.0 / sampleRate;
        if (--untilUpdate <= 0)
        {
            setFilters(t, s, hz, sampleRate);
            untilUpdate = filterInterval;
        }
        const float noise = rng.bipolar() * noiseGain;
        // Velocity drives the analogue voices harder (the 808's accent).
        const float drive = 0.5f + 0.9f * velocity;
        float out = 0.0f;
        bool tailDone = true; // false while a clap's bursts are still to come
        switch (pad)
        {
            case kick:
            {
                // A sine dropping from a punched-up pitch to its note, rounded by
                // the drive; the trigger's click through the tone's low-pass.
                const double ratio = 1.08 + 1.9 * s * s;
                phase = wrap(phase + hz * (1.0 + (ratio - 1.0) * sweep) * dt);
                const float boom = 1.4f * saturate(static_cast<float>(std::sin(twoPi * phase)) * amp, drive);
                f1.run((0.9f + 0.4f * noise) * click);
                out = boom + f1.low * (0.2f + 2.6f * t);
                break;
            }
            case lowTom:
            case highTom:
            {
                const double ratio = 1.0 + 0.6 * s;
                phase = wrap(phase + hz * (1.0 + (ratio - 1.0) * sweep) * dt);
                const float tom = 1.2f * saturate(static_cast<float>(std::sin(twoPi * phase)) * amp, drive);
                f1.run(noise);
                out = tom + (f1.band * t * 1.4f * body + noise * 0.25f * click) * amp;
                break;
            }
            case snare:
            {
                // Two shell modes (a pitch bump on the crack), and the wires as loud
                // as the snappiness, with a burst of band-passed noise on top.
                const double bump = 1.0 + 0.35 * s * sweep;
                phase = wrap(phase + hz * bump * dt);
                phase2 = wrap(phase2 + hz * 1.78 * bump * dt);
                const float shell =
                    static_cast<float>(std::sin(twoPi * phase) + 0.55 * std::sin(twoPi * phase2)) * body;
                f1.run(noise);
                f2.run(f1.high);
                f3.run(noise);
                out = (saturate(shell * (1.0f - 0.5f * t), drive) * 1.5f + f2.low * (0.4f + 1.4f * t)) * amp +
                      f3.band * click * s * 1.1f;
                break;
            }
            case clap:
            {
                // Band-passed noise, retriggered: three short bursts and a longer
                // fourth (further apart with spread), and a tail that fades in under
                // them and lasts the decay.
                const double spacing = 0.006 + 0.008 * s;
                constexpr int lastBurst = static_cast<int>(clapBursts.size()) - 1;
                int b = lastBurst;
                while (b > 0 && time < clapBursts[static_cast<size_t>(b)] * spacing)
                    --b;
                const double since = time - clapBursts[static_cast<size_t>(b)] * spacing;
                const auto burst = static_cast<float>(std::exp(-since / (b < lastBurst ? 0.0022 : 0.008)));
                const double tailStart = clapBursts.back() * spacing;
                const float tail = 0.55f * amp * static_cast<float>(std::min(1.0, time / tailStart));
                f1.run(noise);
                f2.run(noise);
                out = f1.band * (burst + tail) * 2.5f + f2.high * burst * t * 0.35f;
                tailDone = time > tailStart + 0.05;
                break;
            }
            case closedHat:
            case openHat:
            {
                // Six band-limited squares (the 808's metal), with noise, through a
                // band-pass and a high-pass; a little more level in the first ms.
                float metal = 0.0f;
                for (size_t i = 0; i < hatRatios.size(); ++i)
                {
                    const double inc = hz * hatRatios[i] * dt;
                    hatPhase[i] = wrap(hatPhase[i] + inc);
                    metal += dsp::blepSquare(hatPhase[i], inc);
                }
                f1.run(metal / 6.0f * (1.0f - s) + noise * s * 0.7f);
                f2.run(f1.band);
                out = f2.high * amp * (0.75f + 0.5f * click) * 3.0f;
                break;
            }
            case rim:
            {
                // Two modes (around 1.7 kHz and 460 Hz, the high one as loud as the
                // ring), clipped hard together as the 808's rim shot is, and a click.
                phase2 = wrap(phase2 + hz * 0.272 * dt);
                phase = wrap(phase + hz * dt);
                const float modes = (static_cast<float>(std::sin(twoPi * phase)) * (0.5f + 0.8f * t) +
                                     0.45f * static_cast<float>(std::sin(twoPi * phase2)) * body) *
                                    amp;
                f1.run(std::tanh(2.5f * modes) * 0.6f + noise * click * s * 0.9f);
                out = f1.high;
                break;
            }
            default:
                break;
        }
        // Noise and the hats' squares reach far above 20 kHz at a high sample
        // rate: the same top at every rate keeps the pad sounding the same.
        for (auto& stage : top)
        {
            stage.run(out);
            out = stage.low;
        }
        const float envelope = amp * choke;
        out *= choke;
        amp *= ampCoef;
        body *= bodyCoef;
        sweep *= sweepCoef;
        click *= clickCoef;
        choke *= chokeCoef;
        time += dt;
        if (envelope < 1.0e-5f && (tailDone || choke < 1.0e-3f))
        {
            active = false;
            return 0.0f;
        }
        return out;
    }
};

class Pulsar final : public MeteredBuiltIn
{
public:
    Pulsar() : MeteredBuiltIn(type, pulsar::parameterInfo(), true) {}
    bool acceptsMidi() const noexcept override { return true; }
    Activity activity;

private:
    // The extra outputs: as many as the highest a pad uses (so a kit all on the
    // main pair has none), each a pair of buffers filled by every process call.
    int auxOutputCount() const noexcept override
    {
        int highest = 0;
        for (int pad = 0; pad < padCount; ++pad)
            highest = std::max(highest, routeOf(pad));
        return highest;
    }
    const float* auxOutput(int index, int channel) const noexcept override
    {
        const auto i = static_cast<size_t>(index * 2 + channel);
        return index >= 0 && index < extraOutputs && channel >= 0 && channel < 2 && !aux_[i].empty()
                   ? aux_[i].data()
                   : nullptr;
    }
    std::string auxOutputName(int index) const override
    {
        std::array<float, count> p{};
        for (int i = 0; i < count; ++i)
            p[static_cast<size_t>(i)] = parameter(i);
        return routeName(p.data(), index + 1);
    }

    void prepareBuiltIn(double, int maxBlockSize) override
    {
        for (auto& buffer : aux_)
            buffer.assign(static_cast<size_t>(std::max(1, maxBlockSize)), 0.0f);
        for (auto& v : voices_)
            v.active = false;
        expression_.reset();
        for (auto& l : activity.level)
            l.store(0.0f, std::memory_order_relaxed);
    }

    void hit(int pad, int velocity) noexcept
    {
        const float sensitivity = parameter(pulsar::velocity);
        const float v = 1.0f - sensitivity + sensitivity * static_cast<float>(velocity) / 127.0f;
        voices_[static_cast<size_t>(pad)].trigger(pad, v, params_.data(), sampleRate_);
        voices_[static_cast<size_t>(pad)].rng.state ^= static_cast<std::uint32_t>(velocity * 2654435761u);
        if (pad == closedHat)
            voices_[openHat].chokeNow(sampleRate_);
        activity.hits[static_cast<size_t>(pad)].fetch_add(1, std::memory_order_relaxed);
    }

    void handle(const engine::MidiEvent& e) noexcept
    {
        // Drums play out: note-offs (and the sustain pedal) do nothing. The pitch
        // bend tunes every pad (+-2 semitones); the mod wheel opens their tone.
        synth::dispatch(
            e,
            expression_,
            [this](int note, int velocity)
            {
                if (const int pad = padForNote(note); pad >= 0)
                    hit(pad, velocity);
            },
            [](int) {},
            [] {});
    }

    int routeOf(int pad) const noexcept
    {
        return std::clamp(static_cast<int>(std::lround(parameter(paramIndex(pad, route)))), 0, extraOutputs);
    }

    // Where this chunk's samples go in the extra outputs' buffers (a block longer
    // than prepare() announced comes in chunks; the buffers hold the first).
    void clearAux(int numSamples) noexcept
    {
        for (auto& buffer : aux_)
        {
            const auto from = std::min(buffer.size(), static_cast<size_t>(chunkStart_));
            const auto to = std::min(buffer.size(), static_cast<size_t>(chunkStart_ + numSamples));
            std::fill(buffer.begin() + static_cast<std::ptrdiff_t>(from),
                      buffer.begin() + static_cast<std::ptrdiff_t>(to),
                      0.0f);
        }
    }

    void processBypassed(float* left, float* right, int numSamples) noexcept override
    {
        clearAux(numSamples); // off: the extra outputs are silent too
        MeteredBuiltIn::processBypassed(left, right, numSamples);
    }

    void processBuiltIn(float* left, float* right, int numSamples) noexcept override
    {
        for (int p = 0; p < count; ++p)
            params_[static_cast<size_t>(p)] = parameter(p);
        clearAux(numSamples);
        std::array<int, padCount> routes{};
        for (int pad = 0; pad < padCount; ++pad)
            routes[static_cast<size_t>(pad)] = routeOf(pad);
        std::array<float, padCount> gainL{}, gainR{};
        for (int pad = 0; pad < padCount; ++pad)
        {
            const float g = dbToGain(params_[static_cast<size_t>(paramIndex(pad, level))]);
            const float pan =
                std::clamp(params_[static_cast<size_t>(paramIndex(pad, pulsar::pan))], -1.0f, 1.0f);
            gainL[static_cast<size_t>(pad)] = g * std::sqrt(0.5f * (1.0f - pan)) * 1.4142f;
            gainR[static_cast<size_t>(pad)] = g * std::sqrt(0.5f * (1.0f + pan)) * 1.4142f;
        }
        const float driveAmount = params_[drive];
        const float driveGain = 1.0f + driveAmount * 6.0f;
        const float driveNorm = 1.0f / std::tanh(driveGain);
        const float outGain = dbToGain(params_[output]);
        std::array<float, padCount> peak{};

        size_t next = firstChunkEvent();
        const engine::MidiEventList* events = midi_;
        for (int i = 0; i < numSamples; ++i)
        {
            while (events != nullptr && next < events->size() &&
                   std::clamp((*events)[next].sampleOffset - chunkStart_, 0, numSamples) <= i)
                handle((*events)[next++]);
            // Each output sums its pads; the master's drive and level apply to each.
            std::array<float, extraOutputs + 1> l{}, r{};
            int used = 0;
            for (int pad = 0; pad < padCount; ++pad)
            {
                auto& voice = voices_[static_cast<size_t>(pad)];
                if (!voice.active)
                    continue;
                const float x = voice.next(params_.data(),
                                           sampleRate_,
                                           synth::Expression::ratio(expression_.bend *
                                                                    synth::Expression::bendRangeSemitones),
                                           expression_.modWheel) *
                                voice.velocity;
                peak[static_cast<size_t>(pad)] = std::max(peak[static_cast<size_t>(pad)], std::abs(x));
                const auto to = static_cast<size_t>(routes[static_cast<size_t>(pad)]);
                l[to] += x * gainL[static_cast<size_t>(pad)];
                r[to] += x * gainR[static_cast<size_t>(pad)];
                used = std::max(used, static_cast<int>(to));
            }
            const auto finish = [&](float v)
            { return (driveAmount > 0.001f ? std::tanh(v * driveGain) * driveNorm : v) * outGain * 0.5f; };
            left[i] = finish(l[0]);
            right[i] = finish(r[0]);
            const auto at = static_cast<size_t>(chunkStart_ + i);
            for (int o = 1; o <= used; ++o)
            {
                auto& bl = aux_[static_cast<size_t>((o - 1) * 2)];
                auto& br = aux_[static_cast<size_t>((o - 1) * 2 + 1)];
                if (at < bl.size())
                {
                    bl[at] = finish(l[static_cast<size_t>(o)]);
                    br[at] = finish(r[static_cast<size_t>(o)]);
                }
            }
        }
        while (events != nullptr && next < events->size() && inChunk((*events)[next], numSamples))
            handle((*events)[next++]);
        for (int pad = 0; pad < padCount; ++pad)
            activity.level[static_cast<size_t>(pad)].store(std::min(1.0f, peak[static_cast<size_t>(pad)]),
                                                           std::memory_order_relaxed);
    }

    std::array<Voice, padCount> voices_{};
    synth::Expression expression_;
    std::array<float, count> params_{};
    std::array<std::vector<float>, extraOutputs * 2> aux_; // the extra pairs, left then right
};
} // namespace

const char* padName(int pad) noexcept
{
    return pad >= 0 && pad < padCount ? pads[static_cast<size_t>(pad)].name : "";
}
const char* padShortName(int pad) noexcept
{
    return pad >= 0 && pad < padCount ? pads[static_cast<size_t>(pad)].shortName : "";
}
const char* toneName(int pad) noexcept
{
    return pad >= 0 && pad < padCount ? pads[static_cast<size_t>(pad)].tone : "";
}
const char* snapName(int pad) noexcept
{
    return pad >= 0 && pad < padCount ? pads[static_cast<size_t>(pad)].snap : "";
}
int padNote(int pad) noexcept
{
    return pad >= 0 && pad < padCount ? pads[static_cast<size_t>(pad)].note : -1;
}
int padRoute(const float* params, int pad) noexcept
{
    return std::clamp(static_cast<int>(std::lround(params[paramIndex(pad, route)])), 0, extraOutputs);
}

std::string routeName(const float* params, int extra)
{
    std::string name;
    for (int pad = 0; pad < padCount; ++pad)
        if (padRoute(params, pad) == extra)
            name += (name.empty() ? "" : " + ") + std::string(pads[static_cast<size_t>(pad)].name);
    return name;
}

int padForNote(int note) noexcept
{
    for (int p = 0; p < padCount; ++p)
        if (pads[static_cast<size_t>(p)].note == note)
            return p;
    if (note >= 60 && note < 60 + padCount) // C3 up: the pads in order
        return note - 60;
    return -1;
}

std::unique_ptr<engine::InsertProcessor> create()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("pulsar::create");
    return std::make_unique<Pulsar>();
}

std::vector<engine::ParameterInfo> parameterInfo()
{
    std::vector<engine::ParameterInfo> p(count);
    for (int pad = 0; pad < padCount; ++pad)
    {
        const auto& info = pads[static_cast<size_t>(pad)];
        const std::string name = info.name;
        p[static_cast<size_t>(paramIndex(pad, tune))] = {
            name + " tune", "st", -24.0f, 24.0f, info.tune, false, {}};
        p[static_cast<size_t>(paramIndex(pad, decay))] = {
            name + " decay", "ms", 10.0f, 3000.0f, info.decayMs, true, {}};
        p[static_cast<size_t>(paramIndex(pad, tone))] = {
            name + " " + lowerCase(info.tone), "", 0.0f, 1.0f, info.tone0, false, {}};
        p[static_cast<size_t>(paramIndex(pad, snap))] = {
            name + " " + lowerCase(info.snap), "", 0.0f, 1.0f, info.snap0, false, {}};
        p[static_cast<size_t>(paramIndex(pad, level))] = {
            name + " level", "dB", -60.0f, 6.0f, info.level, false, {}};
        p[static_cast<size_t>(paramIndex(pad, pan))] = {name + " pan", "", -1.0f, 1.0f, info.pan, false, {}};
        p[static_cast<size_t>(paramIndex(pad, route))] = {
            name + " output",
            "",
            0.0f,
            static_cast<float>(extraOutputs),
            0.0f,
            false,
            {"Main", "3/4", "5/6", "7/8", "9/10", "11/12", "13/14", "15/16"}};
    }
    p[drive] = {"Drive", "", 0.0f, 1.0f, 0.15f, false, {}};
    p[velocity] = {"Velocity", "", 0.0f, 1.0f, 0.7f, false, {}};
    p[output] = {"Level", "dB", -60.0f, 6.0f, -3.0f, false, {}};
    return p;
}

const std::vector<Kit>& kits()
{
    static const std::vector<Kit> list = []
    {
        std::vector<float> base;
        for (const auto& info : parameterInfo())
            base.push_back(info.defaultValue);
        std::vector<Kit> made;
        using Set = std::initializer_list<std::tuple<int, Field, float>>;
        const auto kit =
            [&](const char* name, Set settings, std::initializer_list<std::pair<int, float>> master = {})
        {
            Kit k{name, base};
            for (const auto& [pad, field, value] : settings)
                k.values[static_cast<size_t>(paramIndex(pad, field))] = value;
            for (const auto& [param, value] : master)
                k.values[static_cast<size_t>(param)] = value;
            made.push_back(std::move(k));
        };
        kit("Pulsar", {});
        kit("Boom Room",
            {{kick, tune, -3.0f},
             {kick, decay, 1400.0f},
             {kick, snap, 0.35f},
             {kick, tone, 0.1f},
             {snare, decay, 380.0f},
             {snare, tone, 0.4f},
             {clap, decay, 600.0f},
             {openHat, decay, 900.0f},
             {lowTom, decay, 900.0f},
             {highTom, decay, 700.0f}},
            {{drive, 0.35f}});
        kit("Tight Electro",
            {{kick, decay, 260.0f},
             {kick, snap, 0.8f},
             {kick, tone, 0.6f},
             {snare, tune, 4.0f},
             {snare, decay, 140.0f},
             {snare, snap, 0.8f},
             {clap, decay, 180.0f},
             {clap, snap, 0.2f},
             {closedHat, decay, 30.0f},
             {closedHat, tone, 0.85f},
             {openHat, decay, 260.0f}},
            {{drive, 0.25f}});
        kit("Dusty",
            {{kick, tune, -1.0f},
             {kick, tone, 0.15f},
             {kick, snap, 0.4f},
             {snare, tone, 0.7f},
             {snare, decay, 200.0f},
             {closedHat, tone, 0.35f},
             {closedHat, snap, 0.6f},
             {openHat, tone, 0.35f},
             {openHat, snap, 0.6f},
             {rim, tone, 0.15f}},
            {{drive, 0.55f}, {output, -5.0f}});
        kit("Minimal Clicks",
            {{kick, decay, 180.0f},
             {kick, snap, 0.25f},
             {kick, tone, 0.8f},
             {rim, decay, 25.0f},
             {rim, level, -4.0f},
             {snare, decay, 90.0f},
             {snare, tone, 0.85f},
             {closedHat, decay, 20.0f},
             {closedHat, tone, 0.95f},
             {lowTom, tune, 7.0f},
             {lowTom, decay, 140.0f},
             {highTom, tune, 12.0f},
             {highTom, decay, 110.0f}},
            {{drive, 0.0f}});
        kit("Industrial",
            {{kick, tune, -5.0f},
             {kick, snap, 1.0f},
             {kick, tone, 0.9f},
             {snare, tune, -6.0f},
             {snare, snap, 1.0f},
             {snare, tone, 0.9f},
             {clap, tune, -5.0f},
             {clap, snap, 0.9f},
             {closedHat, snap, 0.9f},
             {openHat, snap, 0.9f},
             {rim, tone, 1.0f}},
            {{drive, 0.85f}, {output, -7.0f}});
        return made;
    }();
    return list;
}

std::vector<float> renderPad(const float* params, int pad, double sampleRate, int samples)
{
    std::vector<float> out(static_cast<size_t>(std::max(0, samples)), 0.0f);
    if (pad < 0 || pad >= padCount)
        return out;
    Voice voice;
    voice.trigger(pad, 1.0f, params, sampleRate);
    for (auto& v : out)
        v = voice.next(params, sampleRate);
    return out;
}

const Activity* activity(const engine::InsertProcessor& processor) noexcept
{
    if (const auto* p = dynamic_cast<const Pulsar*>(&processor))
        return &p->activity;
    return nullptr;
}

} // namespace northlight::plugins::pulsar
