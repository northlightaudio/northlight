#pragma once

#include "plugins/midi/MidiEffectBase.h"

#include <cstdint>
#include <memory>

// Transpose: a MIDI effect that moves every note by steps of the project's key,
// then by semitones and octaves. With no key set, a step of the key is a
// semitone. A note moved outside 0..127 is not played. JUCE-free.

namespace northlight::plugins::transpose
{

inline constexpr const char* type = "northlight.transpose";

enum Param
{
    steps,     // steps of the project's key, -7..7
    semitones, // -24..24
    octaves,   // -4..4
    count
};

// The note `steps` steps of the scale away from `note` (`mask`: bit n set when
// pitch class n is in the scale; 0: no key, a step is a semitone). A note outside
// the scale moves from the scale note below it and keeps its distance from it.
int stepInScale(int note, int steps, std::uint16_t mask) noexcept;

std::unique_ptr<engine::InsertProcessor> create();

} // namespace northlight::plugins::transpose
