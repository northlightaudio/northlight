#pragma once

#include "engine/Insert.h"

#include <memory>
#include <string>

// The guitar pedals: five stompboxes voiced for guitar and bass rather than as
// general-purpose effects, each with the few controls a pedal has. They sit in the
// Guitar group of the effects menu with Corona. Every knob but the tuner's reads
// 0..10 as a pedal's does. JUCE-free; real-time safe after prepare(); none adds
// latency.
//
//  - Gravity, a compressor / sustainer: an OTA compressor's squash and bloom.
//  - Flare, a distortion: a hard-clipping op-amp distortion with three clippers.
//  - Comet, a delay: a bucket-brigade echo whose repeats darken and smear.
//  - Tide, a chorus: a bucket-brigade chorus that leaves the low end dry.
//  - Polaris, a tuner: a chromatic tuner whose footswitch mutes the output.

namespace northlight::plugins
{

// ----- Gravity ---------------------------------------------------------------------------------
//
// The signal through a feed-forward compressor with a high ratio and a low
// threshold, both rising with Sustain, and make-up that grows with them so a
// played note keeps its level as it decays: the long sustain of a squeezed clean
// guitar. The detector sees the signal through a bass cut (so a bass's lows do not
// pump it) and releases in two stages (fast back from a pick, slow from held
// compression). Attack sets how much of the pick gets through before the gain
// comes down. A downward expander below the noise floor keeps the make-up from
// lifting the hiss between notes. The compressed signal goes through the OTA's
// soft saturation and a gentle treble roll-off; Blend mixes it with the dry
// signal (parallel compression, a bass player's friend) and Level sets the output.
namespace gravity
{
inline const std::string type = "northlight.gravity";

enum Param
{
    sustain, // 0..10
    attack,  // 0..10: 0 the fastest (squashes the pick), 10 the slowest (lets it through)
    blend,   // 0..10: 0 dry, 10 all compressed
    level,   // dB
    count
};

// What Sustain sets: the threshold (dBFS), the ratio and the make-up (dB).
struct Curve
{
    float thresholdDb, ratio, makeupDb;
};
Curve curveFor(float sustain) noexcept;
// Attack's time constant in ms.
float attackMs(float attack) noexcept;
// The compressor's static gain (dB) for an input level (dBFS) at a Sustain.
float staticGainDb(float sustain, float inputDb) noexcept;

// Each feedback frame: input and output peaks, the gain reduction (dB) and, as
// extra, the detector's level (linear).
std::unique_ptr<engine::InsertProcessor> create();
} // namespace gravity

// ----- Flare -----------------------------------------------------------------------------------
//
// A hard-clipping op-amp distortion. The op-amp stage's gain (up to 66 dB, Drive
// on an audio taper) is set by two RC legs, so it rises with frequency: the lows
// stay tight under heavy drive. Its gain-bandwidth product rounds the highs off
// more the higher the gain, which is what keeps it smooth instead of fizzy. Then
// a pair of clipping diodes to ground: silicon (tight and compressed), LED (more
// headroom, louder and more open) or none (the op-amp clips on its rails: loud,
// gritty, closest to a fuzz). A one-pole Tone filter, a gentle roll-off above
// 12 kHz for the fizz the clippers leave, and the output Level, normalised per
// clipper. The nonlinear part runs at four times the rate.
namespace flare
{
inline const std::string type = "northlight.flare";

enum Param
{
    drive, // 0..10
    tone,  // 0..10: 0 dark, 10 bright
    level, // 0..10
    clip,  // choice: Silicon, LED, Open
    count
};

enum Clipper
{
    silicon = 0,
    led = 1,
    open = 2
};

// The op-amp stage's gain at high frequencies (dB) for a Drive setting.
float gainDb(float drive) noexcept;
// The Tone filter's corner (Hz).
float toneHz(float tone) noexcept;
// A clipper's transfer curve (input and output in volts, the pedal's 9 V rails).
float clipCurve(int clipper, float volts) noexcept;

// Each feedback frame: input and output peaks and, as extra, how far the
// clippers are driven (the op-amp's peak output over the clipping voltage).
std::unique_ptr<engine::InsertProcessor> create();
} // namespace flare

// ----- Comet -----------------------------------------------------------------------------------
//
// A bucket-brigade delay: a line of 4096 stages clocked so it holds the Time, so a
// longer delay is clocked slower and its repeats are darker (the clock's
// anti-alias filters close in on its Nyquist). Each pass round the loop goes
// through those filters, a bass cut and the compander's soft limit, so repeats
// darken and thin as they fall away, and at the top of Repeats the loop runs
// away into a warm self-oscillation that the limit keeps in bounds. Turning Time
// glides the clock, bending the pitch of the repeats in flight, as the real
// thing does. Mod adds the clock's slow wobble to the repeats. The dry signal
// passes untouched; Mix sets the echoes' level.
namespace comet
{
inline const std::string type = "northlight.comet";

enum Param
{
    time,    // ms
    repeats, // 0..10
    mix,     // 0..10
    mod,     // Off / On
    count
};

inline constexpr float minTimeMs = 40.0f, maxTimeMs = 800.0f;
inline constexpr int bucketStages = 4096;

// The repeats' bandwidth (Hz) at a delay time: the clock's anti-alias corner.
float bandwidthHz(float timeMs) noexcept;
// The loop gain Repeats sets (above 1 at the top: it runs away into the limit).
float feedbackGain(float repeats) noexcept;

std::unique_ptr<engine::InsertProcessor> create();
} // namespace comet

// ----- Tide ------------------------------------------------------------------------------------
//
// A bucket-brigade chorus: a short delay (a few ms) swept by a triangle LFO,
// band-limited and softly saturated as a BBD is, mixed with the dry signal. The
// wet signal is high-passed above the bass's fundamentals, so on bass (or a low
// guitar riff) the low end stays solid and only the top shimmers. Its outputs are
// stereo, the right side swept in the opposite phase, so the chorus opens out
// across a pair of amps and still sums to mono cleanly. Vibrato takes the dry
// signal out, leaving only the pitch wobble.
namespace tide
{
inline const std::string type = "northlight.tide";

enum Param
{
    rate,  // 0..10
    depth, // 0..10
    mix,   // 0..10
    mode,  // Chorus / Vibrato
    count
};

// The LFO's rate (Hz) for a Rate setting.
float rateHz(float rate) noexcept;
// The sweep (ms either side of the centre delay) for a Depth setting.
float sweepMs(float depth) noexcept;
inline constexpr float centreMs = 7.0f;
inline constexpr float wetHighPassHz = 160.0f;

std::unique_ptr<engine::InsertProcessor> create();
} // namespace tide

// ----- Polaris ---------------------------------------------------------------------------------
//
// A chromatic tuner for guitar and bass, from a five-string bass's low B (31 Hz)
// to the top of a guitar's neck. The input (mono) is low-passed and decimated to
// about 8 kHz, and every 30 ms or so the last 128 ms are analysed with YIN (the
// cumulative mean normalised difference), the work spread over the blocks that
// follow so no block carries all of it. The footswitch mutes the output, as a
// tuner pedal's does, so a player can tune silently; the tuner reads either way.
namespace polaris
{
inline const std::string type = "northlight.polaris";

enum Param
{
    reference, // Hz: what A4 is tuned to
    mute,      // Off / On: the footswitch
    count
};

inline constexpr float minReferenceHz = 430.0f, maxReferenceHz = 450.0f;
inline constexpr float lowestHz = 28.0f, highestHz = 1400.0f;

// The pitch found in a window of samples at `sampleRate` (Hz), 0 for none (too
// quiet, or no clear period); `clarity` (0..1) how periodic it was. Not real
// time (the processor runs the same analysis in pieces).
float detectPitch(const float* samples, int count, double sampleRate, float* clarity = nullptr);

// A frequency as the nearest note at a reference: its MIDI number and how far off
// it is in cents (-50..+50).
struct Note
{
    int midi = 0;
    float cents = 0.0f;
};
Note noteFor(float hz, float referenceHz) noexcept;
// A note's name in sharps ("A", "C#") and its octave (A4 = MIDI 69).
const char* noteName(int midi) noexcept;
int octaveOf(int midi) noexcept;

// What the tuner reads now (the controller thread, any time): the pitch (0 for
// none), its clarity and the input's level (linear peak).
struct Reading
{
    float hz = 0.0f;
    float clarity = 0.0f;
    float level = 0.0f;
};
// The reading of a Polaris processor (none for any other processor).
bool readingOf(const engine::InsertProcessor* processor, Reading& out) noexcept;

std::unique_ptr<engine::InsertProcessor> create();
} // namespace polaris

} // namespace northlight::plugins
