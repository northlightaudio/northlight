#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace northlight::ui
{

// A 28 px ring knob (pan, send level, plugin parameters): a surface2 track, an
// accent value arc (from the centre for bipolar values, from the start
// otherwise), a textPrimary indicator, the value as text below when there is
// room. Vertical drag (Shift = fine), double-click resets, wheel and arrows step.
class Knob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Knob(const Theme& theme);

    // The value range; bipolar knobs are those whose range crosses zero.
    void setRange(double minimum, double maximum, double defaultValue);
    // Travel proportional to the logarithm of the value (frequencies, times,
    // ratios); only for a positive range.
    void setLogarithmic(bool logarithmic);
    // The arc's colour (default accent): a channel strip module's own hue.
    void setArcColour(juce::Colour colour)
    {
        arcColour_ = colour;
        repaint();
    }
    void setValue(double value, juce::NotificationType notify);
    double value() const noexcept { return value_; }
    // Formats the value for the label and the tooltip.
    std::function<juce::String(double)> format;
    // Shown under the ring when there is room.
    void setShowValue(bool show) { showValue_ = show; }

    std::function<void(double)> onValueChanged;
    std::function<void()> onDragStart, onDragEnd;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    // Accessibility: a slider over the range.
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    double accessibleValue() const noexcept { return value_; }
    juce::String accessibleValueText() const { return format ? format(value_) : juce::String(value_, 2); }
    void setAccessibleValue(double value) { setValue(value, juce::sendNotification); }
    std::pair<double, double> accessibleRange() const noexcept { return {minimum_, maximum_}; }
    double accessibleStep() const noexcept { return (maximum_ - minimum_) / 100.0; }

private:
    double fraction() const noexcept;
    void setFraction(double f, bool notify);
    void step(double direction, bool fine);
    juce::String text() const;

    const Theme& theme_;
    double minimum_ = 0.0, maximum_ = 1.0, default_ = 0.0, value_ = 0.0;
    bool showValue_ = true;
    bool logarithmic_ = false;
    juce::Colour arcColour_;
    double dragStartValue_ = 0.0; // the fraction at the start of a drag
    int dragStartY_ = 0;
    bool dragging_ = false;
};

} // namespace northlight::ui
