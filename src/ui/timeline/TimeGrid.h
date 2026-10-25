#pragma once

#include "core/TempoMap.h"
#include "ui/Theme.h"
#include "ui/timeline/GridLines.h"
#include "ui/timeline/TimelineViewport.h"

#include <juce_graphics/juce_graphics.h>

// The grid and the bars ruler as every timeline view draws them, from the one
// line iterator (GridLines.h): screens/arrange.md "Ruler" and "Lanes and grid".

namespace northlight::ui
{

// Bar lines in gridBar, beats in gridBeat and sub-beats in gridSub, through
// `area` (only the columns inside it). `view` maps timeline positions.
void paintGridLines(juce::Graphics& g,
                    const Theme& theme,
                    const core::TempoMap& map,
                    const TimelineViewport& view,
                    juce::Rectangle<int> area);

// The bars row of a ruler over `area`: a full-height tick per bar with its number
// (every second, fourth... bar when bars are narrower than 80 px where the view
// starts, and then the bar's start time beside it), 6 px beat ticks and 3 px
// sub-beat ticks along the bottom. Draws nothing left of `area`.
void paintBarsRuler(juce::Graphics& g,
                    const Theme& theme,
                    const core::TempoMap& map,
                    const TimelineViewport& view,
                    juce::Rectangle<int> area);

} // namespace northlight::ui
