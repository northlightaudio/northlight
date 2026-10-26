#pragma once

#include "core/Model.h"
#include "ui/Theme.h"

#include <juce_graphics/juce_graphics.h>

namespace northlight::ui
{

// The audio editor's pitch grid (screens/pitch-editing.md, pitch mode): a row per
// semitone across `grid`, black keys in laneAlt, rows outside the project key
// darker still, an octave line under each C; and the keyboard in `keys` beside
// it, its C named and the key's root marked. `topCents` is the pitch at the
// grid's top and `rowHeight` a semitone's height.
void paintPitchGrid(juce::Graphics& g,
                    const Theme& theme,
                    juce::Rectangle<int> grid,
                    juce::Rectangle<int> keys,
                    float topCents,
                    float rowHeight,
                    const core::MusicalKey& key);

} // namespace northlight::ui
