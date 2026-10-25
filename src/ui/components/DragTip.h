#pragma once

#include "ui/Theme.h"

#include <juce_graphics/juce_graphics.h>

namespace northlight::ui
{

// The tag a drag shows beside the pointer (screens/arrange.md: the new start,
// a length, a fade, a pitch): caption text on surface2 with a lineStrong
// outline, below and right of `at`, moved to the pointer's left when it would
// leave `bounds` on the right and above it when it would leave them below.
void paintDragTip(juce::Graphics& g,
                  const Theme& theme,
                  const juce::String& text,
                  juce::Point<int> at,
                  juce::Rectangle<int> bounds);

} // namespace northlight::ui
