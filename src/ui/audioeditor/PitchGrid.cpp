#include "ui/audioeditor/PitchGrid.h"

#include "core/NoteNames.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

void paintPitchGrid(juce::Graphics& g,
                    const Theme& theme,
                    juce::Rectangle<int> grid,
                    juce::Rectangle<int> keys,
                    float topCents,
                    float rowHeight,
                    const core::MusicalKey& key)
{
    const auto& c = theme.colours;
    const auto yForCents = [&](float cents)
    { return static_cast<float>(grid.getY()) + (topCents - cents) / 100.0f * rowHeight; };
    const auto centsForY = [&](float y)
    { return topCents - (y - static_cast<float>(grid.getY())) / rowHeight * 100.0f; };
    g.saveState();
    g.reduceClipRegion(grid.getUnion(keys));
    const int topNote = static_cast<int>(std::ceil(topCents / 100.0f)) + 1;
    const int bottomNote =
        static_cast<int>(std::floor(centsForY(static_cast<float>(grid.getBottom())) / 100.0f)) - 1;
    for (int note = std::min(127, topNote); note >= std::max(0, bottomNote); --note)
    {
        const float y = yForCents(static_cast<float>(note) * 100.0f + 50.0f);
        const auto row = juce::Rectangle<float>(
            static_cast<float>(grid.getX()), y, static_cast<float>(grid.getWidth()), rowHeight);
        if (core::isBlackKey(note))
        {
            g.setColour(c.laneAlt);
            g.fillRect(row);
        }
        if (key.isSet() && !key.contains(note))
        {
            g.setColour(c.canvas.withAlpha(0.45f));
            g.fillRect(row);
        }
        if (note % 12 == 0)
        {
            g.setColour(c.gridBar);
            g.fillRect(row.getX(), row.getBottom() - 1.0f, row.getWidth(), 1.0f);
        }
        // The keyboard.
        auto k = juce::Rectangle<float>(
            static_cast<float>(keys.getX()), y, static_cast<float>(keys.getWidth()), rowHeight);
        g.setColour(core::isBlackKey(note) ? c.surface0 : c.surface2);
        g.fillRect(k.reduced(0.0f, 0.5f));
        if (key.isSet() && key.root == note % 12)
        {
            g.setColour(c.accent.withAlpha(0.6f));
            g.fillRect(k.withWidth(3.0f));
        }
        if (note % 12 == 0)
        {
            g.setFont(theme.caption());
            g.setColour(c.textTertiary);
            g.drawText(juce::String(core::noteName(note)),
                       k.toNearestInt().reduced(6, 0),
                       juce::Justification::centredRight);
        }
    }
    g.restoreState();
}

} // namespace northlight::ui
