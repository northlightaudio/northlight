#pragma once

#include "engine/Insert.h"

#include <array>
#include <memory>
#include <string>

// The Exciter: adds harmonics to a sound that lacks them, in the band above a
// crossover, and psychoacoustic bass below a second one, so a flat source gains
// presence, air or weight without an EQ boost. The band is normalised to a fixed
// level before it drives the curve, so the character depends on Drive alone and
// the harmonics stay a set distance under the band at any input level (Amount);
// below a floor nothing is excited, so hiss is not brought up. Dynamics tilt the
// amount toward the loud or the quiet moments, Transients accent onsets, Stereo
// spreads the harmonics to the sides without changing the mono sum, and Auto
// gain matches the output's loudness to the input's for an honest comparison.
// Each curve runs at twice the rate with first-order antiderivative
// anti-aliasing; only the generated harmonics go through it, so the dry signal
// is untouched and there is no latency. JUCE-free.

namespace northlight::plugins::exciter
{

inline const std::string type = "northlight.exciter";

enum Param
{
    frequency,     // Hz, the crossover above which harmonics are generated
    drive,         // dB into the curve (the character: low drive gives low orders only)
    character,     // choice: Character
    amount,        // %, the harmonics against the band: amountGain (100% is +6 dB)
    focus,         // %, -100 (dark) .. +100 (edge): a tilt on the harmonics
    dynamics,      // %, -100 (fill the quiet moments) .. +100 (push the loud ones)
    transients,    // %, extra harmonics on onsets
    stereo,        // %, decorrelated harmonics on the sides
    bassFrequency, // Hz, the low band the bass harmonics come from
    bass,          // %, bass harmonics against the low band: bassGain
    mix,           // %, of everything added
    output,        // dB
    autoGain,      // choice: Off, On (loudness matched to the input)
    count
};

enum Character
{
    tube,  // biased tanh: second and third harmonics, warm
    tape,  // tanh: odd harmonics, an edge
    air,   // a soft square law: mostly the octave, placed an octave above the crossover
    crisp, // a soft full-wave rectifier: a dense even series, bright
    characterCount
};

inline constexpr float minFrequency = 700.0f, maxFrequency = 16000.0f;
inline constexpr float minBassFrequency = 30.0f, maxBassFrequency = 250.0f;
inline constexpr float maxDriveDb = 24.0f;
// The band is brought to this RMS before the drive.
inline constexpr float referenceRms = 0.25f;
// The normalisation lifts a quiet band by at most this much.
inline constexpr float maxNormaliseDb = 48.0f;
// A band below gateFloorDb (RMS, dBFS) excites nothing; the excitation fades in
// over gateRangeDb above it.
inline constexpr float gateFloorDb = -84.0f, gateRangeDb = 18.0f;
// The harmonics never rise past this far over the band, whatever the dynamics.
inline constexpr float maxHarmonicsDb = 6.0f;
// Dynamics: dB of change in the amount per dB the band's level is away from its
// 600 ms average, at 100%; limited to the range below.
inline constexpr float dynamicsSlope = 0.75f;
inline constexpr float dynamicsMinDb = -18.0f, dynamicsMaxDb = 12.0f;
// Transients: up to this much more on an onset at 100%.
inline constexpr float transientMaxDb = 15.0f;
// Auto gain only turns down (an exciter adds), by at most this much.
inline constexpr float autoGainRangeDb = 24.0f;
// Stereo at 100% puts the decorrelated copy this far (linear) against the harmonics: 3 dB under.
inline constexpr float stereoDepth = 0.7071f;
// The levels the card reads are RMS over this time constant.
inline constexpr float rmsSeconds = 0.05f;

// The harmonics' level against the band (linear) for an Amount (or Bass) in %:
// 2 (a / 100)^2, so 50% sits 6 dB under the band and 100% 6 dB over it.
float amountGain(float percent) noexcept;
// The same for Bass: (a / 100)^2, so 100% matches the low band.
float bassGain(float percent) noexcept;
// A character's static curve (x is the driven band) and its antiderivative.
double curve(int character, double x) noexcept;
double curveIntegral(int character, double x) noexcept;

// What the card reads from each feedback frame: input and output peaks, the
// auto gain's reduction (dB), the input's RMS (`extra`) and the RMS of what is
// added (`extra2`, after Mix, before Output and auto gain). Tap 0 is the input,
// tap 1 what is added (both as mono sums, so Stereo's side-only part does not
// show). The processor keeps measuring while bypassed when an editor is watching.
std::unique_ptr<engine::InsertProcessor> create();

} // namespace northlight::plugins::exciter
