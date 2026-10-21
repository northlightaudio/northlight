#pragma once

#include "engine/Insert.h"

#include <memory>
#include <string>
#include <vector>

// Nebula: the pad synth bundled with Northlight (07-plugins.md 7.4d), for slow,
// airy, ethereal sounds. Each note is a cloud of partials (up to 24, each a
// pair of detuned sines drifting on their own and panned across the field),
// shaped by brightness, odd/even balance, stretch (towards bells) and a vowel
// formant, with a halo an octave up, a sub and breath noise; a slow envelope,
// motion (an LFO sweeping the spectrum), and Space, a large reverb, after a
// gentle tone filter. Eight voices. JUCE-free; real-time safe after prepare().

namespace northlight::plugins::nebula
{

inline const std::string type = "northlight.nebula";

enum Param
{
    partials,   // 4..24
    brightness, // 0..1: the spectrum's tilt
    oddEven,    // -1 (odd only, hollow) .. 1 (even partials lifted)
    stretch,    // 0..1: partials pulled sharp, towards a bell
    vowel,      // 0..1: the formant's place, 250 Hz to 3 kHz
    formant,    // 0..1: how much the formant shapes the spectrum
    drift,      // 0..1: how far each partial wanders (cents)
    spread,     // cents between each partial's two sines
    width,      // % how far the partials pan apart
    halo,       // 0..1: partials an octave up
    sub,        // 0..1
    breath,     // 0..1: noise around the note
    attack,     // ms
    decay,
    sustain, // 0..1
    release,
    motionRate,  // Hz
    motionDepth, // 0..1: the spectral sweep
    space,       // 0..1: the reverb's size
    spaceMix,    // 0..1
    tone,        // Hz: a gentle low-pass before the space
    octave,
    velocity, // 0..1
    level,    // dB
    count
};

constexpr int voiceCount = 8;
constexpr int maxPartials = 24;

std::unique_ptr<engine::InsertProcessor> create();
std::vector<engine::ParameterInfo> parameterInfo();

struct Preset
{
    std::string name;
    std::vector<float> values;
};
const std::vector<Preset>& presets();

// The level (0..1, the loudest 1) and frequency ratio of each partial for a
// note at `noteHz`, as the voices compute them with the motion at `motion`
// (-1..1): for drawing the spectrum. Halo partials follow the main ones.
struct Partial
{
    float ratio = 1.0f;
    float level = 0.0f;
    bool halo = false;
};
std::vector<Partial> spectrum(const float* params, float noteHz, float motion = 0.0f);

} // namespace northlight::plugins::nebula
