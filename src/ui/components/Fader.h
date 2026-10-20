#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace aural::ui
{

// The mixer fader (02-design-system.md, "Fader"): a 4 px track with a 0 dB tick
// and a scale, a 36×14 cap with a track-colour line, Logic-style law (0 dB at 75%
// of the travel, +6 dB at the top, -inf at the bottom). Drag moves it (Shift =
// fine), double-click returns to 0 dB, arrows step (Shift fine), Home/End, and
// the dB value shows in a tooltip while dragging. Values are linear gain.
class Fader final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Fader(const Theme& theme);

    void setGain(float linear, juce::NotificationType notify);
    float gain() const noexcept { return gain_; }
    void setTrackColour(juce::Colour colour);
    void setMaxDb(float db) { maxDb_ = db; }

    std::function<void(float)> onGainChanged;
    std::function<void()> onDragStart, onDragEnd;

    // The law, shared with tests: travel fraction 0..1 <-> dB.
    static float dbForFraction(float fraction, float maxDb) noexcept;
    static float fractionForDb(float db, float maxDb) noexcept;
    static constexpr float minusInfinityDb = -100.0f; // db::silence

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }
    // Accessibility: a slider in dB.
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    double accessibleValue() const noexcept;
    juce::String accessibleValueText() const;
    void setAccessibleValue(double db);
    std::pair<double, double> accessibleRange() const noexcept { return {minusInfinityDb, maxDb_}; }
    double accessibleStep() const noexcept { return 0.5; }

private:
    juce::Rectangle<int> trackBounds() const noexcept;
    float fractionForY(int y) const noexcept;
    int yForFraction(float fraction) const noexcept;
    void setDb(float db, bool notify);
    float currentDb() const noexcept;
    void stepDb(float delta);

    const Theme& theme_;
    float gain_ = 1.0f;
    float maxDb_ = 6.0f;
    juce::Colour trackColour_;
    float dragStartDb_ = 0.0f;
    int dragStartY_ = 0;
    bool dragging_ = false;
};

} // namespace aural::ui
