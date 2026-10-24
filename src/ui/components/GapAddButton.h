#pragma once

#include "ui/NorthlightLookAndFeel.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

// A round "+" that adds something between two items in a row (design-system.md
// 2.7, "Gap add button"): it shows only while the pointer is in the gap or it
// has keyboard focus, and takes no clicks otherwise, so it never covers the
// items' edges at rest. iconLarge across: a disc in the control material
// (design-system.md 2.4) with a 1 px lineStrong outline and the add icon
// (iconSmall) in textSecondary; hover the face a step lighter and textPrimary,
// pressed the face pressed in, focus the focus ring.
class GapAddButton final : public juce::Button
{
public:
    GapAddButton(const Theme& theme, int index) : juce::Button("Add here"), theme_(theme), index_(index)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus(true);
    }

    // The position in the row an item added here takes.
    int index() const noexcept { return index_; }

    // Shown while the pointer is in its gap (or its menu is open).
    void setShown(bool shown)
    {
        if (shown == shown_)
            return;
        shown_ = shown;
        repaint();
    }
    bool isShown() const noexcept { return shown_; }
    // What a pointer or a key sees: shown, or reached by Tab.
    bool isLive() const { return shown_ || hasKeyboardFocus(false); }

    bool hitTest(int x, int y) override
    {
        if (!isLive())
            return false;
        return getLocalBounds().toFloat().getCentre().getDistanceFrom(
                   {static_cast<float>(x), static_cast<float>(y)}) <= static_cast<float>(getWidth()) * 0.5f;
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key.isKeyCode(juce::KeyPress::spaceKey))
        {
            triggerClick();
            return true;
        }
        return juce::Button::keyPressed(key);
    }

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        if (!isLive())
            return;
        const auto& c = theme_.colours;
        const float ring = static_cast<float>(theme_.sizes.focusRing) + 1.0f;
        const auto disc = getLocalBounds().toFloat().reduced(ring);
        const bool over = shown_ && (isMouseOver || isButtonDown);
        const float radius = disc.getWidth() * 0.5f;
        auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel());
        if (lnf != nullptr)
            lnf->drawControlFace(g, disc, radius, c.surface2, isButtonDown, over);
        else
        {
            g.setColour(isButtonDown ? c.surfacePressed : c.surface2);
            g.fillEllipse(disc);
        }
        g.setColour(c.lineStrong);
        g.drawEllipse(disc.reduced(0.5f), 1.0f);
        g.setColour(over ? c.textPrimary : c.textSecondary);
        drawPlus(g, disc.getCentre());
        if (lnf != nullptr)
            lnf->drawFocusRing(g, *this, disc, radius);
    }

    // The component's side: the disc plus room for the focus ring around it.
    static int sizeFor(const Theme& theme) { return theme.sizes.iconLarge + 2 * (theme.sizes.focusRing + 1); }

private:
    // The add icon's cross (10 of iconSmall's 14 px, 1.5 px strokes), as two bars
    // snapped to whole device pixels: the icon path scaled to this size puts its
    // strokes across a pixel edge, which blurs them into a doubled look.
    void drawPlus(juce::Graphics& g, juce::Point<float> centre) const
    {
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const auto snap = [scale](float v) { return std::round(v * scale) / scale; };
        const float icon = static_cast<float>(theme_.sizes.iconSmall);
        // In device pixels, with the arm and the stroke both odd or both even so
        // the bars cross exactly in the middle.
        float armPx = std::max(1.0f, std::round(icon * 10.0f / 16.0f * scale));
        const float strokePx = std::max(1.0f, std::round(1.5f * icon / 16.0f * scale));
        if (static_cast<int>(armPx - strokePx) % 2 != 0)
            armPx += 1.0f;
        const float arm = armPx / scale, stroke = strokePx / scale;
        const float left = snap(centre.x - arm * 0.5f), top = snap(centre.y - arm * 0.5f);
        const float x = left + (arm - stroke) * 0.5f, y = top + (arm - stroke) * 0.5f;
        g.fillRect(juce::Rectangle<float>(left, y, arm, stroke));
        g.fillRect(juce::Rectangle<float>(x, top, stroke, arm));
    }

    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    const Theme& theme_;
    int index_;
    bool shown_ = false;
};

} // namespace northlight::ui
