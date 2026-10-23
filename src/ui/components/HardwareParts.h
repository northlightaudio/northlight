#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The drawing shared by a hardware face (design-system.md "Hardware face"):
// an effect drawn as a piece of studio equipment in the hardware tokens.
namespace northlight::ui::hardware
{

// A black chicken-head pointer knob, pointing at `angle` (radians clockwise from
// 12 o'clock): the pointed body with a cream line along it and a round cap.
void drawPointerKnob(juce::Graphics& g,
                     const Theme& theme,
                     juce::Point<float> centre,
                     float diameter,
                     float angle,
                     bool enabled,
                     bool hover);

// The printed scale round a knob: `count` ticks from `from` over `sweep`
// (radians), every `major`th one longer.
void drawScale(juce::Graphics& g,
               const Theme& theme,
               juce::Point<float> centre,
               float radius,
               float from,
               float sweep,
               int count,
               int major,
               bool enabled);

// Text printed on a textured panel: a soft shade under it keeps it legible
// over the light patches of a worn finish.
void drawLegend(juce::Graphics& g,
                const Theme& theme,
                const juce::String& text,
                juce::Rectangle<float> area,
                juce::Justification justification,
                juce::Colour colour);

// An image under assets/<folder>/ beside the executable, loaded once (invalid
// when the file is missing: a face falls back to its flat token colours).
const juce::Image& image(const juce::String& folder, const juce::String& file);

} // namespace northlight::ui::hardware
