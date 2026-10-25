#pragma once

#include "ui/Theme.h"

#include <juce_graphics/juce_graphics.h>

// A clip's frame as the arrange view draws it (screens/arrange.md "Clips"): the
// body, the header band along its top, the muted hatching, the outline and the
// name. The audio, MIDI and recording clips and the take lanes' takes all draw
// through these, so a clip looks the same whatever it holds.

namespace northlight::ui
{

namespace clips
{
// The body (rounded, radius sm) and, when headerHeight > 0, the header band.
void paintBody(juce::Graphics& g,
               const Theme& theme,
               juce::Rectangle<int> block,
               int headerHeight,
               juce::Colour body,
               juce::Colour header);
// The hatching of a muted clip over its body (45 degrees, 6 px apart), within `visible`.
void paintHatch(juce::Graphics& g,
                juce::Rectangle<int> body,
                juce::Rectangle<int> visible,
                juce::Colour colour,
                float dim);
// The 1 px inset outline.
void paintOutline(juce::Graphics& g, const Theme& theme, juce::Rectangle<int> block, juce::Colour colour);
// Where the name goes: the header (or the block when there is none), within what
// is visible, inset from the left and right.
juce::Rectangle<int> labelArea(juce::Rectangle<int> block,
                               juce::Rectangle<int> visible,
                               int headerHeight,
                               int leftInset,
                               int rightInset);
// The name in caption, truncated with an ellipsis.
void paintLabel(juce::Graphics& g,
                const Theme& theme,
                juce::Rectangle<int> area,
                const juce::String& text,
                juce::Colour colour);
} // namespace clips

} // namespace northlight::ui
