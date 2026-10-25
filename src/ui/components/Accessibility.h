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

} // namespace northlight::ui
