#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Meters.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/components/Meter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aural::ui
{

// A track's header at the left of its lane (04-arrange.md, "Track header"): the
// colour stripe, the number and name, M/S/R and monitor toggles on the first
// row; the input selector, a horizontal meter and a compact volume slider on
// the second (hidden below 44 px rows); pan, output and a sends summary on a
// third row at the large row height. The stripe opens the colour popup, the
// name renames inline on double-click, the number drags to reorder, and a
// right-click opens the track menu.
class TrackHeader final : public juce::Component, private juce::Timer
{
public:
    explicit TrackHeader(const Theme& theme);
    ~TrackHeader() override;

    // `index`: the track's position (its number and default colour); `inputNames`:
    // the audio inputs (callback order); `midiInputNames`: the open MIDI inputs.
    void setTrack(const core::Track& track,
                  int index,
                  const core::Project& project,
                  const std::vector<std::string>& inputNames,
                  const std::vector<std::string>& midiInputNames);
    // Live indicators: input being heard and a recording in progress.
    void setLiveState(bool monitoring, bool recording);
    void pushMeter(const rt::MeterBlock::Reading& reading, double elapsedMs);
    void setSelected(bool selected);
    void setHovered(bool hovered);
    core::Id trackId() const noexcept { return trackId_; }
    juce::Colour colour() const noexcept { return colour_; }
    bool mediaMissing() const noexcept { return missing_; }
    void setMediaMissing(bool missing);
    void startRename();

    std::function<void(core::Id)> onSelected; // a click anywhere on the header
    std::function<void(core::Id, int)> onInputChanged;
    std::function<void(core::Id, const std::string&)> onMidiInputChanged; // empty = all
    std::function<void(core::Id, bool)> onArmChanged;
    std::function<void(core::Id, core::MonitorMode)> onMonitorChanged;
    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void(const std::string& name)> onGestureBegin;
    std::function<void()> onGestureEnd;
    // Dragging the number: the screen position while it moves and when released.
    std::function<void(core::Id, juce::Point<int> screen, bool released)> onReorderDrag;
    std::function<void(core::Id, juce::Point<int> screen)> onContextMenu;
    // A MIDI track's "plays" chip was clicked: choose its instrument channel.
    std::function<void(core::Id, juce::Rectangle<int> screenArea)> onTargetMenu;
    // The A toggle: show the track's automation over its lane.
    std::function<void(core::Id, bool shown)> onAutomationToggled;
    void setAutomationShown(bool shown);

    void paint(juce::Graphics& g) override;
    void resized() override;
    // The bottom edge belongs to the arrange view, which resizes the row there.
    bool hitTest(int x, int y) override { return y < getHeight() - resizeEdge && x >= 0; }
    static constexpr int resizeEdge = 3;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    void refreshMonitorButton();
    void showColourMenu();
    void timerCallback() override;
    juce::Rectangle<int> numberBounds() const noexcept;
    juce::Rectangle<int> stripeBounds() const noexcept;

    const Theme& theme_;
    core::Id trackId_ = core::invalidId;
    core::MonitorMode monitor_ = core::MonitorMode::automatic;
    juce::Colour colour_;
    bool updating_ = false;
    bool monitoring_ = false;
    bool recording_ = false;
    bool armed_ = false;
    bool midi_ = false;
    bool aux_ = false;
    bool instrument_ = false; // an instrument channel: no input or arm (its MIDI tracks have them)
    bool muted_ = false;
    bool selected_ = false;
    bool hovered_ = false;
    bool missing_ = false;
    bool reordering_ = false;
    int index_ = 0;
    int sends_ = 0;
    float pulse_ = 0.0f; // the stripe pulse when arming, 1 -> 0 over toggleMs
    double pulseStartMs_ = 0.0;
    std::vector<std::string> midiNames_;
    std::vector<core::Id> busIds_;
    juce::Label name_;
    juce::ComboBox input_, output_;
    juce::TextButton mute_{"M"}, solo_{"S"}, arm_{"R"}, monitorButton_, automation_{"A"};
    juce::Slider volume_;
    Meter meter_;
    juce::TextButton target_; // a MIDI track's: "→ Drums: Pulsar", what it plays
    Knob pan_;
};

} // namespace aural::ui
