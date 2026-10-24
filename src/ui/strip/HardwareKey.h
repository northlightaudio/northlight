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
// or Enter presses it; it has hover, pressed, disabled and focus states.
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

    const Theme& theme_;
    juce::String label_;
    bool hasLed_ = true, lit_ = false;
};

} // namespace northlight::ui::strip
