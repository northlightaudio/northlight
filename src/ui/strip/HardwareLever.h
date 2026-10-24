#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

namespace northlight::ui::strip
{

// A bat-handle toggle on a rendered hardware face (design-system.md "Rendered
// parts"), drawn from a render of each position: up, (centre,) down. A click
// anywhere on the switch flips a two-way lever; on a three-way one, a click above
// its middle moves it up a position, below it down. Up and Down move it, Space
// flips a two-way lever or steps a three-way one. The image's empty margins at
// the sides do not take the mouse. Without its images it draws a washer and a
// line in the hardware tokens.
class HardwareLever final : public juce::Component, public juce::SettableTooltipClient
{
public:
    // `folder` and `files` name the renders under assets/ (up, centre, down; the
    // centre is only used by a three-way lever).
    HardwareLever(const Theme& theme,
                  juce::String name,
                  std::vector<juce::String> positions,
                  juce::String folder,
                  std::array<juce::String, 3> files);

    std::function<void(int position)> onChoose;

    void setPosition(int position);
    int position() const noexcept { return position_; }
    // The nut's diameter in the component's pixels (52 at the render's full size).
    void setNutSize(float pixels)
    {
        nutSize_ = pixels;
        repaint();
    }

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    void enablementChanged() override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    // The washer and nut, centred in the image's top square: what a hand would push.
    juce::Rectangle<float> nutBounds() const;
    juce::Rectangle<float> switchBounds() const;
    void press();
    void move(int by);
    void choose(int position);
    void update();

    const Theme& theme_;
    juce::String name_;
    std::vector<juce::String> positions_;
    juce::String folder_;
    std::array<juce::String, 3> files_;
    int position_ = 0;
    float nutSize_ = 52.0f;
};

} // namespace northlight::ui::strip
