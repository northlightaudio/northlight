#pragma once

#include "engine/Insert.h"
#include "plugins/EffectPresets.h"
#include "plugins/fm/Fm.h"

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Cirrus: the FM synth bundled with Northlight (docs/plans/cirrus.md). Four
// operators in eight algorithms, phase modulation as the DX family did it, with
// feedback on operator 4. Each operator: a ratio (or a fixed frequency), a wave,
// a level, an ADSR envelope, velocity and key scaling. Then a pitch envelope, an
// LFO to pitch and to the modulators, two macros (Tone: every modulator at once;
// Time: every envelope at once), Mono with glide, a high cut, width and level.
// Sixteen voices, run at twice the rate on High quality. JUCE-free; real-time
// safe after prepare().

namespace northlight::plugins::cirrus
{

inline const std::string type = "northlight.cirrus";

// Each operator's parameters, in this order, operator 1 first.
enum OpField
{
    ratio,    // choice: 0.5, 1, 2 ... 32
    fine,     // % added to the ratio, 0..99
    detune,   // cents
    fixed,    // 0 off (the ratio follows the note), 1 on (a fixed frequency)
    fixedHz,  // Hz, while fixed
    wave,     // fm::Wave
    level,    // 0..99: a modulator's depth or a carrier's loudness
    attack,   // ms
    decay,    // ms
    sustain,  // 0..1
    release,  // ms
    velocity, // 0..1: how much velocity scales the level
    keyScale, // -1..1: the level falls (or rises) above middle C, 6 dB an octave at the ends
    fieldCount
};

constexpr int opParam(int op, OpField field) noexcept
{
    return op * fieldCount + field;
}

// The rest, after the four operators' blocks; the algorithm (0..7: algorithms 1
// to 8) is the first.
enum Param
{
    algorithm = fm::operatorCount * fieldCount,
    feedback,      // 0..1, operator 4 into itself
    voiceMode,     // 0 poly, 1 mono (legato)
    glide,         // ms, mono only
    pitchEnvelope, // semitones at the note's start
    pitchTime,     // ms for the pitch envelope to settle
    lfoShape,      // 0 sine, 1 triangle, 2 saw, 3 square, 4 random
    lfoRate,       // Hz
    lfoSync,       // 0 off (the rate in Hz), else a note value (plugins/BeatSync.h)
    lfoPitch,      // cents
    lfoTone,       // 0..1: the LFO moves the modulators, up to +-12 dB
    tone,          // %, -100..100: every modulator, +-12 dB
    time,          // %, -100..100: every envelope, a quarter to four times as long
    highCut,       // Hz
    width,         // %: carriers spread across the stereo field
    quality,       // 0 Eco (1x), 1 High (2x)
    outputLevel,   // dB
    count
};

constexpr int voiceCount = 16;
constexpr int ratioChoiceCount = 33; // 0.5, then 1 to 32

// A ratio choice as a multiple of the note's frequency.
inline double ratioOf(float choice) noexcept
{
    const int i = choice < 0.5f ? 0 : static_cast<int>(choice + 0.5f);
    return i <= 0 ? 0.5 : static_cast<double>(i > 32 ? 32 : i);
}

// What the editor reads from a live Cirrus (screens/plugins.md 7.4h), written once
// per control interval for the newest sounding voice while an editor watches:
// its note (-1 when none sounds), each operator's level now (0..1: its envelope
// times its level after velocity and key scale) and the depth, the summed
// modulation index of its modulators in radians (after Tone, the LFO and the
// limiter). Null for another processor.
struct Activity
{
    std::atomic<int> note{-1};
    std::array<std::atomic<float>, fm::operatorCount> level{};
    std::atomic<float> depth{0.0f};
};
const Activity* activity(const engine::InsertProcessor& processor) noexcept;

std::unique_ptr<engine::InsertProcessor> create();
std::vector<engine::ParameterInfo> parameterInfo();

// The factory sounds, by name: each a full set of parameter values.
const std::vector<EffectPreset>& presets();

} // namespace northlight::plugins::cirrus
