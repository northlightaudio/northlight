#pragma once

#include "core/Model.h"
#include "core/TempoMap.h"
#include "ui/edit/NoteGrid.h"

#include <algorithm>
#include <cstdint>
#include <vector>

// The piano roll's note edits as plain functions of a clip and a selection, so
// they are tested without the view (screens/piano-roll.md). Each returns what to
// change: notes to set (by id, the whole note), notes to add (ids invalid) and
// notes to remove; the view turns that into SetNote, AddNote and RemoveNote
// commands as one undo step. Positions are clip-relative samples. No JUCE.

namespace northlight::ui::noteops
{

using core::Id;
using core::SamplePosition;

struct NoteEdit
{
    std::vector<core::MidiNote> set;
    std::vector<core::MidiNote> add;
    std::vector<Id> remove;
    bool empty() const noexcept { return set.empty() && add.empty() && remove.empty(); }
};

// The notes a selection names, in time order (an empty selection: none).
std::vector<core::MidiNote> selected(const core::MidiClip& clip, const std::vector<Id>& selection);

// ----- the clipboard -------------------------------------------------------------------------

// Copied notes, their starts from the earliest one's (0), and the span they cover
// rounded up to whole grid steps (where a paste of them ends).
struct Clipboard
{
    std::vector<core::MidiNote> notes;
    SamplePosition span = 0;
    bool empty() const noexcept { return notes.empty(); }
};
Clipboard copy(const core::MidiClip& clip, const std::vector<Id>& selection, SamplePosition gridSamples);
// The clipboard's notes from `at` (ids invalid); notes that would start past the
// clip's end are left out.
NoteEdit paste(const core::MidiClip& clip, const Clipboard& clipboard, SamplePosition at);

// ----- lengths and timing --------------------------------------------------------------------

// Each selected note ends where the next note (of any pitch, among the selection
// when it has more than one note) starts; the last keeps its length.
NoteEdit legato(const core::MidiClip& clip, const std::vector<Id>& selection);
NoteEdit setLength(const core::MidiClip& clip, const std::vector<Id>& selection, SamplePosition length);
// Lengths scaled (2 doubles, 0.5 halves), at least `minimum`.
NoteEdit scaleLength(const core::MidiClip& clip,
                     const std::vector<Id>& selection,
                     double factor,
                     SamplePosition minimum);
// Lengths changed by `delta` (keys: a grid step), at least `minimum`.
NoteEdit addLength(const core::MidiClip& clip,
                   const std::vector<Id>& selection,
                   SamplePosition delta,
                   SamplePosition minimum);

// Quantise (screens/piano-roll.md "Quantise"): each note's start moves `strength`
// of the way to the nearest line of `grid` on the tempo track; with `swing`
// every second line of the grid is late by that share of a step (0 straight, 1/3
// a triplet feel, 0.5 the most: swing 50% to 75% as drum machines count it;
// swingPercent converts); `ends` quantises the ends too (at least a step long).
// Notes already where they go are left out. An empty selection quantises every note.
struct QuantiseOptions
{
    float strength = 1.0f;
    float swing = 0.0f; // 0 .. 0.5
    bool ends = false;
};
// Swing as a drum machine's percentage (50 straight .. 75) and back.
inline float swingFromPercent(float percent) noexcept
{
    return std::clamp(percent / 100.0f * 2.0f - 1.0f, 0.0f, 0.5f);
}
NoteEdit quantise(const core::MidiClip& clip,
                  const std::vector<Id>& selection,
                  const core::TempoMap& map,
                  const NoteGrid& grid,
                  double samplesPerPixel,
                  const QuantiseOptions& options);
// Humanise: each note's start moved by up to ±timing samples and its velocity by
// up to ±velocity, from a seeded generator (the same seed, the same result).
NoteEdit humanise(const core::MidiClip& clip,
                  const std::vector<Id>& selection,
                  SamplePosition timing,
                  int velocity,
                  std::uint32_t seed);

// ----- velocity ------------------------------------------------------------------------------

// A velocity drag on a selection (screens/piano-roll.md "Velocity lane"):
// relative by default, every note moved by the dragged note's change (clamped,
// so the dynamics stay); `absolute` sets them all to the value.
NoteEdit dragVelocity(
    const core::MidiClip& clip, const std::vector<Id>& selection, Id dragged, int value, bool absolute);
NoteEdit setVelocity(const core::MidiClip& clip, const std::vector<Id>& selection, int value);
NoteEdit offsetVelocity(const core::MidiClip& clip, const std::vector<Id>& selection, int delta);
// Scaled about 0 (1.25: a quarter louder).
NoteEdit scaleVelocity(const core::MidiClip& clip, const std::vector<Id>& selection, double factor);
// Towards (amount < 1) or away from (> 1) the selection's mean: compress and expand.
NoteEdit compressVelocity(const core::MidiClip& clip, const std::vector<Id>& selection, double amount);
NoteEdit randomiseVelocity(const core::MidiClip& clip,
                           const std::vector<Id>& selection,
                           int range,
                           std::uint32_t seed);

// ----- pitch, mute, split and join -----------------------------------------------------------

NoteEdit transpose(const core::MidiClip& clip, const std::vector<Id>& selection, int semitones);
// Mutes every selected note, or unmutes them when all are muted.
NoteEdit toggleMute(const core::MidiClip& clip, const std::vector<Id>& selection);
// Splits each named note that `at` falls inside (more than `minimum` from either
// end): the note ends at `at` and a new one of the same pitch starts there.
NoteEdit
split(const core::MidiClip& clip, const std::vector<Id>& notes, SamplePosition at, SamplePosition minimum);
// Joins the named notes pitch by pitch: each run of notes of one pitch becomes
// its first note, lengthened to the run's last end; the rest go.
NoteEdit join(const core::MidiClip& clip, const std::vector<Id>& notes);

// ----- selection -----------------------------------------------------------------------------

// The note after the latest selected one (direction > 0) or before the earliest
// (< 0), in time order then pitch; with nothing selected the first or the last.
// invalidId when there is none.
Id stepSelection(const core::MidiClip& clip, const std::vector<Id>& selection, int direction);
// Every note of `pitch`.
std::vector<Id> notesOfPitch(const core::MidiClip& clip, int pitch);

// Applies an edit to a copy of the clip (tests and previews): sets by id, adds
// (with ids from `nextId`, counting up), removes; sorted.
core::MidiClip applied(core::MidiClip clip, const NoteEdit& edit, Id nextId = 1u << 30);

} // namespace northlight::ui::noteops
