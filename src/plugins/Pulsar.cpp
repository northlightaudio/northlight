#include "plugins/Pulsar.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/MeteredBuiltIn.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>
#include <tuple>

namespace aural::plugins::pulsar
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

constexpr std::array<double, 6> hatRatios{
    1.0, 1.4827, 1.8002, 2.546, 2.6303, 3.8968}; // 205, 304, 370, 523, 540, 800 Hz

struct Rng
{
    std::uint32_t state = 0x9E3779B9u;
    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<double>(state >> 8) / 8388608.0 - 1.0);
    }
};

// A two-pole state-variable filter (Chamberlin form is enough for noise).
struct Svf
{
    float low = 0.0f, band = 0.0f;
    float run(float x, float f, float q, float& high) noexcept
    {
        low += f * band;
        high = x - low - q * band;
        band += f * high;
        return band;
    }
    void reset() noexcept { low = band = 0.0f; }
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
    return static_cast<float>(std::exp(std::log(0.001) / std::max(1.0, ms * 0.001 * sampleRate)));
}

// One pad's voice: the synthesis of every pad type, chosen by `pad`.
struct Voice
{
    int pad = 0;
    bool active = false;
    float velocity = 1.0f;
    float amp = 0.0f, ampCoef = 0.999f; // the main envelope
    float aux = 0.0f, auxCoef = 0.99f;  // a second envelope (pitch, body, noise)
    float click = 0.0f;
    double phase = 0.0, phase2 = 0.0;
    std::array<double, 6> hatPhase{};
    double time = 0.0; // seconds since the hit
    float choke = 1.0f, chokeCoef = 1.0f;
    Svf filter, filter2;
    float hp = 0.0f; // a one-pole high-pass's state
    Rng rng;

    void trigger(int which, float vel, const float* params, double sampleRate) noexcept
    {
        pad = which;
        active = true;
        velocity = vel;
        time = 0.0;
        amp = 1.0f;
        aux = 1.0f;
        click = 1.0f;
        phase = phase2 = 0.0;
        hatPhase = {0.0, 0.31, 0.57, 0.12, 0.83, 0.44};
        choke = 1.0f;
        chokeCoef = 1.0f;
        filter.reset();
        filter2.reset();
        hp = 0.0f;
        const float decayMs = params[paramIndex(pad, decay)];
        ampCoef = coefficient(sampleRate, decayMs);
        switch (pad)
        {
            case kick:
            case lowTom:
            case highTom: // the pitch sweep's time
                auxCoef = coefficient(sampleRate, 10.0 + 70.0 * (1.0 - params[paramIndex(pad, snap)]));
                break;
            case snare: // the shell rings shorter than the wires
                auxCoef = coefficient(sampleRate, decayMs * 0.45);
                break;
            default:
                auxCoef = coefficient(sampleRate, decayMs * 0.3);
                break;
        }
    }

    // A fast fade (a closed hat choking an open one).
    void chokeNow(double sampleRate) noexcept { chokeCoef = coefficient(sampleRate, 25.0); }

    float next(const float* params, double sampleRate) noexcept
    {
        if (!active)
            return 0.0f;
        const auto& info = pads[static_cast<size_t>(pad)];
        const float t = params[paramIndex(pad, tone)];
        const float s = params[paramIndex(pad, snap)];
        const double hz = info.baseHz * std::pow(2.0, params[paramIndex(pad, tune)] / 12.0);
        const double dt = 1.0 / sampleRate;
        const float noise = rng.next();
        float out = 0.0f;
        switch (pad)
        {
            case kick:
            case lowTom:
            case highTom:
            {
                // A sine falling from a punched-up pitch to its note, a click on top
                // (and for a tom, a breath of noise).
                const double sweep = pad == kick ? 1.0 + s * 9.0 : 1.0 + s * 2.0;
                const double f = hz * (1.0 + (sweep - 1.0) * aux);
                phase += f * dt;
                if (phase >= 1.0)
                    phase -= 1.0;
                out = static_cast<float>(std::sin(twoPi * phase));
                if (pad == kick)
                    out += t * click * (0.6f * noise + 0.8f);
                else
                {
                    float high;
                    out += t * 0.6f * filter.run(noise, 0.5f, 0.6f, high) * aux;
                    out += 0.3f * click * noise;
                }
                click *= 0.92f;
                break;
            }
            case snare:
            {
                // Two shell modes, and the wires: noise through a band-pass, as loud
                // as the snappiness, with a crack of bright noise at the start.
                phase += hz * dt * (1.0 + 0.3 * aux * s);
                if (phase >= 1.0)
                    phase -= 1.0;
                phase2 += hz * 1.62 * dt;
                if (phase2 >= 1.0)
                    phase2 -= 1.0;
                const float shell =
                    static_cast<float>(std::sin(twoPi * phase) + 0.5 * std::sin(twoPi * phase2)) * aux;
                float high;
                const float wires = filter.run(noise, 0.75f, 0.5f, high);
                out = shell * (1.0f - 0.6f * t) + wires * (0.4f + 1.4f * t) + high * s * click * 0.8f;
                click *= 0.96f;
                break;
            }
            case clap:
            {
                // Four bursts of band-passed noise 9 ms apart (wider apart with
                // spread), then the room's tail.
                const double spacing = 0.006 + 0.008 * s;
                float bursts = 0.0f;
                for (int b = 0; b < 4; ++b)
                {
                    const double since = time - b * spacing;
                    if (since >= 0.0)
                        bursts += static_cast<float>(std::exp(-since / 0.0045));
                }
                const float f = static_cast<float>(
                    2.0 * std::sin(std::numbers::pi * std::min(hz * (0.7 + 0.9 * t), sampleRate * 0.2) /
                                   sampleRate));
                float high;
                const float band = filter.run(noise, f, 0.55f, high);
                out = band * (std::min(1.0f, bursts) * 1.4f + 0.6f * (time > 3.0 * spacing ? 1.0f : 0.0f)) +
                      high * t * 0.2f * click;
                click *= 0.995f;
                break;
            }
            case closedHat:
            case openHat:
            {
                // Six detuned squares (the 808's metal), with noise, through a
                // high-pass as bright as the tone.
                float metal = 0.0f;
                for (size_t i = 0; i < hatRatios.size(); ++i)
                {
                    hatPhase[i] += hz * hatRatios[i] * dt;
                    if (hatPhase[i] >= 1.0)
                        hatPhase[i] -= 1.0;
                    metal += hatPhase[i] < 0.5 ? 1.0f : -1.0f;
                }
                const float x = metal / 6.0f * (1.0f - s) + noise * s;
                float high;
                filter.run(x, 0.9f, 0.4f, high);
                const float cutoff = 0.15f + 0.6f * t;
                hp = hp + cutoff * (high - hp);
                out = (high - hp * (1.0f - t)) * 0.9f;
                break;
            }
            case rim:
            {
                // A short ringing tone with a modulator as metallic as the ring,
                // and a click.
                phase2 += hz * 2.37 * dt;
                if (phase2 >= 1.0)
                    phase2 -= 1.0;
                phase += hz * dt * (1.0 + t * 0.8 * std::sin(twoPi * phase2));
                if (phase >= 1.0)
                    phase -= 1.0;
                out = static_cast<float>(std::sin(twoPi * phase)) + s * click * noise * 1.5f;
                click *= 0.9f;
                break;
            }
            default:
                break;
        }
        const float envelope = amp * choke;
        amp *= ampCoef;
        aux *= auxCoef;
        choke *= chokeCoef;
        time += dt;
        if (envelope < 1.0e-5f)
        {
            active = false;
            return 0.0f;
        }
        return out * envelope;
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
        if (e.type != engine::MidiEvent::Type::noteOn || e.velocity == 0)
            return; // drums play out: note-offs do nothing
        if (const int pad = padForNote(e.note); pad >= 0)
            hit(pad, e.velocity);
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
                const float x = voice.next(params_.data(), sampleRate_) * voice.velocity;
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
    AURAL_ASSERT_NOT_REALTIME("pulsar::create");
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

} // namespace aural::plugins::pulsar
