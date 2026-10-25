#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace northlight::ui::strip
{

// A bakelite key on a hardware face (design-system.md "Hardware key"): its label
// printed on it, a LED at its left lit while it is chosen (a radio key) or on (a
// latching one), pressed in a pixel while lit or held. A square key with an icon
// puts the icon over the label and the LED in its top left corner. Click, Space
// or Enter presses it; it has hover, pressed, disabled and focus states. A
// rendered key (setRendered) is drawn from patches of its face's renders instead,
// lit by its own glow rather than a LED.
class HardwareKey final : public juce::Component, public juce::SettableTooltipClient
{
public:
    // `title` names it for the tooltip and accessibility; `hasLed` false for a
    // plain button (LOAD IR).
    HardwareKey(const Theme& theme, juce::String label, juce::String title, bool hasLed = true);

    std::function<void()> onPress;
    // An icon drawn over the label (a square key), in the label's colour; with no
    // label, the icon alone, centred on the key.
    std::function<void(juce::Graphics&, juce::Rectangle<float>)> icon;

    void setLit(bool lit);
    bool lit() const noexcept { return lit_; }
    void setLabel(const juce::String& label);

    // Drawn from `off` and `on`, patches of the face's renders covering the key's
    // bounds (unlit, and lit with its glow on the panel round it), and while held
    // down from `heldOff` and `heldOn` (the same with the key pushed in); `face` is
    // the key itself within the bounds, where the label is printed in `ink` (or
    // `litInk` while lit). Without the images, flat keys in `flat` and `flatLit`.
    struct Rendered
    {
        juce::Image off, on, heldOff, heldOn;
        juce::Rectangle<float> face;
        juce::Colour ink, litInk, flat, flatLit;
    };
    void setRendered(Rendered rendered);
    // The label's size in px (14 by default; larger on a face scaled down).
    void setLabelSize(float size);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    void enablementChanged() override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void press();
    void paintLed(juce::Graphics& g, juce::Point<float> centre) const;
    void paintRendered(juce::Graphics& g);

    const Theme& theme_;
    juce::String label_;
    bool hasLed_ = true, lit_ = false;
    bool rendered_ = false;
    Rendered parts_;
    float labelSize_ = 14.0f;
};

} // namespace northlight::ui::strip
