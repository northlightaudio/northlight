#pragma once

#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/components/SegmentedControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui::strip
{

// A module card's chrome (screens/channel-strip.md 12.2; ModuleCard's): the
// surface, the 34 px header (the hue line, the power button, the name, a model
// switch for a slot with several models, the live status, the collapse chevron),
// the dimming of a switched-off body, and the narrow tab a collapsed card
// becomes. Embedded in another frame (an insert's card), it draws nothing and
// its buttons hide. Its buttons are the card's children; what they ask for goes
// to the hooks.
class CardFrame
{
public:
    static constexpr int headerHeight = 34;
    static constexpr int hueLine = 2;
    static constexpr int collapsedWidth = 34;

    CardFrame(juce::Component& card, const Theme& theme, std::string type, juce::Colour colour);
    ~CardFrame();

    // The power button was pressed: the module switched on (true) or off.
    std::function<void(bool on)> onPower;
    // The chevron asked to collapse the card.
    std::function<void()> onCollapse;
    // The model switch chose another model of the slot (plugins::strip::alternatives).
    std::function<void(const std::string& type)> onSwapModel;

    void setBypass(bool bypass);
    bool bypassed() const noexcept { return bypass_; }
    // Collapsed: every child but the power button hides until it expands.
    void setCollapsed(bool collapsed);
    bool isCollapsed() const noexcept { return collapsed_; }
    void setEmbedded(bool embedded);
    bool isEmbedded() const noexcept { return embedded_; }
    // The header's live readout; true when what it shows changed (the caller
    // repaints the header).
    bool showStatus(const juce::String& status);

    // Places the header's controls in the card's bounds and returns the body
    // (empty while collapsed).
    juce::Rectangle<int> layout();
    void paint(juce::Graphics& g, juce::Colour statusColour) const;
    void paintOverChildren(juce::Graphics& g) const;

private:
    juce::Rectangle<int> headerArea() const;
    // The header's name: the slot's ("Compressor") when the card has a model switch.
    juce::String headerName() const;

    juce::Component& card_;
    const Theme& theme_;
    const std::string type_;
    const juce::Colour colour_;
    IconButton power_;
    std::unique_ptr<juce::Button> collapse_;
    std::unique_ptr<SegmentedControl> modelSwitch_; // a slot with several models (Clean / Opto)
    bool bypass_ = true, collapsed_ = false, embedded_ = false;
    std::vector<juce::Component::SafePointer<juce::Component>> hiddenByCollapse_;
    juce::String shownStatus_;
};

} // namespace northlight::ui::strip
