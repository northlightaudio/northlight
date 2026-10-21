#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace northlight::ui::brand
{

// The brand files shipped beside the app (assets/, docs/design/brand.md),
// shared by the splash screen and the welcome sheets.

// The splash photograph (assets/splash), loaded once; invalid when missing.
const juce::Image& photo();
// Its credit, as shown wherever the photograph is.
inline constexpr const char* photoCredit = "Photograph by Gytis Bukauskas on Unsplash";
// A brand drawing from assets/brand ("northlight-wordmark.svg", ...); null when missing.
std::unique_ptr<juce::Drawable> drawable(const juce::String& fileName);

// The wordmark's box on its own grid (scripts/brand-assets.py): 48 units tall,
// the baseline at 36, the N starting 1.42 units in.
inline constexpr float wordmarkBox = 48.0f, wordmarkBaseline = 36.0f, wordmarkInset = 1.42f;
// Draws the wordmark with its left edge at x and its baseline at y, its type
// `size` px (the em; the box is 48 units for a 40-unit em).
void drawWordmark(juce::Graphics& g, const juce::Drawable& wordmark, float x, float baselineY, float size);

} // namespace northlight::ui::brand
