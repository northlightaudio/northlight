#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace northlight::ui
{

// A level meter (02-design-system.md, "Meter"): one 6 px bar per channel with 2 px
// gaps, the zones from the tokens painted as solid segments, a 1 px peak-hold
// line, and a clip square above the bars that stays lit until clicked (or, when
// autoClear is on, for clipHoldMs). The bar rises over meterRiseMs and falls at
// meterFallDbPerSec (ui/MeterBallistics.h); the peak line and clip see raw peaks.
// The owner pushes peaks (linear) per UI frame; the meter keeps the ballistics.
// Vertical by default; the horizontal variant draws the same bars rotated,
// without the scale.
class Meter final : public juce::Component
{
public:
    Meter(const Theme& theme, int channels, bool vertical = true);

    // New peak values (linear, per channel) since the last call; `elapsedMs` is
    // the time since the previous push for the fall ballistics.
    void push(const float* peaks, int numChannels, double elapsedMs);
    void setShowScale(bool show) { showScale_ = show; }
    void setAutoClearClip(bool autoClear) { autoClearClip_ = autoClear; }
    void clearClip();
    bool clipped() const noexcept;
    // The width the bars take (without the scale), so strips can lay out.
    int barsWidth() const noexcept;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    // Accessibility: a read-only level in dB.
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    double accessibleValue() const noexcept;

private:
    struct Channel
    {
        float levelDb = -100.0f;
        float peakDb = -100.0f;
        double peakAgeMs = 0.0;
        bool clipped = false;
        double clipAgeMs = 0.0;
    };
    float yForDb(float db, juce::Rectangle<float> bar) const noexcept;

    const Theme& theme_;
    std::vector<Channel> channels_;
    bool vertical_ = true;
    bool showScale_ = false;
    bool autoClearClip_ = false;
    bool wasSilent_ = false;
};

} // namespace northlight::ui
