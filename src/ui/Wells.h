#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace aural::ui
{

// A display well (12-channel-strip.md 12.1), cut into the panel: drawWell fills
// it (the material's meter well, canvas without the material) before the
// content; drawWellFrame goes over the content: an inner shadow from the top
// edge and a little from the sides, a dark rim, and the lip of light along the
// bottom edge that the panel's surface catches.
inline void drawWell(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> area)
{
    g.setColour(theme.material.enabled ? theme.material.meterWell : theme.colours.canvas);
    g.fillRoundedRectangle(area, theme.radius.sm);
}

inline void drawWellFrame(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> area)
{
    const auto shadow = theme.material.enabled && !theme.material.meterWellInset.empty()
                            ? theme.material.meterWellInset.colour
                            : theme.colours.overlay;
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(area, theme.radius.sm);
        g.reduceClipRegion(clip);
        const float top = 10.0f, side = 4.0f, bottom = 3.0f;
        g.setGradientFill(juce::ColourGradient::vertical(shadow.withAlpha(0.0f),
                                                         area.getBottom() - bottom,
                                                         shadow.withMultipliedAlpha(0.3f),
                                                         area.getBottom()));
        g.fillRect(area.withTop(area.getBottom() - bottom));
        g.setGradientFill(
            juce::ColourGradient::vertical(shadow, area.getY(), shadow.withAlpha(0.0f), area.getY() + top));
        g.fillRect(area.withHeight(top));
        g.setGradientFill(juce::ColourGradient::horizontal(
            shadow.withMultipliedAlpha(0.5f), area.getX(), shadow.withAlpha(0.0f), area.getX() + side));
        g.fillRect(area.withWidth(side));
        g.setGradientFill(juce::ColourGradient::horizontal(shadow.withAlpha(0.0f),
                                                           area.getRight() - side,
                                                           shadow.withMultipliedAlpha(0.5f),
                                                           area.getRight()));
        g.fillRect(area.withLeft(area.getRight() - side));
    }
    // The rim, then the lip below it.
    g.setColour(shadow.withMultipliedAlpha(0.9f));
    g.drawRoundedRectangle(area.reduced(0.5f), theme.radius.sm, 1.0f);
    g.setColour(theme.material.enabled ? theme.material.grooveLip : theme.colours.lineSubtle);
    g.drawHorizontalLine(
        static_cast<int>(area.getBottom()), area.getX() + theme.radius.sm, area.getRight() - theme.radius.sm);
}

} // namespace aural::ui
