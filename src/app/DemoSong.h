#pragma once

#include "core/Model.h"

// The demo song (README): "Northern Drift", a short piece in A minor at 120 bpm
// written in Aural with its own instruments and effects: Pulsar drums, an
// Aurora bass and keys, a Nebula pad, Starbit's chord arpeggio and lead, two FX
// tracks (Hall, Echo), inserts, channel strip modules, automation and section
// markers. Built in code against the instruments' presets and parameters, so it
// stays playable as they change; `Aural --demo-song <file>` writes it and opens it.

namespace aural
{

core::Project makeDemoSong();

} // namespace aural
