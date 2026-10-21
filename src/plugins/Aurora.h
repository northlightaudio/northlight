#pragma once

#include "engine/Insert.h"

#include <memory>
#include <string>
#include <vector>

// Aurora: the polysynth bundled with Northlight (screens/plugins.md 7.5). Two morphing
// oscillators (sine, triangle, saw, square) with up to seven-voice unison,
// a sub and noise; a drivable multimode filter with its own envelope and key
// tracking; amp and filter envelopes; an LFO to pitch, cutoff and level;
// glide; and Shimmer, a stereo chorus on the output. Twelve voices.
// JUCE-free; real-time safe after prepare().

namespace northlight::plugins::aurora
{

inline const std::string type = "northlight.aurora";

enum Param
{
    osc1Shape, // 0..3: sine, triangle, saw, square (morphing between)
    osc1Octave,
    osc1Level,
    osc2Shape,
    osc2Octave,
    osc2Semi,
    osc2Fine, // cents
    osc2Level,
    unison, // voices per oscillator, 1..7
    spread, // detune of the outermost unison voices, cents
    width,  // % stereo spread of the unison voices
    sub,    // a sine an octave under oscillator 1
    noise,
    filterMode, // 0 low-pass 12, 1 low-pass 24, 2 band-pass, 3 high-pass
    cutoff,     // Hz
    resonance,  // 0..1
    drive,      // 0..1
    filterEnv,  // -1..1: the filter envelope's reach, +-5 octaves
    keyTrack,   // 0..1: the cutoff follows the note
    ampAttack,  // ms
    ampDecay,
    ampSustain, // 0..1
    ampRelease,
    filterAttack,
    filterDecay,
    filterSustain,
    filterRelease,
    lfoShape,  // 0 sine, 1 triangle, 2 saw, 3 square, 4 random
    lfoRate,   // Hz
    lfoPitch,  // cents
    lfoCutoff, // octaves
    lfoAmp,    // 0..1 tremolo depth
    glide,     // ms
    shimmer,   // 0..1 the chorus
    velocity,  // 0..1 how much velocity shapes level and filter
    level,     // dB
    count
};

constexpr int voiceCount = 12;
constexpr int maxUnison = 7;

std::unique_ptr<engine::InsertProcessor> create();
std::vector<engine::ParameterInfo> parameterInfo();

// The factory sounds, by name: each a full set of parameter values.
struct Preset
{
    std::string name;
    std::vector<float> values;
};
const std::vector<Preset>& presets();

// One cycle of a morphing shape (0..3) at phase 0..1, for drawing it.
float shapeAt(float shape, float phase) noexcept;

} // namespace northlight::plugins::aurora
