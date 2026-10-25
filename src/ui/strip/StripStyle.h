#pragma once

#include "engine/Gain.h"
#include "plugins/ChannelStrip.h"
#include "ui/Number.h"
#include "ui/Theme.h"
#include "ui/Wells.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>
#include <span>
#include <string>

// Shared drawing for the channel strip editors (screens/channel-strip.md): module
// hues, the log-frequency and dB mappings and the graph well. Module hues are
// data colours from the track palette, never the accent (which stays the
// colour of selection and focus).

namespace northlight::ui::strip
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
// the tooltips (textSecondary for a type with none).
juce::Colour moduleColour(const Theme& theme, const std::string& type);

// The EQ's band hues: low cut, bands 1-4, high cut.
inline juce::Colour bandColour(const Theme& theme, int band)
{
    static const char* names[] = {"Lichen", "Rowan", "Gorse", "Machair", "Thrift", "Heather"};
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

// The frequency lines a log-frequency plot shows by default, 30 Hz to 20 kHz.
inline constexpr std::array<double, 12> frequencyLines{
    30.0, 50.0, 100.0, 200.0, 300.0, 500.0, 1000.0, 2000.0, 3000.0, 5000.0, 10000.0, 20000.0};

// A log-frequency plot's vertical grid over `p`: a line at each of `lines`
// (gridBar on the decades, gridBeat between), each labelled under the plot
// ("50", "2k") in textTertiary, its 12 px box `labelGap` below the plot. The
// lines at the ends of the range (under 40 Hz, 20 kHz) have no label. Leaves
// the labels' font set.
void drawFrequencyGrid(juce::Graphics& g,
                       const Theme& theme,
                       juce::Rectangle<float> p,
                       std::span<const double> lines = frequencyLines,
                       float labelGap = 1.0f);

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
    return engine::gainToDb(linear, static_cast<float>(engine::processorFloorDb));
}

// drawWell and drawWellFrame live in ui/Wells.h (shared with the mixer).
using ui::drawWell;
using ui::drawWellFrame;

} // namespace northlight::ui::strip
