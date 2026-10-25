#pragma once

#include "core/Model.h"
#include "ui/PaintStats.h"

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>
#include <optional>

// The waveform as every timeline view draws it (the arrange view's clips and
// recording takes, the take lanes, the audio editor): one min/max column per
// pixel, the clip's gain and fades applied the way they play (and clamped to
// full scale after them, so a boost reads as louder until it clips), each
// channel in its own lane with an optional centre line.

namespace northlight::ui
{

// A clip's level at a timeline offset into it: its gain through its fades
// (core::fadeShape), as the engine plays it.
inline float clipLevelAt(const core::AudioClip& clip, core::SamplePosition offset) noexcept
{
    float level = clip.gain;
    if (clip.fadeIn > 0 && offset < clip.fadeIn)
        level *= core::fadeShape(static_cast<float>(offset) / static_cast<float>(clip.fadeIn),
                                 core::fadeShapeK(clip.fadeInCurve));
    if (clip.fadeOut > 0 && offset > clip.length - clip.fadeOut)
        level *= core::fadeShape(static_cast<float>(clip.length - offset) / static_cast<float>(clip.fadeOut),
                                 core::fadeShapeK(clip.fadeOutCurve));
    return level;
}

struct WaveformStyle
{
    juce::Colour lit;                   // a column that plays
    juce::Colour dim;                   // one that does not (outside the clip, not in the comp)
    std::optional<juce::Colour> centre; // a 1 px line through each channel's middle
    float heightFraction = 0.48f;       // of a channel's lane, either side of the middle, at full scale
};

// One column: the peaks under it and the level they play at.
struct WaveColumn
{
    float minimum = 0.0f, maximum = 0.0f;
    float level = 1.0f;
    bool lit = true;
};

// Draws `channels` lanes stacked in `area`, the columns [fromX, toX); column(ch, x)
// gives each column's peaks.
template <typename Column>
void paintWaveform(juce::Graphics& g,
                   juce::Rectangle<int> area,
                   int channels,
                   int fromX,
                   int toX,
                   const WaveformStyle& style,
                   Column&& column)
{
    channels = std::max(1, channels);
    fromX = std::max(fromX, area.getX());
    toX = std::min(toX, area.getRight());
    const float laneHeight = static_cast<float>(area.getHeight()) / static_cast<float>(channels);
    const float half = laneHeight * style.heightFraction;
    for (int ch = 0; ch < channels; ++ch)
    {
        const float mid = static_cast<float>(area.getY()) + laneHeight * (static_cast<float>(ch) + 0.5f);
        if (style.centre)
        {
            g.setColour(*style.centre);
            g.fillRect(static_cast<float>(area.getX()), mid, static_cast<float>(area.getWidth()), 1.0f);
        }
        bool lit = true;
        g.setColour(style.lit);
        for (int x = fromX; x < toX; ++x)
        {
            paintstats::counters().waveColumns++;
            const WaveColumn c = column(ch, x);
            if (c.lit != lit)
            {
                lit = c.lit;
                g.setColour(lit ? style.lit : style.dim);
            }
            const float top = mid - std::clamp(c.maximum * c.level, -1.0f, 1.0f) * half;
            const float bottom = mid - std::clamp(c.minimum * c.level, -1.0f, 1.0f) * half;
            g.fillRect(
                static_cast<float>(x), std::min(top, bottom), 1.0f, std::max(1.0f, std::abs(bottom - top)));
        }
    }
}

} // namespace northlight::ui
