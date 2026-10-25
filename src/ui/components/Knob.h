#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

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

    // How the knob is drawn: the house ring; a hardware face's chicken-head
    // pointer over a printed scale (design-system.md "Hardware face"); a bud,
    // a bodiless ring whose value arc ends in a lit tip (design-system.md "Bud knob");
    // or a rendered knob's pointer layer, turning over a body its face draws
    // (design-system.md "Rendered parts").
    enum class Style
    {
        ring,
        pointer,
        bud,
        rendered,
    };
    void setStyle(Style style)
    {
        style_ = style;
        repaint();
    }

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
    // The ring's diameter (default the theme's knob size): a module's main
    // controls can be larger.
    void setRingSize(int pixels)
    {
        ringSize_ = pixels;
        repaint();
    }
    float valueGap(float below) const;
    int ringSize() const noexcept { return ringSize_ > 0 ? ringSize_ : theme_.sizes.knob; }
    // Style::rendered: the pointer layer, pointing up at rest, drawn over the
    // knob's whole bounds and turned with the value; the skirt (ringSize) is what
    // takes the mouse.
    void setRenderedPointer(juce::Image pointer)
    {
        renderedPointer_ = std::move(pointer);
        repaint();
    }
    // Style::rendered: how far the pointer turns from the bottom of the range to
    // the top (radians, centred on 12 o'clock; default 270 degrees). A stepped
    // selector with few positions turns through less, so they fan across its top.
    void setSweep(float radians)
    {
        sweep_ = radians;
        repaint();
    }
    // Where the pointer points at a value (radians clockwise from 12 o'clock).
    float pointerAngle() const noexcept { return (static_cast<float>(fraction()) - 0.5f) * sweep_; }
    // Steps the value snaps to (a switch's positions, note values); 0 for none.
    void setInterval(double interval)
    {
        interval_ = interval;
        setValue(value_, juce::dontSendNotification);
    }

    // The owner's callbacks.
    std::function<void(double)> onValueChanged;
    std::function<void()> onDragStart, onDragEnd;
    // The pointer entering (true) or leaving (false) the knob.
    std::function<void(bool)> onHover;
    // Others that follow the knob besides its owner (a rendered face tracking the
    // control in hand): each told after the owner's callbacks, in the order they
    // were added, whenever that was. The owner's callbacks stay its own.
    struct Watcher
    {
        std::function<void(double)> valueChanged;
        std::function<void()> dragStart, dragEnd;
        std::function<void(bool inside)> hover;
    };
    void addWatcher(Watcher watcher) { watchers_.push_back(std::move(watcher)); }
    bool isDragging() const noexcept { return dragging_; }

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
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
    double accessibleStep() const noexcept
    {
        return interval_ > 0.0 ? interval_ : (maximum_ - minimum_) / 100.0;
    }

private:
    // The owner's callback, then every watcher's.
    void notifyValue();
    void notifyDragStart();
    void notifyDragEnd();
    void notifyHover(bool inside);
    std::vector<Watcher> watchers_;

    double fraction() const noexcept;
    void setFraction(double f, bool notify);
    void step(double direction, bool fine);
    juce::String text() const;
    void paintPointer(juce::Graphics& g);
    void paintBud(juce::Graphics& g);
    void paintRendered(juce::Graphics& g);

    const Theme& theme_;
    double minimum_ = 0.0, maximum_ = 1.0, default_ = 0.0, value_ = 0.0;
    bool showValue_ = true;
    int ringSize_ = 0; // 0: the theme's knob size
    bool logarithmic_ = false;
    Style style_ = Style::ring;
    juce::Image renderedPointer_;
    double interval_ = 0.0;
    float sweep_ = juce::MathConstants<float>::pi * 1.5f;
    juce::Colour arcColour_;
    double dragStartValue_ = 0.0; // the fraction at the start of a drag
    int dragStartY_ = 0;
    bool dragging_ = false;
};

} // namespace northlight::ui
