#pragma once

#include "engine/Insert.h"

#include <array>
#include <memory>
#include <string>

// Bloom: a reverb made as an effect rather than a room. A feedback delay network
// of eight modulated lines, mixed by a Householder matrix, with an allpass in each
// line for density, from a large hall to an absurd space with decays from under a
// second to a minute, and Freeze, which holds the tail for good and keeps it
// moving. Shimmer pitch-shifts the tail inside the loop, so each pass climbs (or
// falls) another interval; Swell feeds the input in through a rising cloud of
// taps, so the tail builds slowly like a reversed reverb; Gate cuts it a set time
// after the input stops; Duck lowers it under the dry input. Low cut and damping
// work inside the loop (the lows and highs decay faster), Tilt shapes the tail
// after it. A governor watches the network's energy and holds it below a ceiling,
// so no setting can run away. No latency. JUCE-free.

namespace northlight::plugins::bloom
{

inline const std::string type = "northlight.bloom";

enum Param
{
    decay,       // RT60 in seconds, minDecaySeconds..maxDecaySeconds (log)
    size,        // %: the lines' lengths, from scale minScale to maxScale (exponential)
    freeze,      // choice: Off, On (the tail holds for good; the input stays out)
    shimmer,     // %: how much of each pass is pitch-shifted
    pitch,       // choice: pitchSemitones (the last is both octaves)
    swell,       // ms: the input builds into the tail over this time; swellOffMs and below is off
    preDelay,    // ms, 0..maxPreDelayMs
    sync,        // choice: Off (preDelay in ms) or a note value (plugins/BeatSync.h)
    diffusion,   // %: the input diffusers and the allpass in each line
    modulation,  // %: the lines' modulation depth, from a slow chorus to warping
    modRate,     // Hz: the modulation's rate
    lowCut,      // Hz: the lows decay faster below this (in the loop)
    damping,     // Hz: the highs decay faster above this (in the loop)
    tilt,        // dB: the tail's tilt around tiltHz, after the loop (+ brighter)
    duck,        // %: how far the tail ducks under the dry input (100% = duckMaxDb)
    duckRelease, // ms: how quickly the tail comes back
    gate,        // ms: the tail is cut this long after the input stops; gateOffMs and below is off
    width,       // %: the tail's stereo width
    mix,         // % wet
    count
};

inline constexpr float minDecaySeconds = 0.4f;
inline constexpr float maxDecaySeconds = 60.0f;
inline constexpr float maxPreDelayMs = 500.0f;
// A synced pre-delay is held to this (a whole note at a slow tempo is long).
inline constexpr double maxSyncedPreDelaySeconds = 2.0;
inline constexpr float swellOffMs = 10.0f;
inline constexpr float maxSwellMs = 4000.0f;
inline constexpr float gateOffMs = 40.0f;
inline constexpr float maxGateMs = 2000.0f;
inline constexpr float duckMaxDb = 24.0f;
inline constexpr float tiltHz = 900.0f;
// The lines' lengths at size scale 1, in ms (mean 51.5 ms), and the size range.
inline constexpr std::array<float, 8> lineMs{31.7f, 37.3f, 41.9f, 47.1f, 53.3f, 59.9f, 67.1f, 73.9f};
inline constexpr float minScale = 0.25f, maxScale = 5.0f;
// The allpass in each line (ms, fixed: changing it would click).
inline constexpr std::array<float, 8> loopAllpassMs{3.1f, 4.3f, 5.7f, 6.9f, 8.3f, 9.7f, 11.3f, 12.9f};
// The shimmer's grain: each of its two read heads sweeps this window.
inline constexpr float shimmerWindowSeconds = 0.07f;
// The modulation's deepest sweep (seconds either side) and its fastest pitch
// deviation (a fraction: 0.06 is about a semitone), so a fast rate never turns
// into a siren.
inline constexpr float maxModSeconds = 0.015f;
inline constexpr float maxPitchDeviation = 0.06f;
// Frozen, the lines keep at least this much modulation so the tail never turns static.
inline constexpr float frozenModSeconds = 0.0006f;
// The governor's ceiling: the RMS of one line (about -6 dBFS).
inline constexpr float energyCeiling = 0.5f;

inline constexpr std::array<float, 6> pitchSemitones{-12.0f, 5.0f, 7.0f, 12.0f, 19.0f, 12.0f};
inline constexpr int bothOctaves = 5; // half the lines up an octave, half down

// The size parameter as a scale of lineMs, and as the room's size in metres (the
// mean line length at the speed of sound): what the card reads out.
float sizeScale(float percent) noexcept;
float sizeMetres(float percent) noexcept;
// The pitch ratio of line `line` for a pitch choice (both octaves: even lines up, odd down).
float pitchRatio(float choice, int line) noexcept;
// The pre-delay in seconds: the note value at the tempo when synced (held to
// maxSyncedPreDelaySeconds), else the ms.
double preDelaySeconds(float preDelayMs, float syncChoice, double tempoBpm) noexcept;
// The tail's RT60 at a frequency, from the loop's damping and low cut: the
// decay where the filters leave the signal alone, shorter where they act.
float decayAt(float hz, float decaySeconds, float sizePercent, float dampingHz, float lowCutHz) noexcept;

// What the card reads from each feedback frame: input and output peaks, the
// ducking (`reduction`, dB), the tail's RMS (`extra`, the wet signal before the
// mix) and the dry input's RMS (`extra2`). Tap 0 is the dry input, tap 1 the tail.
std::unique_ptr<engine::InsertProcessor> create();

} // namespace northlight::plugins::bloom
