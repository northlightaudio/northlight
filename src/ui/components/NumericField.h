#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace northlight::ui
{

// A numeric field (design-system.md): shows a value in the mono `value` face
// with its unit, right aligned. Pointer down and vertical drag changes it (1 px =
// 1 step; Shift = fine, 0.1 step; Ctrl = coarse, 10 steps); double-click types;
// Enter commits, Esc reverts; the wheel steps while focused.
class NumericField final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit NumericField(const Theme& theme);

    void setRange(double minimum, double maximum, double step);
    void setValue(double value, juce::NotificationType notify);
    double value() const noexcept { return value_; }
    void setUnit(const juce::String& unit);
    void setDecimals(int decimals) { decimals_ = decimals; }
    // Drawn as part of a readout well (the transport's tempo): no field of its
    // own, the value then its unit from the left on the well's shared baseline
    // (components/Readout.h).
    void setReadout(bool readout)
    {
        readout_ = readout;
        repaint();
    }
    // In readout mode: as wide as the value and unit plus the readout inset on
    // each side.
    int readoutWidth() const;
    // Formats the value (without the unit); default fixed decimals.
    std::function<juce::String(double)> format;
    std::function<double(const juce::String&)> parse;

    std::function<void(double)> onValueChanged;
    std::function<void()> onDragStart, onDragEnd;
    // A key the owner wants first while the field is focused (tap tempo on T);
    // return true to consume it.
    std::function<bool(const juce::KeyPress&)> onKey;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    // Accessibility: a slider whose value reads with its unit.
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    double accessibleValue() const noexcept { return value_; }
    juce::String accessibleValueText() const
    {
        return text() + (unit_.isEmpty() ? juce::String() : " " + unit_);
    }
    void setAccessibleValue(double value) { setValue(value, juce::sendNotification); }
    std::pair<double, double> accessibleRange() const noexcept { return {minimum_, maximum_}; }
    double accessibleStep() const noexcept { return step_; }

private:
    void beginEditing();
    void endEditing(bool commit);
    juce::String text() const;
    void stepBy(double steps);

    const Theme& theme_;
    double minimum_ = 0.0, maximum_ = 1.0, step_ = 0.01, value_ = 0.0;
    int decimals_ = 2;
    bool readout_ = false;
    juce::String unit_;
    std::unique_ptr<juce::TextEditor> editor_;
    double dragStartValue_ = 0.0;
    int dragStartY_ = 0;
    bool dragging_ = false, dragged_ = false;
};

} // namespace northlight::ui
