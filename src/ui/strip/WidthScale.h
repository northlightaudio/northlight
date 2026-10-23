#pragma once

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>

// The stereo widener display's width axis (screens/plugins.md 7.2a): 0 to 200%
// on a linear scale in the plot's lower part, and above it a compressed headroom
// strip for what the output measures past 200%, 200 to 800% on a log scale (400%
// halfway), so a very wide band reads as off the scale rather than clipped.
namespace northlight::ui::strip::widthscale
{

inline constexpr float settable = 200.0f; // the widest a band can be set (the linear scale's top)
inline constexpr float ceiling = 800.0f;  // the headroom strip's top

// The strip's height: 14% of the plot, between 16 and 26 px.
inline float headroom(juce::Rectangle<float> plot)
{
    return std::round(std::clamp(plot.getHeight() * 0.14f, 16.0f, 26.0f));
}

// Where the linear scale ends (200%) and the strip begins.
inline float settableTop(juce::Rectangle<float> plot)
{
    return plot.getY() + headroom(plot);
}

inline float toY(float width, juce::Rectangle<float> plot)
{
    const float top = settableTop(plot);
    if (width <= settable)
        return plot.getBottom() - std::clamp(width / settable, 0.0f, 1.0f) * (plot.getBottom() - top);
    const float over = std::log2(width / settable) / std::log2(ceiling / settable);
    return top - std::clamp(over, 0.0f, 1.0f) * headroom(plot);
}

// The width a drag at y sets: the linear scale only (a band is set to 200% at most).
inline float settableAt(float y, juce::Rectangle<float> plot)
{
    const float top = settableTop(plot);
    return std::clamp((plot.getBottom() - y) / (plot.getBottom() - top), 0.0f, 1.0f) * settable;
}

} // namespace northlight::ui::strip::widthscale
