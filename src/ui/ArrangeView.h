#pragma once

#include "audio/PeakStore.h"
#include "core/Model.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace aural::ui
{

// The arrange view, Phase 2 scope: tracks as rows, clips with waveforms drawn from
// the peak cache, a ruler, a playhead that follows playback, horizontal zoom
// (Ctrl+wheel, around the pointer) and scroll (wheel / Shift+wheel / scrollbar).
// Click the ruler or empty track space to locate. Editing arrives in Phase 6.
class ArrangeView final : public juce::Component,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener
{
public:
    explicit ArrangeView(audio::PeakStore& peaks);
    ~ArrangeView() override;

    void setProject(const core::Project* project);
    void setPlayhead(core::SamplePosition position, bool playing);

    std::function<void(core::SamplePosition)> onLocate;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    static constexpr int rulerHeight = 24;
    static constexpr int headerWidth = 160;
    static constexpr int rowHeight = 72;
    static constexpr int scrollbarHeight = 12;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;

    double sampleRate() const noexcept;
    int xForSample(core::SamplePosition s) const noexcept;
    core::SamplePosition sampleForX(int x) const noexcept;
    core::SamplePosition visibleSamples() const noexcept;
    core::SamplePosition contentLength() const noexcept;
    void setZoom(double samplesPerPixel, int anchorX);
    void setScroll(core::SamplePosition firstVisible);
    void updateScrollbar();
    void drawRuler(juce::Graphics& g, juce::Rectangle<int> area);
    void drawClip(juce::Graphics& g,
                  const core::Track& track,
                  const core::AudioClip& clip,
                  juce::Rectangle<int> lane);

    const Theme& theme_ = Theme::dark();
    audio::PeakStore& peaks_;
    const core::Project* project_ = nullptr;
    core::SamplePosition playhead_ = 0;
    bool playing_ = false;
    double samplesPerPixel_ = 48000.0 / 40.0; // 40 px per second at 48 kHz
    core::SamplePosition scroll_ = 0;         // first visible sample
    juce::ScrollBar scrollbar_{false};
    bool updatingScrollbar_ = false;
};

} // namespace aural::ui
