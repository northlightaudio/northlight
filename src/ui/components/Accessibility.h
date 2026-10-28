#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace northlight::ui
{

// What UI Automation sees of a control with a value (design/accessibility-performance.md
// 11.3): a slider with a readable value. `Owner` has accessibleValue(),
// accessibleValueText(), setAccessibleValue(double), accessibleRange() (a pair)
// and accessibleStep().
template <typename Owner> class AccessibleValue final : public juce::AccessibilityValueInterface
{
public:
    explicit AccessibleValue(Owner& owner) : owner_(owner) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return owner_.accessibleValue(); }
    juce::String getCurrentValueAsString() const override { return owner_.accessibleValueText(); }
    void setValue(double newValue) override { owner_.setAccessibleValue(newValue); }
    void setValueAsString(const juce::String& newValue) override
    {
        owner_.setAccessibleValue(newValue.getDoubleValue());
    }
    AccessibleValueRange getRange() const override
    {
        const auto range = owner_.accessibleRange();
        return {{range.first, range.second}, owner_.accessibleStep()};
    }

private:
    Owner& owner_;
};

// A control's spoken name: its title, else its tooltip up to the shortcut (the
// tooltip's "Name  Ctrl+X" form, as IconButton titles itself).
inline juce::String accessibleName(const juce::Component& component, const juce::String& tooltip)
{
    if (component.getTitle().isNotEmpty())
        return component.getTitle();
    return tooltip.upToFirstOccurrenceOf("  ", false, false);
}

// A handler for a custom control that keeps its name in its tooltip (most of
// ours do): the name falls back to the tooltip, and the tooltip is the help text.
class TooltipNamedHandler final : public juce::AccessibilityHandler
{
public:
    TooltipNamedHandler(juce::Component& component,
                        juce::SettableTooltipClient& tooltip,
                        juce::AccessibilityRole role,
                        juce::AccessibilityActions actions = {},
                        Interfaces interfaces = {})
        : juce::AccessibilityHandler(component, role, std::move(actions), std::move(interfaces)),
          tooltip_(tooltip)
    {
    }
    juce::String getTitle() const override { return accessibleName(getComponent(), tooltip_.getTooltip()); }
    juce::String getHelp() const override
    {
        const auto tip = tooltip_.getTooltip();
        return tip == getTitle() ? juce::String() : tip;
    }

private:
    juce::SettableTooltipClient& tooltip_; // getTooltip() is not const
};

// What UI Automation sees of a part that only shows a reading (a lamp, a needle,
// a jewel): read-only text named by its title, whose value is the component's
// description. The owner keeps the description current; it is read on demand,
// never announced, so a moving reading does not chatter.
inline std::unique_ptr<juce::AccessibilityHandler> readoutHandler(juce::Component& component)
{
    struct Reading final : juce::AccessibilityTextValueInterface
    {
        explicit Reading(juce::Component& owner) : owner_(owner) {}
        bool isReadOnly() const override { return true; }
        juce::String getCurrentValueAsString() const override { return owner_.getDescription(); }
        void setValueAsString(const juce::String&) override {}
        juce::Component& owner_;
    };
    return std::make_unique<juce::AccessibilityHandler>(
        component,
        juce::AccessibilityRole::staticText,
        juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces{std::make_unique<Reading>(component)});
}

// Sets a button's on state and makes it a checkable toggle for UI Automation.
// JUCE only reports a button as a toggle when it toggles itself on click, so a
// button whose state comes from the model reads as a plain button without this.
inline void showToggle(juce::Button& button, bool on)
{
    button.setToggleable(true);
    button.setToggleState(on, juce::dontSendNotification);
}

// Remembers what had the keyboard focus when a window or panel opened, and
// gives it back when that closes, so the focus is never lost (11.2).
class FocusReturn
{
public:
    FocusReturn() : opener_(juce::Component::getCurrentlyFocusedComponent()) {}
    void restore()
    {
        if (opener_ != nullptr && opener_->isShowing())
            opener_->grabKeyboardFocus();
    }

private:
    juce::Component::SafePointer<juce::Component> opener_;
};

// Names a per-track control after its track ("Mute Bass"), so a screen reader
// moving through a strip or header hears more than "M".
inline void titleForTrack(juce::Component& control, const juce::String& action, const juce::String& track)
{
    control.setTitle(track.isEmpty() ? action : action + " " + track);
}

} // namespace northlight::ui
