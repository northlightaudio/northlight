#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace northlight::ui
{

// A display well (screens/channel-strip.md 12.1), cut into the panel: drawWell fills
// it (the material's meter well, canvas without the material) before the
// content; drawWellFrame goes over the content: an inner shadow from the top
// edge and a little from the sides, a dark rim, and the lip of light along the
// bottom edge that the panel's surface catches.
inline void drawWell(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> area)
{
    g.setColour(theme.material.enabled ? theme.material.meterWell : theme.colours.canvas);
    g.fillRoundedRectangle(area, theme.radius.sm);
}

// Drawn from an image cached per size, scale and colours (the frame's clip path
// and four gradients cost more than the content they frame, and most wells
// are redrawn every display frame). Message thread.
void drawWellFrame(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> area);
// The frame's art, drawn directly (what the cache holds).
void paintWellFrame(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> area);
// How many frames the cache has drawn into images (tests).
int wellFrameRenders() noexcept;

} // namespace northlight::ui
