#pragma once

#include "engine/Insert.h"
#include "plugins/EffectPresets.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

// Lumen: the electric piano bundled with Northlight (screens/plugins.md 7.4h,
// docs/plans/electric-piano.md), modelled, not sampled, on a tine piano. Each
// note is a hammer striking a tine: the hammer's felt is a nonlinear spring, so
// a harder strike is shorter and brighter as well as louder; the tine is four
// modes, its long fundamental (the tone bar) and three short, inharmonic beam
// modes (the bell); its tip moves in front of a magnetic pickup whose flux is a
// nonlinear function of where the tip is (the bark), and the output is the
// flux's rate of change. A felt damper stops a released note with a soft
// thump, and the action knocks. After the voices: a preamp with bass, treble
// and drive, a stereo tremolo (an auto-pan, as on the suitcase models, or
// amplitude), a phaser and a chorus. The mod wheel adds to the tremolo's depth;
// channel pressure gives the shared vibrato. Up to 64 voices. JUCE-free;
// real-time safe after prepare().

namespace northlight::plugins::lumen
{

inline const std::string type = "northlight.lumen";

enum Param
{
    hardness,       // % the hammers' felt (0 soft and dark .. 100 hard and bright)
    velocity,       // % how much velocity changes the strike (0 even .. 100 very dynamic)
    noise,          // % the action's knocks and the damper's thump
    bell,           // % the tine's beam modes (the bell in the attack)
    decay,          // % of the natural decay times (25 .. 400)
    damper,         // % how quickly a released note stops (0 slow .. 100 fast)
    age,            // % each note's fixed detune, up to 5 cents
    bark,           // % how close the tines sit to the pickups
    symmetry,       // % the tines' offset from the pickups' centre (0 centred: second harmonic)
    bass,           // dB, the preamp's low shelf
    treble,         // dB, the preamp's high shelf
    drive,          // % the preamp's drive
    tremoloMode,    // Off, Pan, Amplitude
    tremoloRate,    // Hz
    tremoloSync,    // note value (sync::noteValues), 0 off
    tremoloDepth,   // %
    tremoloShape,   // % sine .. square
    phaserRate,     // Hz
    phaserDepth,    // %
    phaserFeedback, // %
    phaserMix,      // % (0: off)
    chorusRate,     // Hz
    chorusDepth,    // %
    chorusMix,      // % (0: off)
    voices,         // 8 .. 64
    tune,           // Hz, A3 (MIDI 69)
    level,          // dB
    count
};

constexpr int maxVoices = 64;
constexpr int modeCount = 4;
// The 73 keys the model is voiced across (E0 to E6); notes outside extend the curves.
constexpr int lowestKey = 28;
constexpr int highestKey = 100;

std::unique_ptr<engine::InsertProcessor> create();
std::vector<engine::ParameterInfo> parameterInfo();
const std::vector<EffectPreset>& presets();

// What a note is made of, for the voice, the tests and the editor. Times in
// seconds; ratios to the fundamental; weights at the tine's tip (before Bell).
struct NoteModel
{
    double hz = 440.0;
    std::array<double, modeCount> ratio{};
    std::array<double, modeCount> t60{};    // while the key is held
    std::array<double, modeCount> weight{}; // the mode's share of the tip's motion
    double damperT60 = 0.2;                 // once released
    double contactSeconds = 0.001;          // the hammer's contact at full strike
    double register01 = 0.5;                // 0 at E0, 1 at E6
    double swing = 1.0;                     // how far the tip travels for a strike (bass tines swing further)
    double levelTrim = 1.0;                 // evens the keyboard's level at a full strike
};

// `values`: the parameters (count of them). `detuneCents`: the note's age detune.
NoteModel noteModel(int note, const float* values, double detuneCents = 0.0) noexcept;

// The pickup's flux for a tip position (in the model's units, a full strike
// reaching about 0.5), at a Bark and Symmetry (0..1): a bell-shaped curve the
// tip moves across. Normalised so its steepest slope is 1.
double pickupFlux(double x, float barkAmount, float symmetryAmount) noexcept;

// The hammer's speed (0..1) for a MIDI velocity at a Velocity setting (0..1).
double strikeSpeed(int midiVelocity, float velocityAmount) noexcept;

} // namespace northlight::plugins::lumen
