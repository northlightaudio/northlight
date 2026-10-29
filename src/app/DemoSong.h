#pragma once

#include "core/Model.h"

// The demo song, a test project for development (benchmarks, captures, bounce
// tests), not shipped or promoted: "Harbour Lights", a neo-soul piece in D major
// at 90 bpm played only by the built-in synths (Pulsar drums, Aurora bass, keys
// and lead, a Nebula pad, a Starbit arpeggio) through most of the built-in
// effects: Glue, Patina, Tide, Widener, Flare into Corona into Comet, Orbit, the
// Exciter, a bass compressor keyed from the kick's own output, and three FX
// tracks (Plate, a Bloom shimmer, a Tape Echo). With channel strip modules,
// automation, section markers and the kit's extra outputs in a closed folder.
// Built in code against the presets and parameters, so it stays playable as
// they change; `Northlight --demo-song <file>` writes it and opens it.

namespace northlight
{

core::Project makeDemoSong();

// Its tempo and length, for the tests.
constexpr double demoSongBpm = 90.0;
constexpr core::SamplePosition demoSongBarLength = 4 * 32000; // at 48 kHz
constexpr int demoSongBars = 38;                              // 36, a last chord and its tail

} // namespace northlight
