#pragma once

#include "ui/NorthlightLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace northlight::ui
{

// A list box that shows the region focus ring when it, or one of its rows, has
// keyboard focus (design-system.md 2.7, "Focus on a region"). Otherwise a
// juce::ListBox.
class FocusListBox final : public juce::ListBox
{
public:
    using juce::ListBox::ListBox;

    void paintOverChildren(juce::Graphics& g) override
    {
        juce::ListBox::paintOverChildren(g);
        if (auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
            lnf->drawRegionFocusRing(g, *this, getLocalBounds(), true);
    }
    void focusOfChildComponentChanged(FocusChangeType) override { repaint(); }
    void focusGained(FocusChangeType cause) override
    {
        juce::ListBox::focusGained(cause);
        repaint();
    }
    void focusLost(FocusChangeType) override { repaint(); }
};

} // namespace northlight::ui
