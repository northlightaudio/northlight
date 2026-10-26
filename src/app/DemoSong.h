#pragma once

#include "core/Model.h"

#include <cstdint>
#include <string>
#include <vector>

// The demo song (README): "Northern Drift", a short piece in A minor at 120 bpm
// written in Northlight with its own instruments and effects: Pulsar drums (its
// extra outputs in a closed folder under the kit), an Aurora bass and keys, a
// Nebula pad, Starbit's chord arpeggio and lead, a sung voice through the verse
// (an audio clip, so the audio editor and pitch editing have something to show),
// two FX tracks (Hall, Echo), inserts, channel strip modules, automation and
// section markers. Built in code against the instruments' presets and
// parameters, so it stays playable as they change; `Northlight --demo-song
// <file>` writes it and opens it.

namespace northlight
{

// With `mediaFolder` (UTF-8, the project's folder) the voice is rendered there as
// "Northern Drift - Voice.wav" and placed on its track; without one (or when the
// file cannot be written) the song has no voice track.
core::Project makeDemoSong(const std::string& mediaFolder = {});

// The voice: a synthetic singer (a glottal pulse through vowel formants, with
// scoops, slides, vibrato and breaths, a few notes a little off pitch) singing the
// verse melody, mono at `sampleRate`. Pitch-analysable, which the tests rely on.
std::vector<float> demoVoice(double sampleRate);
// The voice's length in samples at 48 kHz on the song's timeline (eight bars).
constexpr std::int64_t demoVoiceLength = 8 * 4 * 24000;

} // namespace northlight
