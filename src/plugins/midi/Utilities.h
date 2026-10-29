#pragma once

#include "engine/Insert.h"

#include <memory>

// The utility MIDI effects: Scale, Velocity, Key Range, MIDI Filter, Note
// Length and Humanise (docs/plans/midi-inserts.md). JUCE-free.

namespace northlight::plugins
{

// Scale: keeps notes in a key.
namespace scale
{
inline constexpr const char* type = "northlight.scale";
enum Param
{
    mode,   // Nearest, Up, Down, Block, White keys
    follow, // Own key, Project key
    root,   // C..B (its own key)
    kind,   // the scale (its own key): MidiTools scaleKinds order
    count
};
enum Mode
{
    nearest,
    up,
    down,
    block,
    whiteKeys, // the white keys play the scale from the root (black keys play nothing)
};
// Where a note goes (-1: it plays nothing). `root` 0..11, `mask` bit n = pitch class n.
int place(int note, int mode, int root, std::uint16_t mask) noexcept;
std::unique_ptr<engine::InsertProcessor> create();
} // namespace scale

// Velocity: reshapes note-on velocities.
namespace velocity
{
inline constexpr const char* type = "northlight.velocity";
enum Param
{
    dynamics,      // % around the centre: 100 unchanged, less compresses, more expands
    centre,        // 1..127
    gain,          // % scale
    offset,        // -127..127
    random,        // 0..64: up to this much either way
    minimum,       // 1..127
    maximum,       // 1..127
    fixed,         // Off, On: every note plays at the fixed velocity
    fixedVelocity, // 1..127
    count
};
struct Settings
{
    float dynamics = 100.0f, centre = 64.0f, gain = 100.0f, offset = 0.0f;
    float minimum = 1.0f, maximum = 127.0f;
    bool fixed = false;
    float fixedVelocity = 100.0f;
};
// A velocity through the settings; `random` (in velocity steps) is added
// before the range clamp, and a fixed velocity overrides everything.
int shape(int velocity, const Settings& settings, float random = 0.0f) noexcept;
std::unique_ptr<engine::InsertProcessor> create();
} // namespace velocity

// Key Range: passes the notes inside a range.
namespace keyrange
{
inline constexpr const char* type = "northlight.keyrange";
enum Param
{
    low,  // 0..127
    high, // 0..127
    mode, // Block outside, Fold in by octaves
    count
};
std::unique_ptr<engine::InsertProcessor> create();
} // namespace keyrange

// MIDI Filter: lets kinds of events through or not, and moves one controller to another.
namespace midifilter
{
inline constexpr const char* type = "northlight.midifilter";
enum Param
{
    notes,           // Block, Pass
    controllers,     // Block, Pass
    pitchBend,       // Block, Pass
    channelPressure, // Block, Pass
    polyPressure,    // Block, Pass
    move,            // Off, On: one controller is moved to another
    mapFrom,         // 0..127, the controller moved
    mapTo,           // 0..127, where it goes
    count
};
std::unique_ptr<engine::InsertProcessor> create();
} // namespace midifilter

// Note Length: sets how long notes last.
namespace notelength
{
inline constexpr const char* type = "northlight.notelength";
enum Param
{
    mode,  // As played, Note value, Time, Legato
    value, // a note value (MidiTools noteValues)
    time,  // ms
    pedal, // Pass, Hold: the sustain pedal holds the notes it would sustain, and is not sent
    count
};
enum Mode
{
    asPlayed,
    noteValue,
    fixedTime,
    legato, // each note lasts until the next one starts
};
std::unique_ptr<engine::InsertProcessor> create();
} // namespace notelength

// Humanise: random timing (late only), velocity, and a chance that each note plays.
namespace humanise
{
inline constexpr const char* type = "northlight.humanise";
enum Param
{
    timing,   // ms, up to this much late
    velocity, // up to this much either way
    chance,   // % of notes that play
    seed,     // 0..9999: the same seed plays the same variations
    count
};
std::unique_ptr<engine::InsertProcessor> create();
} // namespace humanise

} // namespace northlight::plugins
