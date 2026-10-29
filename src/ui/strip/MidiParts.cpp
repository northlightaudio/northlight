#include "ui/strip/MidiParts.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui::strip
{

namespace
{
// A pitch class's place: its white key (0..6), and whether it is black (then it
// sits on the right edge of that white key).
constexpr int whiteOf[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
constexpr bool isBlack[12] = {false, true, false, true, false, false, true, false, true, false, true, false};

struct Geometry
{
    juce::Rectangle<float> keys;
    float whiteW = 0.0f;
    int whites = 0;
};

Geometry geometryOf(juce::Rectangle<float> area, const KeyboardView& view)
{
    Geometry g;
    g.keys = area.withTop(area.getBottom() - std::clamp(area.getHeight(), keyboardHeight, keyboardLargest));
    g.whites = std::max(1, view.octaves * 7);
    g.whiteW = g.keys.getWidth() / static_cast<float>(g.whites);
    return g;
}

bool inStrip(const KeyboardView& view, int note)
{
    return note >= view.lowNote && note < view.lowNote + 12 * view.octaves;
}
} // namespace

juce::Rectangle<float> keyBounds(juce::Rectangle<float> area, const KeyboardView& view, int note)
{
    if (!inStrip(view, note))
        return {};
    const auto geo = geometryOf(area, view);
    const int offset = note - view.lowNote;
    const int pc = offset % 12;
    const float x = geo.keys.getX() + static_cast<float>(offset / 12 * 7 + whiteOf[pc]) * geo.whiteW;
    if (!isBlack[pc])
        return {x, geo.keys.getY(), geo.whiteW, geo.keys.getHeight()};
    const float w = geo.whiteW * 0.6f;
    return {x + geo.whiteW - w * 0.5f, geo.keys.getY(), w, geo.keys.getHeight() * 0.6f};
}

void drawKeyboard(juce::Graphics& g,
                  const Theme& theme,
                  juce::Rectangle<float> area,
                  juce::Colour hue,
                  const KeyboardView& view)
{
    const auto& c = theme.colours;
    const auto geo = geometryOf(area, view);
    const int top = view.lowNote + 12 * view.octaves;
    const auto inKey = [&](int note) { return (view.scaleMask >> (note % 12) & 1u) != 0; };
    const auto footMarks = [&](int note, juce::Rectangle<float> key)
    {
        // The key in use: a dot above the foot. A key held: a bar along it.
        if (inKey(note))
        {
            g.setColour(hue.withAlpha(0.55f));
            g.fillEllipse(
                juce::Rectangle<float>(4.0f, 4.0f).withCentre({key.getCentreX(), key.getBottom() - 6.0f}));
        }
        if (view.held[static_cast<size_t>(note)])
        {
            g.setColour(c.textTertiary);
            g.fillRect(key.withTop(key.getBottom() - 3.0f).reduced(1.0f, 0.0f));
        }
    };
    // The white keys, lit ones in the hue, a canvas line between them.
    for (int note = view.lowNote; note < top; ++note)
    {
        if (isBlack[(note - view.lowNote) % 12])
            continue;
        auto key = keyBounds(area, view, note).withTrimmedRight(1.0f);
        g.setColour(c.surface3);
        g.fillRoundedRectangle(key, 2.0f);
        g.fillRect(key.withHeight(key.getHeight() - 2.0f)); // square at the top, rounded at the foot
        if (const float a = view.lit[static_cast<size_t>(note)]; a > 0.0f)
        {
            g.setColour(hue.withAlpha(a));
            g.fillRoundedRectangle(key, 2.0f);
            g.fillRect(key.withHeight(key.getHeight() - 2.0f));
        }
        footMarks(note, key);
        const auto labelFont = theme.font(Theme::Face::ui, theme.fonts.size.micro, theme.fonts.weight.medium);
        const juce::String octaveName = "C" + juce::String(note / 12 - 2); // C3 is MIDI 60 (core::noteName)
        // Named only where the name fits beside the black key: never cut.
        if ((note - view.lowNote) % 12 == 0 && static_cast<float>(juce::GlyphArrangement::getStringWidthInt(
                                                   labelFont, octaveName)) <= key.getWidth() * 0.66f - 2.0f)
        {
            g.setColour(c.textTertiary);
            g.setFont(labelFont);
            // Above the dot, clear of the black key beside it.
            g.drawText(octaveName,
                       juce::Rectangle<float>(
                           key.getX() + 2.0f, key.getBottom() - 21.0f, key.getWidth() * 0.66f, 12.0f),
                       juce::Justification::centredLeft,
                       false);
        }
    }
    // The black keys over them.
    for (int note = view.lowNote; note < top; ++note)
    {
        if (!isBlack[(note - view.lowNote) % 12])
            continue;
        const auto key = keyBounds(area, view, note);
        g.setColour(c.surface0);
        g.fillRoundedRectangle(key, 2.0f);
        if (const float a = view.lit[static_cast<size_t>(note)]; a > 0.0f)
        {
            g.setColour(hue.withAlpha(a));
            g.fillRoundedRectangle(key, 2.0f);
        }
        footMarks(note, key);
    }
    // A lit note past either end: a chevron on the end key, pointing its way.
    float below = 0.0f, above = 0.0f;
    for (int n = 0; n < 128; ++n)
    {
        if (n < view.lowNote)
            below = std::max(below, view.lit[static_cast<size_t>(n)]);
        else if (n >= top)
            above = std::max(above, view.lit[static_cast<size_t>(n)]);
    }
    const auto chevron = [&](float x, float direction, float alpha)
    {
        const float y = geo.keys.getY() + 8.0f;
        juce::Path p;
        p.addTriangle(x, y - 4.0f, x, y + 4.0f, x + 5.0f * direction, y);
        g.setColour(hue.withAlpha(alpha));
        g.fillPath(p);
    };
    if (below > 0.0f)
        chevron(geo.keys.getX() + 7.0f, -1.0f, below);
    if (above > 0.0f)
        chevron(geo.keys.getRight() - 7.0f, 1.0f, above);
}

} // namespace northlight::ui::strip
