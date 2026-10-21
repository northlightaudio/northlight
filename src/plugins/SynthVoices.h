#pragma once

#include "engine/Midi.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

// What the built-in instruments share: the envelope stages, the envelope
// curves, voice allocation and how a MIDI event is read. Real-time safe.

namespace northlight::plugins::synth
{

enum class Stage : std::uint8_t
{
    off,
    attack,
    decay,
    sustain,
    release
};

// An ADSR envelope, stepped once per sample. step(): a linear attack and
// exponential decay and release (Aurora, Nebula); stepLinear(): linear all the
// way, as a sound chip counts its volume (Starbit).
struct Envelope
{
    Stage stage = Stage::off;
    float value = 0.0f;

    bool active() const noexcept { return stage != Stage::off; }
    void start() noexcept { stage = Stage::attack; }
    void release() noexcept
    {
        if (stage != Stage::off)
            stage = Stage::release;
    }

    float step(float attackStep, float decayCoef, float sustain, float releaseCoef) noexcept
    {
        switch (stage)
        {
            case Stage::attack:
                value += attackStep;
                if (value >= 1.0f)
                {
                    value = 1.0f;
                    stage = Stage::decay;
                }
                break;
            case Stage::decay:
                value = sustain + (value - sustain) * decayCoef;
                if (value - sustain < 1.0e-4f)
                {
                    value = sustain;
                    stage = Stage::sustain;
                }
                break;
            case Stage::sustain:
                value = sustain;
                break;
            case Stage::release:
                value *= releaseCoef;
                if (value < 1.0e-5f)
                {
                    value = 0.0f;
                    stage = Stage::off;
                }
                break;
            case Stage::off:
                break;
        }
        return value;
    }

    float stepLinear(float attackStep, float decayStep, float sustain, float releaseStep) noexcept
    {
        switch (stage)
        {
            case Stage::attack:
                value += attackStep;
                if (value >= 1.0f)
                {
                    value = 1.0f;
                    stage = Stage::decay;
                }
                break;
            case Stage::decay:
                value -= decayStep;
                if (value <= sustain)
                {
                    value = sustain;
                    stage = Stage::sustain;
                }
                break;
            case Stage::sustain:
                value = sustain;
                break;
            case Stage::release:
                value -= releaseStep;
                if (value <= 0.0f)
                {
                    value = 0.0f;
                    stage = Stage::off;
                }
                break;
            case Stage::off:
                break;
        }
        return value;
    }
};

// The voice for a note: the one already playing it (a retrigger), else a free
// one, else the oldest (stolen). Voice needs `note`, `age` and active().
template <typename Voice> Voice& allocateVoice(Voice* voices, int count, int note) noexcept
{
    Voice* voice = nullptr;
    for (int i = 0; i < count; ++i)
        if (voices[i].note == note && voices[i].active())
            voice = &voices[i];
    if (voice == nullptr)
        for (int i = 0; i < count; ++i)
            if (!voices[i].active())
            {
                voice = &voices[i];
                break;
            }
    if (voice == nullptr)
    {
        voice = &voices[0];
        for (int i = 0; i < count; ++i)
            if (voices[i].age < voice->age)
                voice = &voices[i];
    }
    return *voice;
}

// What a player's hands add to the notes: the pitch bend (+-2 semitones), the
// mod wheel and channel pressure (both deepen a 5.5 Hz vibrato, up to half a
// semitone), and the sustain pedal (a note released while it is down keeps
// sounding until it comes up). Real-time safe.
struct Expression
{
    static constexpr double bendRangeSemitones = 2.0;
    static constexpr double vibratoHz = 5.5;
    static constexpr double vibratoSemitones = 0.5;

    float bend = 0.0f;     // -1..1 of the range
    float modWheel = 0.0f; // 0..1
    float pressure = 0.0f; // 0..1
    bool pedal = false;
    std::array<std::uint8_t, 128> sustained{}; // released while the pedal held them
    double vibratoPhase = 0.0;                 // 0..1

    void reset() noexcept { *this = Expression{}; }
    // Whether the pitch is untouched (no bend, no vibrato): the synth can skip the maths.
    bool neutral() const noexcept { return bend == 0.0f && modWheel <= 0.0f && pressure <= 0.0f; }
    float vibratoDepth() const noexcept { return modWheel > pressure ? modWheel : pressure; }
    // Moves the vibrato on by `seconds`; returns the pitch offset in semitones
    // (bend plus vibrato) for that moment. Exactly 0 when neutral().
    double advance(double seconds) noexcept
    {
        if (neutral())
        {
            vibratoPhase = 0.0;
            return 0.0;
        }
        vibratoPhase += seconds * vibratoHz;
        vibratoPhase -= std::floor(vibratoPhase);
        return bend * bendRangeSemitones +
               vibratoDepth() * vibratoSemitones * std::sin(2.0 * std::numbers::pi * vibratoPhase);
    }
    static double ratio(double semitones) noexcept
    {
        return semitones == 0.0 ? 1.0 : std::exp2(semitones / 12.0);
    }
};

// How an instrument reads an event: a note-on with velocity 0 is a note-off;
// controllers are ignored.
template <typename NoteOn, typename NoteOff, typename AllOff>
void dispatch(const engine::MidiEvent& e, NoteOn&& noteOn, NoteOff&& noteOff, AllOff&& allOff) noexcept
{
    switch (e.type)
    {
        case engine::MidiEvent::Type::noteOn:
            if (e.velocity == 0)
                noteOff(e.note);
            else
                noteOn(e.note, e.velocity);
            break;
        case engine::MidiEvent::Type::noteOff:
            noteOff(e.note);
            break;
        case engine::MidiEvent::Type::allNotesOff:
            allOff();
            break;
        default:
            break;
    }
}

// The same, with the player's expression: pitch bend, the mod wheel (CC 1),
// channel pressure and the sustain pedal (CC 64) update `x`; a note-off while
// the pedal is down waits for it to come up; reset-all-controllers (CC 121)
// returns everything to rest.
template <typename NoteOn, typename NoteOff, typename AllOff>
void dispatch(
    const engine::MidiEvent& e, Expression& x, NoteOn&& noteOn, NoteOff&& noteOff, AllOff&& allOff) noexcept
{
    const auto releasePedal = [&]
    {
        x.pedal = false;
        for (int n = 0; n < 128; ++n)
            if (x.sustained[static_cast<size_t>(n)] != 0)
            {
                x.sustained[static_cast<size_t>(n)] = 0;
                noteOff(n);
            }
    };
    switch (e.type)
    {
        case engine::MidiEvent::Type::noteOn:
            x.sustained[e.note & 127u] = 0;
            if (e.velocity == 0)
            {
                if (x.pedal)
                    x.sustained[e.note & 127u] = 1;
                else
                    noteOff(e.note);
            }
            else
                noteOn(e.note, e.velocity);
            break;
        case engine::MidiEvent::Type::noteOff:
            if (x.pedal)
                x.sustained[e.note & 127u] = 1;
            else
                noteOff(e.note);
            break;
        case engine::MidiEvent::Type::allNotesOff:
            x.sustained.fill(0);
            allOff();
            break;
        case engine::MidiEvent::Type::controller:
            switch (e.note)
            {
                case 1:
                    x.modWheel = static_cast<float>(e.velocity & 127) / 127.0f;
                    break;
                case 64:
                    if (e.velocity >= 64)
                        x.pedal = true;
                    else if (x.pedal)
                        releasePedal();
                    break;
                case 121:
                    x.bend = 0.0f;
                    x.modWheel = 0.0f;
                    x.pressure = 0.0f;
                    if (x.pedal)
                        releasePedal();
                    break;
                default:
                    break;
            }
            break;
        case engine::MidiEvent::Type::pitchBend:
        {
            const int value = e.bendValue();
            x.bend = static_cast<float>(value) / (value < 0 ? 8192.0f : 8191.0f);
            break;
        }
        case engine::MidiEvent::Type::channelPressure:
            x.pressure = static_cast<float>(e.velocity & 127) / 127.0f;
            break;
        default:
            break;
    }
}

} // namespace northlight::plugins::synth
