#pragma once

#include "ui/Motion.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace northlight::ui
{

// The floating tooltip (design-system.md, "Tooltip"): after the tooltip
// delay it fades in over 80 ms; moving to a neighbouring control keeps it up
// and swaps the text (no second delay or fade). Drawn by the look-and-feel.
class Tooltips final : public juce::TooltipWindow
{
public:
    explicit Tooltips(const Theme& theme)
        : juce::TooltipWindow(nullptr, theme.motion.tooltipDelayMs), theme_(theme)
    {
    }

    static constexpr int fadeMs = 80;

    // The app's scale, not the hovered control's. JUCE's tooltip window takes the
    // scale of the component under the mouse, so over a rendered face (scaled to
    // fit its card) it shrank and was placed in the shrunken coordinates: well
    // above and left of the pointer. Every tooltip is the same size, by the pointer.
    float getDesktopScaleFactor() const override { return juce::Component::getDesktopScaleFactor(); }

private:
    void visibilityChanged() override
    {
        if (!isVisible())
            return;
        setAlpha(0.0f);
        juce::Component::SafePointer<Tooltips> safe(this);
        motion::Tween::run(*this,
                           theme_.motion.ms(fadeMs),
                           [safe](float t)
                           {
                               if (safe != nullptr)
                                   safe->setAlpha(t);
                           });
    }

    const Theme& theme_;
};

} // namespace northlight::ui
