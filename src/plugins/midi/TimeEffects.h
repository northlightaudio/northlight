#pragma once

#include "engine/Insert.h"

#include <memory>

// The MIDI effects that play in time: the Arpeggiator, Note Repeat and Note Echo
// (docs/plans/midi-inserts.md). Synced to the song's grid while the transport
// plays; stopped, they run at the project tempo from the first key. JUCE-free.

namespace northlight::plugins
{

namespace arpeggiator
{
inline constexpr const char* type = "northlight.arpeggiator";
enum Param
{
    mode,     // Up, Down, Up/Down, Down/Up, As played, Random, Chord
    rate,     // a note value (MidiTools noteValues)
    octaves,  // 1..4
    gate,     // % of a step
    swing,    // %: every second step is late by up to half a step
    latch,    // Off, On: keeps playing after the keys are released
    velocity, // As played, Fixed
    fixedVelocity,
    count
};
enum Mode
{
    up,
    down,
    upDown,
    downUp,
    asPlayed,
    random,
    chord,
};
// Which of `total` positions step `step` plays in the order (up/down modes and
// as played; random and chord are not ordered).
int position(int mode, int step, int total) noexcept;
std::unique_ptr<engine::InsertProcessor> create();
} // namespace arpeggiator

namespace noterepeat
{
inline constexpr const char* type = "northlight.noterepeat";
enum Param
{
    rate, // a note value
    gate, // % of a step
    ramp, // % change of velocity per repeat
    count
};
std::unique_ptr<engine::InsertProcessor> create();
} // namespace noterepeat

namespace noteecho
{
inline constexpr const char* type = "northlight.noteecho";
enum Param
{
    time,     // a note value
    repeats,  // 1..16
    feedback, // % of the velocity each repeat keeps
    pitch,    // semitones each repeat moves
    count
};
std::unique_ptr<engine::InsertProcessor> create();
} // namespace noteecho

} // namespace northlight::plugins
