#pragma once

#include "core/NoteNames.h"

#include <cstdint>
#include <vector>

// Vocal pitch editing (docs/design/screens/pitch-editing.md). An audio clip can carry a
// VocalEdit: the notes found in its media, each with the parameters the user (or
// "correct to key") set, and the unvoiced segments between them (breaths,
// consonants, silence) with their gain. The pitch curve and pitch marks the notes
// were found from are derived data cached beside the media, not part of the model.
//
// Times are in source samples (the clip's media at the project rate), so a clip
// that is moved, trimmed, split or duplicated keeps its edits on the audio they
// belong to. Pitches are in cents above MIDI note 0 (6000 = MIDI note 60).

namespace northlight::core
{

using Id = std::uint64_t;
using SamplePosition = std::int64_t;

struct VocalNote
{
    Id id = 0;
    SamplePosition start = 0, end = 0; // source samples
    float centre = 6000.0f;            // the pitch as sung (cents)
    float target = 6000.0f;            // where the centre is moved to
    float drift = 1.0f;                // how much of the slow drift around the centre is kept (0..1)
    float vibrato = 1.0f;              // vibrato depth (0..2; 1 = as sung)
    // The glide into this note from the one before (when they join) and out of it
    // into the next, in samples either side of the join: the singer's slide is kept
    // and moves with the two notes across this span.
    SamplePosition transitionIn = 0, transitionOut = 0;
    float gainDb = 0.0f;
    bool muted = false;

    SamplePosition length() const noexcept { return end - start; }
    float shift() const noexcept { return target - centre; }
    // Whether the note plays exactly as recorded.
    bool isIdentity() const noexcept
    {
        return target == centre && drift == 1.0f && vibrato == 1.0f && gainDb == 0.0f && !muted;
    }
    bool operator==(const VocalNote&) const = default;
};

enum class VocalSegmentKind : std::uint8_t
{
    breath,
    consonant, // sibilants and other unvoiced consonants: shown, never touched by the tools
    silence,   // room tone between phrases
};

struct VocalSegment
{
    Id id = 0;
    SamplePosition start = 0, end = 0; // source samples
    VocalSegmentKind kind = VocalSegmentKind::silence;
    float gainDb = 0.0f;
    bool muted = false;

    SamplePosition length() const noexcept { return end - start; }
    bool isIdentity() const noexcept { return gainDb == 0.0f && !muted; }
    bool operator==(const VocalSegment&) const = default;
};

struct VocalEdit
{
    // The analysis the notes came from (engine::PitchAnalysis::version); 0 = the
    // clip has no vocal edit and plays its media untouched.
    int analysisVersion = 0;
    std::vector<VocalNote> notes;       // sorted by start, not overlapping
    std::vector<VocalSegment> segments; // sorted by start, not overlapping the notes

    bool active() const noexcept { return analysisVersion > 0; }
    // Whether anything plays differently from the recording.
    bool changesSound() const noexcept
    {
        if (!active())
            return false;
        for (const auto& n : notes)
            if (!n.isIdentity())
                return true;
        for (const auto& s : segments)
            if (!s.isIdentity())
                return true;
        return false;
    }
    VocalNote* findNote(Id noteId) noexcept
    {
        for (auto& n : notes)
            if (n.id == noteId)
                return &n;
        return nullptr;
    }
    const VocalNote* findNote(Id noteId) const noexcept
    {
        return const_cast<VocalEdit*>(this)->findNote(noteId);
    }
    VocalSegment* findSegment(Id segmentId) noexcept
    {
        for (auto& s : segments)
            if (s.id == segmentId)
                return &s;
        return nullptr;
    }
    bool operator==(const VocalEdit&) const = default;
};

// The project's key: what "correct to key" aims at and the piano roll highlights.
enum class ScaleKind : std::uint8_t
{
    major,
    minor, // natural minor
    harmonicMinor,
    dorian,
    mixolydian,
    majorPentatonic,
    minorPentatonic,
    chromatic,
};

struct MusicalKey
{
    int root = -1; // pitch class 0 (C) .. 11 (B); -1 = no key set
    ScaleKind scale = ScaleKind::major;

    bool isSet() const noexcept { return root >= 0 && root < 12; }
    // Bit n set when pitch class n is in the scale.
    std::uint16_t mask() const noexcept;
    bool contains(int pitchClass) const noexcept
    {
        return !isSet() || (mask() >> (((pitchClass % 12) + 12) % 12) & 1u) != 0;
    }
    bool operator==(const MusicalKey&) const = default;
};

const char* scaleName(ScaleKind scale) noexcept;

} // namespace northlight::core
