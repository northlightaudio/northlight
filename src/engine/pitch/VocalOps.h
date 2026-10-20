#pragma once

#include "core/Vocal.h"
#include "engine/pitch/PitchAnalysis.h"

#include <vector>

// Edits on a VocalEdit that the audio editor's tools make. Plain functions on the
// model's values; the editor wraps the result in a SetVocalEdit command.

namespace aural::engine::pitch
{

// The pitch the ear hears over [start, end): the loudness-weighted mean of the
// stable middle (the attack and release left out), as the analysis finds centres.
float centreOver(const PitchAnalysis& analysis, std::int64_t start, std::int64_t end);

// Splits a note at a source sample; the right half gets id 0 (the command gives it
// one) and keeps the note's shift and amounts. False when `at` is within 30 ms of
// either end.
bool splitNote(core::VocalEdit& edit, const PitchAnalysis& analysis, core::Id noteId, std::int64_t at);

// Joins a note with the next one when they join (no gap between them).
bool mergeWithNext(core::VocalEdit& edit, const PitchAnalysis& analysis, core::Id noteId);

// Every segment of a kind gets this gain (muted = silent).
void setSegments(core::VocalEdit& edit, core::VocalSegmentKind kind, float gainDb, bool muted);

// The notes back to how they were sung (pitch, drift, vibrato; gain and mute kept).
void resetPitch(core::VocalEdit& edit, const std::vector<core::Id>& notes);

// Each note's target on the nearest semitone (100% correction, drift as it is).
void snapToSemitones(core::VocalEdit& edit, const std::vector<core::Id>& notes);

} // namespace aural::engine::pitch
