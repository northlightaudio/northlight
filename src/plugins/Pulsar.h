#pragma once

#include "engine/Insert.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Pulsar: the drum synth bundled with Aural (07-plugins.md 7.4c). Eight
// synthesised pads on the General MIDI drum notes (and C3 to G3): kick, rim,
// snare, clap, closed hat, open hat, low tom, high tom. Each pad has tune,
// decay, two shaping controls of its own, level and pan; the closed hat
// chokes the open one. A master drive, velocity and level follow. JUCE-free;
// real-time safe after prepare().

namespace aural::plugins::pulsar
{

inline const std::string type = "aural.pulsar";

enum Pad
{
    kick,
    rim,
    snare,
    clap,
    closedHat,
    openHat,
    lowTom,
    highTom,
    padCount
};

// A pad's parameters, in order; pad p's field f is parameter p * fieldCount + f.
enum Field
{
    tune,  // semitones
    decay, // ms
    tone,  // 0..1, the pad's first shaping control (a kick's click, a snare's snappy, ...)
    snap,  // 0..1, its second (a kick's punch, a snare's crack, ...)
    level, // dB
    pan,   // -1..1
    fieldCount
};
constexpr int paramIndex(int pad, Field field) noexcept
{
    return pad * fieldCount + field;
}

// After the pads.
enum Master
{
    drive = padCount * fieldCount,
    velocity, // 0..1 how much velocity shapes the level
    output,   // dB
    count
};

// A pad's name ("Kick"), short name ("KICK"), its two shaping controls' names
// ("Click", "Punch") and its MIDI note (36 for the kick).
const char* padName(int pad) noexcept;
const char* padShortName(int pad) noexcept;
const char* toneName(int pad) noexcept;
const char* snapName(int pad) noexcept;
int padNote(int pad) noexcept;
// The pad a note plays, or -1.
int padForNote(int note) noexcept;

std::unique_ptr<engine::InsertProcessor> create();
std::vector<engine::ParameterInfo> parameterInfo();

// The factory kits, by name: each a full set of parameter values.
struct Kit
{
    std::string name;
    std::vector<float> values;
};
const std::vector<Kit>& kits();

// One hit of a pad at full velocity, mono, before the master: for drawing it.
std::vector<float> renderPad(const float* params, int pad, double sampleRate, int samples);

// What the editor reads from a live Pulsar: a counter per pad that a hit
// bumps, and each pad's level now (0..1). Null for another processor.
struct Activity
{
    std::array<std::atomic<std::uint32_t>, padCount> hits{};
    std::array<std::atomic<float>, padCount> level{};
};
const Activity* activity(const engine::InsertProcessor& processor) noexcept;

} // namespace aural::plugins::pulsar
