#include "core/NoteNames.h"

namespace northlight::core
{
namespace
{
int pitchClassOf(int midiNote) noexcept
{
    return ((midiNote % 12) + 12) % 12;
}
} // namespace

const char* pitchClassName(int pitchClass) noexcept
{
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[pitchClassOf(pitchClass)];
}

int octaveOf(int midiNote) noexcept
{
    // Floor division, so notes below 0 (a pitch-edit target can go there) still
    // name the right octave.
    const int octave = (midiNote - pitchClassOf(midiNote)) / 12;
    return octave + middleCOctave - middleC / 12;
}

std::string noteName(int midiNote)
{
    return std::string(pitchClassName(midiNote)) + std::to_string(octaveOf(midiNote));
}

bool isBlackKey(int midiNote) noexcept
{
    switch (pitchClassOf(midiNote))
    {
        case 1:
        case 3:
        case 6:
        case 8:
        case 10:
            return true;
        default:
            return false;
    }
}

} // namespace northlight::core
