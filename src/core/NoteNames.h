#pragma once

#include <string>

// MIDI note names, one convention for the whole app: middle C (MIDI 60) is C3, as
// in most DAWs and the Pulsar pad map (docs/decisions.md).
namespace northlight::core
{

inline constexpr int middleC = 60;
inline constexpr int middleCOctave = 3;

// A note's pitch class, 0 (C) to 11 (B), for any note, negative ones too.
constexpr int pitchClassOf(int midiNote) noexcept
{
    return ((midiNote % 12) + 12) % 12;
}

const char* pitchClassName(int pitchClass) noexcept; // "C", "C#", ...
int octaveOf(int midiNote) noexcept;                 // 60 -> 3, 69 (A 440) -> 3
std::string noteName(int midiNote);                  // 60 -> "C3", 61 -> "C#3"
bool isBlackKey(int midiNote) noexcept;

} // namespace northlight::core
