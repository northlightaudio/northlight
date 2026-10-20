#pragma once

#include "engine/Midi.h"

#include <cstdint>

// What the built-in instruments share: the envelope stages, the envelope
// curves, voice allocation and how a MIDI event is read. Real-time safe.

namespace aural::plugins::synth
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

} // namespace aural::plugins::synth
