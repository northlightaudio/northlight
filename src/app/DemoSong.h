#pragma once

#include "core/Model.h"

// The demo song, a test project for development (benchmarks, captures, bounce
// tests), not shipped or promoted: "Window Seat", a lofi hip hop piece in F
// major at 75 bpm played only by the built-in synths: Lumen's electric piano on
// jazzy seventh and ninth chords, slow swung Pulsar drums, an Aurora bass ducked
// by a compressor keyed from the kick's own output, a Cirrus bell melody and a
// Nebula pad. Humanise loosens the keys and the melody on their MIDI tracks;
// Patina, Glue, Orbit and the limiter age and hold the mix, and three FX tracks
// (a room, a plate and a tape echo) give it space. With channel strip modules,
// automation, section markers and the kit's extra outputs in a closed folder.
// Built in code against the presets and parameters, so it stays playable as
// they change; `Northlight --demo-song <file>` writes it and opens it.

namespace northlight
{

core::Project makeDemoSong();

// Its tempo and length, for the tests.
constexpr double demoSongBpm = 75.0;
constexpr core::SamplePosition demoSongBarLength = 4 * 38400; // at 48 kHz
constexpr int demoSongBars = 38;                              // 36, a last chord and its tail

} // namespace northlight
