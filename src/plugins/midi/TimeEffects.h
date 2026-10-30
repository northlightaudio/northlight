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
// Whether a mode reads the held keys low to high (else as they were pressed).
bool sortsByPitch(int mode) noexcept;
// What one step plays: keys first..first+count-1 of the held keys (in the
// order sortsByPitch gives), `octave` octaves up. The effect plays this and
// the editor draws it, so the two always agree.
struct Step
{
    int first = 0;
    int count = 0; // every key for Chord, else one; none with no keys held
    int octave = 0;
};
// Step `step` of `keyCount` held keys over `octaves` octaves. Random draws its
// position (0..keyCount * octaves - 1) and passes it in; other modes ignore it.
Step stepAt(int mode, int step, int keyCount, int octaves, int randomPosition = 0) noexcept;
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
