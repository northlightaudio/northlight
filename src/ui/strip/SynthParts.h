#pragma once

#include "ui/Theme.h"
#include "ui/Wells.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

// Pieces the bundled synths' editors share (07-plugins.md 7.4b-7.4d): the
// drag handle, a display's screen, and a corner chip that opens a list.

namespace aural::ui::strip
{

// A draggable point: a 9 px textPrimary disc with a ring in the hue (and a
// halo while it is held).
inline void
drawHandle(juce::Graphics& g, const Theme& theme, juce::Colour hue, juce::Point<float> at, bool active)
{
    const auto r = juce::Rectangle<float>(9.0f, 9.0f).withCentre(at);
    if (active)
    {
        g.setColour(hue.withAlpha(0.3f));
        g.fillEllipse(r.expanded(4.0f));
    }
    g.setColour(theme.colours.textPrimary.withAlpha(active ? 1.0f : 0.85f));
    g.fillEllipse(r);
    g.setColour(hue);
    g.drawEllipse(r, 1.5f);
}

// A display's glass: darker than the panel, with the well's frame.
inline void drawScreen(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> r)
{
    drawWell(g, theme, r);
}

// A small label in a display's corner that opens a list (the filter mode, the
// LFO shape, the envelope being edited).
class CornerChoice final : public juce::Component, public juce::SettableTooltipClient
{
public:
    CornerChoice(const Theme& theme, juce::Colour hue) : theme_(theme), hue_(hue)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    std::vector<juce::String> choices;
    int selected = 0;
    std::function<void(int)> onChoose;

    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(hue_.withAlpha(isMouseOver() ? 0.32f : 0.2f));
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(hue_);
        g.setFont(
            theme_.font(Theme::Face::ui, theme_.fonts.size.caption - 1.0f, theme_.fonts.weight.semibold));
        const auto text = selected >= 0 && static_cast<size_t>(selected) < choices.size()
                              ? choices[static_cast<size_t>(selected)]
                              : juce::String();
        g.drawText(
            text + juce::String::fromUTF8(" \xe2\x96\xbe"), getLocalBounds(), juce::Justification::centred);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override
    {
        juce::PopupMenu menu;
        for (size_t i = 0; i < choices.size(); ++i)
            menu.addItem(static_cast<int>(i) + 1, choices[i], true, static_cast<int>(i) == selected);
        juce::Component::SafePointer<CornerChoice> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                           [safe](int r)
                           {
                               if (safe != nullptr && r > 0 && safe->onChoose)
                                   safe->onChoose(r - 1);
                           });
    }
    int preferredWidth() const
    {
        int w = 0;
        const auto font =
            theme_.font(Theme::Face::ui, theme_.fonts.size.caption - 1.0f, theme_.fonts.weight.semibold);
        for (const auto& c : choices)
            w = std::max(w, juce::GlyphArrangement::getStringWidthInt(font, c));
        return w + 26;
    }

private:
    const Theme& theme_;
    juce::Colour hue_;
};

} // namespace aural::ui::strip
