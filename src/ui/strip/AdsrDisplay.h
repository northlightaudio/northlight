#pragma once

#include "ui/strip/ModuleCard.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace northlight::ui::strip
{

// An ADSR envelope to drag (screens/plugins.md 7.4b, 7.4d): each stage has a
// quarter of the plot's width, its time on a log scale in it (up to its
// parameter's maximum), the sustain level up the height. The handles are the
// peak (attack), the sustain point (decay across, level up) and the end
// (release); a press takes the nearest and drags it, as one gesture. A subclass
// paints the envelope (points() and path() give its shape) and names the
// envelope the handles edit by its attack parameter (decay, sustain and release
// follow it).
class AdsrDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit AdsrDisplay(ModuleCard& card);

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

protected:
    // The attack parameter of the envelope the handles edit.
    virtual int editedAttack() const = 0;
    // Where the envelope is drawn.
    virtual juce::Rectangle<float> plot() const = 0;

    // The peak, the sustain start, the sustain end and the release end of the
    // envelope whose attack is parameter `attack`.
    std::array<juce::Point<float>, 4> points(int attack) const;
    // The envelope's line: from the plot's bottom left through its points.
    juce::Path path(const std::array<juce::Point<float>, 4>& points) const;
    // The edited envelope's three handles in `hue` (the one in hand held).
    void drawHandles(juce::Graphics& g, juce::Colour hue) const;

    ModuleCard& card_;

private:
    // A stage's longest time, the far end of its quarter.
    float maxMs(int param) const { return card_.info(param).maximum; }

    int drag_ = -1; // the handle in hand: 0 attack, 1 decay and sustain, 3 release
};

} // namespace northlight::ui::strip
