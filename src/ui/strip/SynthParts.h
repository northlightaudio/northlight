#pragma once

#include "engine/Insert.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/OpticalCentre.h"
#include "ui/Theme.h"
#include "ui/Wells.h"
#include "ui/strip/CardParts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <functional>
#include <vector>

// Pieces the bundled synths' editors share (screens/plugins.md 7.4b-7.4d): a
// display's screen, a corner chip that opens a list, and a row of
// pills (shared with the effects: Bloom's interval, Vesper's space).

namespace northlight::ui::strip
{

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
    // The choices of a stepped parameter, as the processor names them.
    void setChoices(const engine::ParameterInfo& info)
    {
        choices.clear();
        for (const auto& c : info.choices)
            choices.push_back(juce::String::fromUTF8(c.c_str()));
    }
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

// A row of pills, one per choice (Bloom's shimmer interval, Vesper's space): the
// chosen one filled in the hue, the others outlined. Click one, or Left and Right
// (Home, End) to step.
class PillRow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    PillRow(const Theme& theme, juce::Colour hue, const juce::String& tooltip) : theme_(theme), hue_(hue)
    {
        setWantsKeyboardFocus(true);
        setRepaintsOnMouseActivity(true);
        setTooltip(tooltip);
        setTitle(tooltip);
    }

    std::vector<juce::String> labels;
    int selected = 3;
    // Words ("Plate") in the label face; numbers ("+12") in the value face, the default.
    bool words = false;
    std::function<void(int)> onChange;

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        const int hover = isMouseOver() ? indexAt(getMouseXYRelative().x) : -1;
        g.setFont(words ? theme_.label() : theme_.value());
        for (int i = 0; i < count(); ++i)
        {
            const auto r = pill(i);
            const bool chosen = i == selected;
            if (chosen)
            {
                g.setColour(isEnabled() ? hue_ : c.textDisabled);
                g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
            }
            else
            {
                if (i == hover)
                {
                    g.setColour(c.surface2);
                    g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
                }
                g.setColour(c.textPrimary.withAlpha(0.15f));
                g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f);
            }
            g.setColour(chosen ? c.canvas : i == hover ? c.textPrimary : c.textSecondary);
            drawOpticallyCentred(g, labels[static_cast<size_t>(i)], r);
        }
        if (auto* laf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel());
            laf != nullptr && count() > 0)
            laf->drawFocusRing(g, *this, pill(selected), pill(selected).getHeight() * 0.5f);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (const int i = indexAt(e.x); i >= 0)
            choose(i);
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::leftKey)
            choose(std::max(0, selected - 1));
        else if (key == juce::KeyPress::rightKey)
            choose(std::min(count() - 1, selected + 1));
        else if (key == juce::KeyPress::homeKey)
            choose(0);
        else if (key == juce::KeyPress::endKey)
            choose(count() - 1);
        else
            return false;
        return true;
    }

private:
    int count() const noexcept { return static_cast<int>(labels.size()); }
    juce::Rectangle<float> pill(int i) const
    {
        constexpr float gap = 6.0f;
        const auto area = getLocalBounds().toFloat();
        const float w =
            (area.getWidth() - gap * static_cast<float>(count() - 1)) / static_cast<float>(count());
        return juce::Rectangle<float>(
            area.getX() + (w + gap) * static_cast<float>(i), area.getY(), w, area.getHeight());
    }
    int indexAt(int x) const
    {
        for (int i = 0; i < count(); ++i)
            if (pill(i).expanded(3.0f, 0.0f).contains(static_cast<float>(x), pill(i).getCentreY()))
                return i;
        return -1;
    }
    void choose(int i)
    {
        if (i == selected)
            return;
        selected = i;
        repaint();
        if (onChange)
            onChange(i);
    }

    const Theme& theme_;
    juce::Colour hue_;
};

} // namespace northlight::ui::strip
