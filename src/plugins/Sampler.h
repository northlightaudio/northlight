#pragma once

#include "engine/Insert.h"
#include "plugins/SampleLoader.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// The Sampler: a polyphonic player of one sample bundled with Northlight (screens/plugins.md
// 7.4f). A root note and a key range, tune, velocity to level, an ADSR on the
// amplitude, start and end points, a forward loop with a crossfade, one-shot
// playback, a resonant low-pass with the envelope on its cutoff, a polyphony
// limit and glide. Pitch is 4-point Hermite interpolation at the voice's exact
// position, so a sample at another rate plays at its own pitch.
//
// The sample is a file path kept in the insert's state (the model's
// Insert::state), so it saves with the project and undoes like any edit;
// everything else is in the parameter list, so it automates. JUCE-free:
// decoding is the app's, through the Loader the factory is given. setState()
// runs the loader on the controller thread and hands the processor an immutable
// buffer through an epoch-retired publisher (rt::SnapshotPublisher); the
// real-time thread only reads it. Real-time safe after prepare().

namespace northlight::plugins::sampler
{

inline const std::string type = "northlight.sampler";

// Sample, Loader and the state encoding are in plugins/SampleLoader.h.

enum Param
{
    root,      // the note the sample plays at its own pitch
    keyLow,    // the lowest note it answers
    keyHigh,   // the highest
    tune,      // semitones
    fine,      // cents
    velocity,  // 0..1: how much the velocity sets the level
    attack,    // ms
    decay,     // ms
    sustain,   // 0..1
    release,   // ms
    start,     // 0..1 of the sample: where a note starts
    end,       // 0..1: where it stops
    loop,      // 0 off, 1 on
    loopStart, // 0..1
    loopEnd,   // 0..1
    loopFade,  // ms: the crossfade into the loop's end
    oneShot,   // 0 off, 1 on: play to the end, note-offs ignored
    cutoff,    // Hz (20 kHz with no envelope: the filter is out)
    resonance, // 0..1
    filterEnv, // octaves the envelope moves the cutoff
    voices,    // the polyphony limit
    glide,     // ms from the last note's pitch
    level,     // dB
    count
};

constexpr int maxVoices = 32;
// The voices a processor keeps: the limit plus room for stolen voices to fade.
constexpr int voicePool = maxVoices + 8;

std::unique_ptr<engine::InsertProcessor> create(Loader loader = {});
std::vector<engine::ParameterInfo> parameterInfo();

// ----- for the editor and tests (controller thread; `processor` may be any insert) -------

// The sample a sampler plays (null for none, or for another processor).
std::shared_ptr<const Sample> currentSample(const engine::InsertProcessor& processor);
// Why the sample did not load (empty when it did, or when there is none).
std::string loadError(const engine::InsertProcessor& processor);
// Gives a sampler a sample directly (tests, previews): the path is not kept.
void setSample(engine::InsertProcessor& processor, std::shared_ptr<const Sample> sample);
// Where each sounding voice is in the sample (frames) and the note it plays,
// as the real-time thread last published them.
struct VoiceInfo
{
    double position = 0.0;
    int note = 0;
    bool releasing = false;
};
std::vector<VoiceInfo> soundingVoices(const engine::InsertProcessor& processor);

} // namespace northlight::plugins::sampler
