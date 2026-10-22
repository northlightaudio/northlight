#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace northlight::ui
{

// A determinate progress bar with its label above (design-system.md, "Progress"):
// the label in body textSecondary, one line, cut with an ellipsis; under it the
// 4 px bar, a surface2 track with the accent fill. The loading sheet's content.
class ProgressRow final : public juce::Component
{
public:
    explicit ProgressRow(const Theme& theme) : theme_(theme) { setInterceptsMouseClicks(false, false); }

    // `fraction` is clamped to 0..1.
    void setProgress(double fraction, const juce::String& label);
    double fraction() const noexcept { return fraction_; }
    const juce::String& label() const noexcept { return label_; }

    // The height it needs: the label's line, a small gap and the bar.
    static int preferredHeight(const Theme& theme);

    void paint(juce::Graphics& g) override;

private:
    const Theme& theme_;
    double fraction_ = 0.0;
    juce::String label_;
};

} // namespace northlight::ui
