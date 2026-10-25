#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// Parts several hardware faces share (design-system.md "Rendered parts"): a VU
// meter's needle under its rendered hood and glass, Corona's red jewel as a
// pilot lamp, and Corona's chrome footswitch. Each has a flat fallback in the
// hardware tokens for when its renders are missing.

namespace northlight::ui::strip
{

// A VU meter's moving parts: the needle and its shadow on the dial, then the
// pivot's hood and the glass over them, from a face's renders. The dial and the
// printed scale are the face's.
class VuNeedleMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    using HardwareColour = juce::Colour Theme::Hardware::*;
    struct Look
    {
        // The drawing's own units and how they land on the component (a meter
        // rendered at another size than the face's).
        juce::AffineTransform transform;
        // What the hood's and the glass's images cover, in those units.
        juce::Rectangle<float> frame;
        // The needle is clipped to this (the dial), unless it is empty.
        juce::Rectangle<float> clip;
        // The needle: from `base` to `tip` out from the pivot, `width` wide, and
        // its shadow the same line moved by `shadowOffset`.
        juce::Point<float> pivot;
        float base = 0.0f, tip = 0.0f, width = 1.0f;
        HardwareColour needle = nullptr;
        juce::Point<float> shadowOffset;
        float shadowWidth = 1.0f, shadowAlpha = 0.25f;
        // The renders (in assets/<folder>).
        const char* folder = "";
        const char* hoodFile = "";
        const char* glassFile = "";
        // Without the hood's render: a trapezoid over the needle's foot, centred
        // on `hoodCentreX`, `hoodFoot` its bottom edge.
        float hoodCentreX = 0.0f, hoodFoot = 0.0f, hoodFootHalfWidth = 0.0f, hoodTopHalfWidth = 0.0f,
              hoodHeight = 0.0f;
        HardwareColour hood = nullptr;
        // The scale's half sweep (degrees each side of upright) and the reading
        // the description stops at.
        float halfSweep = 50.0f;
    };

    VuNeedleMeter(const Theme& theme, Look look);

    // A reading in VU: the needle moves (and repaints) when its angle changes.
    void setReading(float vu);
    float angle() const noexcept { return angle_; }

    void paint(juce::Graphics& g) override;

private:
    const Theme& theme_;
    Look look_;
    float angle_;
};

// The pilot lamp: Corona's red jewel, lit while `lit` says so.
class JewelLamp final : public juce::Component, public juce::SettableTooltipClient
{
public:
    JewelLamp(const Theme& theme, std::function<bool()> lit);

    void paint(juce::Graphics& g) override;

private:
    const Theme& theme_;
    std::function<bool()> lit_;
};

// Corona's chrome footswitch, pressed while the mouse holds it; a click, Space or
// Enter stomps it. With an LED (Corona's REVERB and TREMOLO), the LED sits to the
// button's right and set() lights it; without (a pedal's bypass), the component is
// the button's square.
class Footswitch final : public juce::Component, public juce::SettableTooltipClient
{
public:
    // `name` for the tooltip ("Reverb: On"); the accessible title adds "footswitch".
    Footswitch(const Theme& theme, juce::String name, bool led);

    std::function<void()> onPress;

    // On or off, and the LED's brightness while on.
    void set(bool on, float glow);

    // The share of the button's size its image reaches past it on each side (the
    // render's rim and shadow), and the room the LED takes to its right.
    static constexpr float margin = 0.3f;
    static constexpr float ledRoom = 12.0f;
    // The component's bounds for a button `size` across centred on `centre`.
    static juce::Rectangle<float> boundsFor(juce::Point<float> centre, float size, bool led);

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;
    void mouseDown(const juce::MouseEvent&) override { repaint(); }
    void mouseUp(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    juce::Rectangle<float> buttonBounds() const;
    void press();

    const Theme& theme_;
    juce::String title_;
    bool led_;
    bool on_ = false;
    float glow_ = 1.0f;
};

} // namespace northlight::ui::strip
