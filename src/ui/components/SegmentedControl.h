#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace aural::ui
{

// A segmented control: a pill track (surface1) with a sliding thumb (surface3)
// behind the selected segment; labels in `label`, selected textPrimary, others
// textSecondary. Click or Left/Right selects; the thumb slides over toggleMs.
class SegmentedControl final : public juce::Component, private juce::Timer
{
public:
    explicit SegmentedControl(const Theme& theme);

    void setSegments(std::vector<juce::String> labels);
    void setSelected(int index, juce::NotificationType notify);
    int selected() const noexcept { return selected_; }
    int preferredWidth() const;

    std::function<void(int)> onChange;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override { repaint(); }
    void focusLost(FocusChangeType) override { repaint(); }

private:
    void timerCallback() override;
    juce::Rectangle<float> segmentBounds(int index) const noexcept;

    const Theme& theme_;
    std::vector<juce::String> labels_;
    int selected_ = 0;
    float thumbX_ = 0.0f, thumbTargetX_ = 0.0f, thumbWidth_ = 0.0f;
    double animationStart_ = 0.0;
    float animationFrom_ = 0.0f;
};

} // namespace aural::ui
