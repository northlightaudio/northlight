#pragma once

#include "core/Model.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

// The pitch tools' rules, as plain functions of the vocal edit and the grid's
// mapping (screens/pitch-editing.md, "The audio clip editor"), so they are tested
// without the editor.

namespace northlight::ui::pitchedit
{

// A pitch drag of `delta` cents on a note at `target`: the move that lands its
// target on the nearest semitone, or `delta` itself when `free` (Alt: in cents).
float dragDelta(float target, float delta, bool free) noexcept;

// The nearest note's name ("E3"), with how far from it the pitch is when that is a
// cent or more ("E3 +2 ct", "A2 −14 ct").
juce::String label(float cents);
// "+12 ct", "−3 ct", "0 ct".
juce::String signedCents(float cents);

// The note under a point of the pitch grid: of the notes playing at `source`, the
// one whose target row is nearest `y`, within 1.2 rows. Null when none is.
const core::VocalNote* noteAt(const core::VocalEdit& edit,
                              core::SamplePosition source,
                              float y,
                              const std::function<float(float cents)>& yForCents,
                              float rowHeight);

// A join between two notes that touch, near a point: the left note, when the
// point is within 4 px of the join and between the two notes' rows (a row of
// margin). Null when there is none.
const core::VocalNote* joinAt(const std::vector<core::VocalNote>& notes,
                              float x,
                              float y,
                              const std::function<float(double source)>& xForSource,
                              const std::function<float(float cents)>& yForCents,
                              float rowHeight);

} // namespace northlight::ui::pitchedit
