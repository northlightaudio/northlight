#pragma once

#include "engine/Insert.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

// Starbit: the chiptune synth bundled with Northlight (screens/plugins.md 7.4e), after the
// sound chips of 8-bit consoles. A pulse with the classic duty cycles (and a
// duty sweep), a 4-bit stepped triangle and saw, LFSR noise (long or short,
// metallic) and a 32-step 4-bit wave you draw; a volume envelope in 16 steps;
// tracker arpeggios (the held notes, or a chord shape); vibrato with a delay,
// a pitch sweep into each note and glide; mono or four voices; and the lo-fi
// end: bit depth, sample rate and a chip echo. JUCE-free; real-time safe after
// prepare().

namespace northlight::plugins::starbit
{

inline const std::string type = "northlight.starbit";

enum Wave
{
    pulse,
    triangle,
    saw,
    noise,
    custom,
};

enum Arp
{
    arpOff,
    arpChord, // cycle the held notes (mono)
    arpOctave,
    arpMajor,
    arpMinor,
    arpPower,
    arpMajor7,
    arpMinor7,
};

constexpr int waveSteps = 32; // the custom wave: 32 steps of 0..15

enum Param
{
    wave,      // Wave
    duty,      // 0 12.5%, 1 25%, 2 50%, 3 75%
    dutySweep, // 0..1: how fast the duty cycles through the four
    noiseMode, // 0 long, 1 short (metallic)
    octave,
    coarse, // semitones
    attack, // ms
    decay,
    sustain, // 0..1 (16 steps)
    release,
    arp,                                 // Arp
    arpRate,                             // Hz (steps a second)
    vibratoDepth,                        // semitones
    vibratoRate,                         // Hz
    vibratoDelay,                        // ms
    sweep,                               // semitones the note starts from
    sweepTime,                           // ms to reach the note
    glide,                               // ms
    voices,                              // 0 mono, 1 poly (four)
    bits,                                // 2..16
    rate,                                // Hz: the sample-and-hold rate
    echo,                                // 0..1
    velocity,                            // 0..1
    level,                               // dB
    firstWaveStep,                       // the custom wave's 32 steps follow
    arpSync = firstWaveStep + waveSteps, // 0 off (the rate in Hz), else a note value a step
    count
};

constexpr int polyVoices = 4;

std::unique_ptr<engine::InsertProcessor> create();
std::vector<engine::ParameterInfo> parameterInfo();

struct Preset
{
    std::string name;
    std::vector<float> values;
};
const std::vector<Preset>& presets();

// The arpeggio's steps (semitone offsets) for a shape (empty for off or chord).
std::vector<int> arpSteps(int shape);

// One cycle of the current wave as 32 levels (0..15), for drawing it (the
// noise draws as a fixed run of its bits).
std::array<int, waveSteps> waveShape(const float* params);

} // namespace northlight::plugins::starbit
