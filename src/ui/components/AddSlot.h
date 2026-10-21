#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace northlight::ui
{

// An empty slot that adds something (an insert, a send): the shape of a
// filled slot drawn as a dashed lineStrong outline, radius sm, with "+ Name"
// in caption textTertiary near its top (centred when the slot is a single
// row). Hover lifts it to surface1 and textSecondary (design-system.md 2.7).
class AddSlot final : public juce::Button
{
public:
    AddSlot(const Theme& theme, const juce::String& name) : juce::Button("+ " + name), theme_(theme)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        const auto& c = theme_.colours;
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        const bool live = isEnabled() && (isMouseOver || isButtonDown);
        if (live)
        {
            g.setColour(isButtonDown ? c.surfacePressed : c.surface1);
            g.fillRoundedRectangle(r, theme_.radius.sm);
        }
        juce::Path outline;
        outline.addRoundedRectangle(r, theme_.radius.sm);
        juce::Path dashed;
        const float dashes[] = {3.0f, 3.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(live ? c.lineStrong.brighter(0.2f) : c.lineStrong);
        g.fillPath(dashed);
        g.setFont(theme_.caption());
        g.setColour(!isEnabled() ? c.textDisabled : live ? c.textSecondary : c.textTertiary);
        const int tall = static_cast<int>(theme_.fonts.size.caption) + 2 * theme_.spacing.sm;
        const auto label = getHeight() > 2 * tall ? getLocalBounds().removeFromTop(tall) : getLocalBounds();
        g.drawFittedText(
            getButtonText(), label.reduced(theme_.spacing.xs, 0), juce::Justification::centred, 1, 0.8f);
    }

private:
    const Theme& theme_;
};

} // namespace northlight::ui
