#pragma once

// Patina's stage windows (screens/plugins.md 7.2a): the lit glass in each stage's
// bezel with its live readout (Heat's curve, Chamber's tail, Drift's pitch,
// Grain's noise, Wear's bandwidth, Crush's waveform) and the lamps under it.

#include "plugins/Patina.h"
#include "ui/Theme.h"
#include "ui/strip/PatinaFace.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <vector>

namespace northlight::ui::strip::patinaface
{

using Telemetry = plugins::patina::Telemetry;

// What a stage's window reads of its card (Patina's) and asks of it.
class WindowHost
{
public:
    virtual ~WindowHost() = default;
    virtual const Theme& theme() const noexcept = 0;
    virtual float param(int index) const noexcept = 0;
    virtual bool stageOn(int stage) const noexcept = 0;
    // The telemetry history, oldest first, and the frames received in all.
    virtual int historyCount() const noexcept = 0;
    virtual std::uint64_t historyTotal() const noexcept = 0;
    virtual const Telemetry::Frame& frameAt(int i) const noexcept = 0;
    virtual const Telemetry::Frame& newest() const noexcept = 0;
    virtual const Telemetry* telemetry() const noexcept = 0;
    // The amount a stage is set to after Age (where its lamps' dim tail ends).
    virtual float setAmount(int stage) const noexcept = 0;
    virtual void toggleStage(int stage) = 0;
    virtual void focusWindow(int stage) = 0;
};

// ----- a stage's window -------------------------------------------------------------------------

// The lit glass in a stage's bezel, its live readout, and the row of lamps under
// the bezel: the window's bounds run from the glass's top to the lamps' foot, and
// it draws nothing over the bezel between them.
class StageWindow final : public juce::Component, public juce::SettableTooltipClient
{
public:
    StageWindow(WindowHost& card, int stage);

    void paint(juce::Graphics& g) override;
    bool hitTest(int x, int y) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

    // The window's glass, where the readout draws (its own pixels).
    juce::Rectangle<float> well() const;

private:
    // What a readout draws with: the window's ink, lit or unlit.
    struct Inks
    {
        juce::Colour hue, textPrimary, textSecondary, textTertiary, gridBar, dotRing;
    };

    void paintLamps(juce::Graphics& g);
    void paintHeat(juce::Graphics& g, juce::Rectangle<float> p);
    void paintChamber(juce::Graphics& g, juce::Rectangle<float> p);
    void paintDrift(juce::Graphics& g, juce::Rectangle<float> p);
    void paintGrain(juce::Graphics& g, juce::Rectangle<float> p);
    void paintWear(juce::Graphics& g, juce::Rectangle<float> p);
    void paintCrush(juce::Graphics& g, juce::Rectangle<float> p);
    void readout(juce::Graphics& g, juce::Rectangle<float> p, const juce::String& text, juce::Colour colour);
    void scaleLabel(juce::Graphics& g, juce::Rectangle<float> p, const juce::String& text);

    WindowHost& card_;
    const int stage_;
    Inks inks_;
    std::vector<float> a_, b_; // per column, reused
    std::vector<float> scopeIn_, scopeOut_;
};

// A strip along the top of each readout for its figure.
constexpr float readoutRoom = 16.0f;

} // namespace northlight::ui::strip::patinaface
