#pragma once

#include "core/Model.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace aural::ui
{

// Phase 2 placeholder for the arrange view: tracks as rows, clips as blocks on a
// fixed time scale, and a playhead. Clicking the ruler or empty space locates.
// Waveforms, zoom and scroll arrive with the real arrange view.
class TrackListView final : public juce::Component
{
public:
    TrackListView();

    void setProject(const core::Project* project);
    void setPlayhead(core::SamplePosition position, bool playing);

    std::function<void(core::SamplePosition)> onLocate;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    static constexpr int rulerHeight = 24;
    static constexpr int headerWidth = 160;
    static constexpr int rowHeight = 56;

    double pixelsPerSample() const noexcept;
    int xForSample(core::SamplePosition s) const noexcept;
    core::SamplePosition sampleForX(int x) const noexcept;

    const Theme& theme_ = Theme::dark();
    const core::Project* project_ = nullptr;
    core::SamplePosition playhead_ = 0;
    bool playing_ = false;
    double pixelsPerSecond_ = 40.0;
};

} // namespace aural::ui
