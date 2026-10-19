#pragma once

#include "audio/PeakStore.h"
#include "core/Model.h"
#include "ui/Theme.h"
#include "ui/TrackHeader.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aural::ui
{

// The arrange view, Phase 2 scope: tracks as rows, clips with waveforms drawn from
// the peak cache, a bars/beats and time ruler, a playhead that follows playback,
// horizontal zoom (Ctrl+wheel, around the pointer), vertical scroll (wheel) and
// horizontal scroll (Shift+wheel / scrollbar),
// and audio files dropped onto it become tracks at the drop position. Click the
// ruler or empty track space to locate. Editing arrives in Phase 6.
class ArrangeView final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener
{
public:
    explicit ArrangeView(audio::PeakStore& peaks);
    ~ArrangeView() override;

    void setProject(const core::Project* project);
    void setPlayhead(core::SamplePosition position, bool playing, bool recording);
    // The loop/punch region, drawn in the ruler and over the lanes; set by dragging
    // in the ruler.
    void setRegion(core::SamplePosition start, core::SamplePosition end, bool loop, bool punch);
    // Names of the device's active inputs, in callback order, for the track headers.
    void setInputNames(std::vector<std::string> names);
    // Live state per track (same order as the project's tracks): whether the input is
    // being heard, and whether a recording is in progress.
    void setMonitoringStates(const std::vector<bool>& monitoring, bool recording);

    std::function<void(core::SamplePosition)> onLocate;
    std::function<void(core::SamplePosition, core::SamplePosition)> onRegionChanged;
    std::function<void(core::Id, int)> onTrackInputChanged;
    std::function<void(core::Id, bool)> onTrackArmChanged;
    std::function<void(core::Id, core::MonitorMode)> onTrackMonitorChanged;
    // Files dropped on the view, with the timeline position under the pointer.
    std::function<void(const std::vector<juce::File>&, core::SamplePosition)> onImportFiles;
    // Extensions the drop target accepts, e.g. "*.wav;*.flac".
    void setAcceptedExtensions(const juce::String& wildcards);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    static constexpr int rulerHeight = 40; // bars/beats row over a time row
    static constexpr int headerWidth = 220;
    static constexpr int rowHeight = 72;
    static constexpr int scrollbarHeight = 12;
    static constexpr int scrollbarWidth = 12;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;

    double sampleRate() const noexcept;
    int xForSample(core::SamplePosition s) const noexcept;
    core::SamplePosition sampleForX(int x) const noexcept;
    core::SamplePosition visibleSamples() const noexcept;
    core::SamplePosition contentLength() const noexcept;
    void setZoom(double samplesPerPixel, int anchorX);
    void setScroll(core::SamplePosition firstVisible);
    void setVerticalScroll(int firstRowPixel);
    void updateScrollbar();
    void rebuildHeaders();
    void layoutHeaders();
    int contentHeight() const noexcept;
    int lanesHeight() const noexcept { return getHeight() - rulerHeight - scrollbarHeight; }
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
    bool recording_ = false;
    core::SamplePosition regionStart_ = 0, regionEnd_ = 0;
    bool loop_ = false, punch_ = false;
    bool draggingRegion_ = false;
    core::SamplePosition dragAnchor_ = 0;
    std::vector<std::unique_ptr<TrackHeader>> headers_;
    std::vector<std::string> inputNames_;
    double samplesPerPixel_ = 48000.0 / 40.0; // 40 px per second at 48 kHz
    core::SamplePosition scroll_ = 0;         // first visible sample
    juce::ScrollBar scrollbar_{false};
    juce::ScrollBar verticalScrollbar_{true};
    int verticalScroll_ = 0; // pixels
    bool updatingScrollbar_ = false;
    juce::StringArray acceptedExtensions_;
    bool dragOver_ = false;
};

} // namespace aural::ui
