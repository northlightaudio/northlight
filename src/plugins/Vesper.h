#pragma once

#include "engine/Insert.h"

#include <memory>
#include <string>

// Vesper: a reverb for vocals that listens to the singing (docs/plans/vesper.md).
// The Listener (plugins/VocalListener) hears the dry vocal every 2.5 ms, and
// Vesper shapes the reverb by it:
//
//  - the send: consonants and sibilance (Clarity) and breaths (Breaths) are
//    turned down on their way into the reverb, never in the dry signal;
//  - the pre-delay: a time, a note value, or Auto, which follows the singing
//    rate so the tail starts after the consonant that opens each word;
//  - the space: a plate, a chamber or a hall, each its own tuning of the shared
//    feedback network (plugins/FeedbackNetwork); a new space builds while the
//    old one rings out;
//  - the tail: Follow's high-pass tracks the sung note, Mud cuts a build-up at
//    250-500 Hz, Centre takes the tail's middle down so it sits round the voice;
//  - Duck lowers the tail while the singer sings (keyed by what the Listener
//    hears, not the level), Release brings it back (in ms or a note value), and
//    Ends lifts and lengthens it at each line end.
//
// No latency. JUCE-free.

namespace northlight::plugins::vesper
{

inline const std::string type = "northlight.vesper";

enum Param
{
    space,        // choice: Plate, Chamber, Hall
    decay,        // RT60 in seconds, minDecaySeconds..maxDecaySeconds (log)
    size,         // %: the space's lines' lengths, minSizePercent..maxSizePercent
    preDelay,     // ms, 0..maxPreDelayMs (when Auto is off and not synced)
    preDelayAuto, // choice: Off, On (the pre-delay follows the singing rate)
    preDelaySync, // choice: Off (ms) or a note value (plugins/BeatSync.h); Auto wins
    clarity,      // %: how far consonants and sibilance are turned down in the send
    breaths,      // %: how far breaths are turned down in the send
    follow,       // octaves under the sung note for the tail's high-pass; followOffOctaves and below is off
    mud,          // %: the dynamic cut at 250-500 Hz in the tail
    lowCut,       // Hz: the lows decay faster below this (in the loop)
    damping,      // Hz: the highs decay faster above this (in the loop)
    width,        // %: the tail's side, 0..150
    centre,       // %: how far the tail's middle is taken down
    duck,         // dB: how far the tail goes down while the singer sings
    release,      // ms: how quickly the tail comes back in a gap (when not synced)
    releaseSync,  // choice: Off (ms) or a note value
    ends,         // %: how much the tail lifts and lengthens at a line end
    gap,          // ms: how long a pause ends a line
    mix,          // % wet
    output,       // dB
    count
};

inline constexpr float minDecaySeconds = 0.3f, maxDecaySeconds = 8.0f;
inline constexpr float minSizePercent = 50.0f, maxSizePercent = 150.0f;
inline constexpr float maxPreDelayMs = 250.0f;
inline constexpr double maxSyncedPreDelaySeconds = 2.0;
inline constexpr float followOffOctaves = 0.45f, minFollowOctaves = 0.4f, maxFollowOctaves = 2.0f;
inline constexpr float maxDuckDb = 18.0f;
inline constexpr float sendCutDb = 24.0f; // Clarity or Breaths at 100%
inline constexpr float mudCutDb = 6.0f;   // Mud at 100%, at the most build-up
inline constexpr float endsLiftDb = 6.0f; // Ends at 100%
inline constexpr float endsDecayRatio = 2.0f;

// Auto pre-delay for a singing rate (syllables a second): about 90 ms for slow,
// held lines down to 25 ms for fast ones; 60 ms before anything is sung.
float autoPreDelayMs(float syllablesPerSecond) noexcept;
// The pre-delay in seconds: Auto, else the note value at the tempo (held to
// maxSyncedPreDelaySeconds), else the ms.
double preDelaySeconds(
    float preDelayMs, float autoChoice, float syncChoice, float syllablesPerSecond, double tempoBpm) noexcept;
// The tail's high-pass for a held pitch: `octaves` under it, or 0 (off) when
// Follow is off or nothing has been sung.
float followCutoffHz(float heldPitchHz, float octaves) noexcept;

// What a card reads from each feedback frame: input and output peaks, the
// ducking (`reduction`, dB), the tail's RMS (`extra`, before the mix) and the
// dry input's RMS (`extra2`). Tap 0 is the dry input, tap 1 the tail.
std::unique_ptr<engine::InsertProcessor> create();

} // namespace northlight::plugins::vesper
