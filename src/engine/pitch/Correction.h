#pragma once

#include "core/Model.h"

#include <cstdint>
#include <vector>

// "Correct to key" (docs/design/13-pitch-editing.md): a suggestion pass that sets
// each note's target and drift amount. It never snaps:
//
//   - each note is aimed at one of the scale pitches either side of it or at its
//     nearest semitone (a chromatic note sung in tune stays chromatic: the key
//     breaks ties, it does not force), chosen by a small dynamic programme over the
//     phrase that weighs the distance in
//     cents, keeps the melody's direction (a rising interval never becomes a
//     falling or level one), lands repeated sung pitches on one target, and can
//     prefer chord tones from the MIDI sounding at the time;
//   - notes already within the tolerance are left alone, the rest move by the
//     strength, so a little of the singer's offset stays;
//   - short notes (ornaments, melismas) move much less, long ones have some drift
//     trimmed, vibrato is kept;
//   - nothing moves more than about a semitone on its own: such notes are flagged.

namespace aural::engine::pitch
{

struct CorrectionOptions
{
    float strength = 0.85f;       // share of the way to the target (0..1)
    float toleranceCents = 10.0f; // notes this close are left alone
    float drift = 0.6f;           // drift amount for long notes (1 = as sung; 0.6 trims 40%)
    float vibrato = 1.0f;         // vibrato amount
    bool chordAware = false;      // prefer chord tones of the MIDI under each note
    float maxShiftCents = 110.0f; // farther than this: flagged, not moved
};

struct CorrectionResult
{
    std::vector<core::VocalNote> notes;
    std::vector<core::Id> flagged; // notes that would have had to move too far
};

// `chordTones` (optional, one per note): the pitch classes sounding under each
// note as a 12-bit mask (bit n = pitch class n), 0 when nothing is known.
CorrectionResult correctToKey(const std::vector<core::VocalNote>& notes,
                              core::MusicalKey key,
                              double sampleRate,
                              const CorrectionOptions& options = {},
                              const std::vector<std::uint16_t>& chordTones = {});

// The major or minor key that best fits the notes: a pitch-class histogram weighted
// by duration, correlated with the Krumhansl-Kessler profiles.
core::MusicalKey detectKey(const std::vector<core::VocalNote>& notes);

// For each note of `clip` (on track `trackId`): the pitch classes the project's
// MIDI clips on other tracks sound across the note's middle.
std::vector<std::uint16_t>
chordTonesUnder(const core::Project& project, core::Id trackId, const core::AudioClip& clip);

} // namespace aural::engine::pitch
