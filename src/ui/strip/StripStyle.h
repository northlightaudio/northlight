#pragma once

#include "plugins/ChannelStrip.h"
#include "ui/Theme.h"
#include "ui/Wells.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <string>

// Shared drawing for the channel strip editors (12-channel-strip.md): module
// hues, the log-frequency and dB mappings and the graph well. Module hues are
// data colours from the track palette, never the accent (which stays the
// colour of selection and focus).

namespace aural::ui::strip
{

// A track palette colour by name (textSecondary when a user's token file drops it).
inline juce::Colour paletteColour(const Theme& theme, const char* name)
{
    for (const auto& entry : theme.track.palette)
        if (entry.name == name)
            return entry.value;
    return theme.colours.textSecondary;
}

// Each module's hue, so a module reads the same in the editor, the mixer and
// the tooltips.
inline juce::Colour moduleColour(const Theme& theme, const std::string& type)
{
    namespace t = plugins::strip::types;
    if (type == t::gate)
        return paletteColour(theme, "Mint");
    if (type == t::eq)
        return paletteColour(theme, "Sky");
    if (type == t::compressor)
        return paletteColour(theme, "Indigo");
    if (type == t::deesser)
        return paletteColour(theme, "Violet");
    if (type == t::saturation)
        return paletteColour(theme, "Tangerine");
    if (type == t::limiter)
        return paletteColour(theme, "Coral");
    // The built-ins, in the same family (a built-in EQ, compressor or limiter
    // takes its strip counterpart's hue).
    if (type == "aural.eq")
        return paletteColour(theme, "Sky");
    if (type == "aural.compressor")
        return paletteColour(theme, "Indigo");
    if (type == "aural.limiter")
        return paletteColour(theme, "Coral");
    if (type == "aural.gain")
        return paletteColour(theme, "Lime");
    if (type == "aural.reverb")
        return paletteColour(theme, "Teal");
    if (type == "aural.delay")
        return paletteColour(theme, "Amber");
    if (type == "aural.aurora")
        return paletteColour(theme, "Violet");
    if (type == "aural.pulsar")
        return paletteColour(theme, "Coral");
    if (type == "aural.nebula")
        return paletteColour(theme, "Magenta");
    if (type == "aural.starbit")
        return paletteColour(theme, "Lime");
    return theme.colours.textSecondary;
}

// The EQ's band hues: low cut, bands 1-4, high cut.
inline juce::Colour bandColour(const Theme& theme, int band)
{
    static const char* names[] = {"Teal", "Coral", "Amber", "Lime", "Magenta", "Violet"};
    return paletteColour(theme, names[juce::jlimit(0, 5, band)]);
}

// The audible range on a log axis.
constexpr double minHz = 20.0, maxHz = 20000.0;

inline float hzToX(double hz, juce::Rectangle<float> area)
{
    const double f = std::log(juce::jlimit(minHz, maxHz, hz) / minHz) / std::log(maxHz / minHz);
    return area.getX() + static_cast<float>(f) * area.getWidth();
}

inline double xToHz(float x, juce::Rectangle<float> area)
{
    const double f = juce::jlimit(0.0, 1.0, static_cast<double>((x - area.getX()) / area.getWidth()));
    return minHz * std::pow(maxHz / minHz, f);
}

// dB on a linear axis from `topDb` at the top to `bottomDb` at the bottom.
inline float dbToY(float db, float topDb, float bottomDb, juce::Rectangle<float> area)
{
    const float f = (topDb - juce::jlimit(bottomDb, topDb, db)) / (topDb - bottomDb);
    return area.getY() + f * area.getHeight();
}

inline float yToDb(float y, float topDb, float bottomDb, juce::Rectangle<float> area)
{
    const float f = juce::jlimit(0.0f, 1.0f, (y - area.getY()) / area.getHeight());
    return topDb - f * (topDb - bottomDb);
}

inline float toDb(float linear)
{
    return linear > 1.0e-6f ? 20.0f * std::log10(linear) : -120.0f;
}

inline juce::String formatHz(double hz)
{
    if (hz >= 1000.0)
        return juce::String(hz / 1000.0, hz >= 10000.0 ? 1 : 2) + " kHz";
    return juce::String(static_cast<int>(std::lround(hz))) + " Hz";
}

inline juce::String formatDb(float db, int decimals = 1)
{
    if (db <= -119.0f)
        return juce::String::fromUTF8("-\xe2\x88\x9e dB");
    return (db > 0.05f ? "+" : "") + juce::String(db, decimals) + " dB";
}

// drawWell and drawWellFrame live in ui/Wells.h (shared with the mixer).
using ui::drawWell;
using ui::drawWellFrame;

} // namespace aural::ui::strip
