#pragma once

#include "engine/Insert.h"
#include "plugins/SampleLoader.h"

#include <memory>
#include <string>

// Corona: a tube guitar amp modelled on the 1960s-70s American 2x12 combo with
// reverb and tremolo (a Twin Reverb), and its cabinet. The signal goes through
// the circuit's stages in order: a first 12AX7 stage, the treble / middle /
// bass tone stack (the exact passive network), the volume pot with its bright
// capacitor, a second 12AX7 stage (with the silverface's pull boost in front),
// the spring reverb's tank on a send, a mixer stage, the opto tremolo, the
// master volume, the phase inverter, a push-pull 6L6 power amp with negative
// feedback and supply sag, the output transformer and the speaker's load. The
// triode stages are curves solved from Koren's tube equations; everything
// nonlinear runs oversampled (2x, 4x or 8x) through minimum-phase half-bands
// with antiderivative anti-aliasing, so there is no latency to report. A noise
// gate sits in front, and the cabinet and two mics (plugins/Cabinet.h) build a
// response a zero-latency convolver plays. A user's own response file can
// stand in for the cabinet: its path is the insert's state.
//
// The knobs read 1..10 as the amp's do (1 is fully down). JUCE-free; real-time
// safe after prepare().

namespace northlight::plugins::amp
{

inline const std::string type = "northlight.amp";

enum Param
{
    bright,    // Off / On
    volume,    // 1..10
    treble,    // 1..10
    middle,    // 1..10
    bass,      // 1..10
    reverb,    // 1..10
    speed,     // 1..10, the tremolo's rate
    intensity, // 1..10, its depth
    reverbOn,  // Off / On: the footswitch
    tremoloOn, // Off / On: the footswitch
    master,    // 1..10
    boost,     // Off / On: the master's pull boost
    input,     // dB, the input's trim (0 dBFS = 1 V at the jack)
    gate,      // dB, the noise gate's threshold (gateOffDb = off)
    sag,       // %, how far the supply droops under load
    quality,   // choice: 2x / 4x / 8x oversampling
    output,    // dB
    cabinetOn, // Off / On: off sends the amp's output without a cabinet (a line out)
    cabinet,   // choice: plugins::cabinet::Model
    micA,      // first of the mics' parameters (micFieldCount each)
    room = micA + 16,
    count
};

// Each mic's parameters, from micParam(mic, field).
enum MicField
{
    micOn,       // Off / On
    micType,     // choice: plugins::cabinet::MicType
    micPosition, // %, 0 the cap .. 100 the cone's edge
    micDistance, // cm, 1..100
    micAngle,    // degrees, 0..45
    micLevel,    // dB
    micPan,      // %, -100 left .. 100 right
    micPolarity, // Normal / Inverted
    micFieldCount
};
static_assert(micFieldCount * 2 == room - micA);

constexpr int micParam(int mic, int field) noexcept
{
    return micA + mic * micFieldCount + field;
}

inline constexpr float gateOffDb = -90.0f;
// The oversampling the Quality choice gives.
inline constexpr int qualityFactors[] = {2, 4, 8};

// A knob's 1..10 reading as a pot's travel, 0..1.
constexpr double travel(float knob) noexcept
{
    return knob <= 1.0f ? 0.0 : knob >= 10.0f ? 1.0 : (knob - 1.0) / 9.0;
}

std::unique_ptr<engine::InsertProcessor> create(sampler::Loader loader = {});

// ----- for the editor and the tests (controller thread; any processor) ----------------------

// Builds the cabinet's response for the current settings now (the background
// thread otherwise does it within about 15 ms of a change).
void settleCabinet(engine::InsertProcessor& processor);
// The user's response file (empty for none) and why it could not be read.
std::string userResponsePath(const engine::InsertProcessor& processor);
std::string userResponseError(const engine::InsertProcessor& processor);
// The state for a response file (the Sampler's encoding of a path).
inline std::string encodeState(const std::string& path)
{
    return sampler::encodeState(path);
}

// What each frame of the amp's feedback carries: input = the input's peak after
// the trim (linear, 1 = 0 dBFS); output = the output's peak; reduction = the
// gate's (dB); extra = how hard the power amp is driven (1 = where it starts
// to clip); extra2 = the tremolo's lamp (0..1).

} // namespace northlight::plugins::amp
