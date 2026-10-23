#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace northlight::ui
{

// A stepped rotary switch (design-system.md "Rotary selector"): a knob body whose
// pointer snaps between a few labelled positions printed around it, for a
// parameter a console would put on a switch. Click a label, drag vertically,
// scroll or use the arrow keys; double-click returns to the default.
class RotarySelector final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int bodySize = 34;

    explicit RotarySelector(const Theme& theme);
    ~RotarySelector() override;

    // The short labels around the switch, one per position, and the full names
    // for the tooltip ("10 ms" for the label "10"); `name` titles the tooltip.
    void
    setPositions(std::vector<juce::String> labels, std::vector<juce::String> fullNames, juce::String name);
    void setSelected(int index, juce::NotificationType notification);
    int selected() const noexcept { return selected_; }
    void setDefault(int index) noexcept { default_ = index; }
    void setHue(juce::Colour hue);
    // Drawn for a hardware face: a chicken-head pointer with printed legends
    // (design-system.md "Hardware face").
    void setHardware(bool hardware)
    {
        hardware_ = hardware;
        repaint();
    }

    // The size the labels need around the body.
    int preferredWidth() const noexcept { return bodySize + 2 * (labelReach + labelHalfWidth); }
    // Labels above the body and, for the lowest of six positions, a little below
    // the body's centre.
    int preferredHeight() const noexcept { return labelReach + 6 + bodySize / 2 + 22; }

    std::function<void(int index)> onChange;
    // A drag, from press to release: one gesture, one undo step.
    std::function<void()> onGestureBegin, onGestureEnd;

    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
    void enablementChanged() override { repaint(); }

    // Where a position's label and dot sit (for tests and hit testing).
    juce::Point<float> labelCentre(int index) const;

private:
    static constexpr int labelReach = 18;     // from the rim to a label's centre
    static constexpr int labelHalfWidth = 17; // half a label's box (four characters)
    static constexpr int dragStep = 16;       // pixels per position

    juce::Point<float> centre() const;
    void paintHardware(juce::Graphics& g, bool hover);
    float angleOf(float position) const;
    int positionAt(juce::Point<float> p) const;
    void choose(int index, bool fromUser);
    void updateTooltip();

    const Theme& theme_;
    std::vector<juce::String> labels_, fullNames_;
    juce::String name_;
    juce::Colour hue_;
    int selected_ = 0, default_ = 0, hoverLabel_ = -1;
    float shown_ = 0.0f; // the pointer's position, easing to selected_
    int dragOrigin_ = 0, dragStart_ = 0;
    bool dragging_ = false, clickedLabel_ = false, hardware_ = false;
};

} // namespace northlight::ui
