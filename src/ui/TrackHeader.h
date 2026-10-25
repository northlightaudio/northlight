#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Meters.h"
#include "ui/InputChoices.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/components/Meter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui
{

// A track's header at the left of its lane (screens/arrange.md, "Track header"): the
// colour stripe, the track-kind icon and name, M/S/R and monitor toggles on the first
// row; the input selector, a horizontal meter and a compact volume slider on
// the second (hidden below 44 px rows); pan, output and a sends summary on a
// third row at the large row height. The stripe opens the colour popup, the
// name renames inline on double-click, the icon drags to reorder, and a
// right-click opens the track menu.
class TrackHeader final : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    explicit TrackHeader(const Theme& theme);
    ~TrackHeader() override;

    // `index`: the track's position (its number and default colour); `inputs`:
    // the audio input menu's choices; `midiInputNames`: the open MIDI inputs.
    // A values refresh: volume, pan and the toggles only (no combo refill).
    void setValues(const core::Channel& channel);
    void setTrack(const core::Track& track,
                  int index,
                  const core::Project& project,
                  const std::vector<InputChoice>& inputs,
                  const std::vector<std::string>& midiInputNames);
    // Live indicators: input being heard and a recording in progress.
    void setLiveState(bool monitoring, bool recording);
    void pushMeter(const rt::MeterBlock::Reading& reading, double elapsedMs);
    void setSelected(bool selected);
    void setHovered(bool hovered);
    core::Id trackId() const noexcept { return trackId_; }
    juce::Colour colour() const noexcept { return colour_; }
    bool mediaMissing() const noexcept { return missing_; }
    // Over the icon: the track's number and kind (the rest of the header has none).
    juce::String getTooltip() override;
    void setMediaMissing(bool missing);
    void startRename();

    std::function<void(core::Id)> onSelected;                              // a click anywhere on the header
    std::function<void(core::Id, int, const std::string&)> onInputChanged; // channel, or a bus name
    std::function<void(core::Id, const std::string&)> onMidiInputChanged;  // empty = all
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
    // The A toggle: open or close the track's automation lanes under its row;
    // a right-click asks for its menu (the mode, the lanes).
    std::function<void(core::Id, bool open)> onAutomationToggled;
    std::function<void(core::Id, juce::Rectangle<int> screenArea)> onAutomationMenu;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override; // the A toggle's lane count
    void resized() override;
    // The bottom edge belongs to the arrange view, which resizes the row there.
    bool hitTest(int x, int y) override { return y < getHeight() - resizeEdge && x >= 0; }
    static constexpr int resizeEdge = 3;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    // The A toggle: a right-click asks for the menu instead of toggling.
    struct AutomationButton final : juce::TextButton
    {
        AutomationButton() : juce::TextButton("A") {}
        std::function<void()> onMenu;
        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                if (onMenu)
                    onMenu();
                return;
            }
            juce::TextButton::mouseDown(e);
        }
        void mouseUp(const juce::MouseEvent& e) override
        {
            if (!e.mods.isPopupMenu())
                juce::TextButton::mouseUp(e);
        }
    };
    void setAutomation(const core::Channel& channel);
    void refreshMonitorButton();
    void showColourMenu();
    void timerCallback() override;
    juce::Rectangle<int> numberBounds() const noexcept;
    juce::Rectangle<int> stripeBounds() const noexcept;

    const Theme& theme_;
    core::Id trackId_ = core::invalidId;
    // Selects the track on a press anywhere in the header, its controls included
    // (the name, the buttons, the fader, the meter), not just its background.
    struct SelectOnPress final : juce::MouseListener
    {
        explicit SelectOnPress(TrackHeader& header) : header_(header) {}
        void mouseDown(const juce::MouseEvent&) override
        {
            if (header_.onSelected)
                header_.onSelected(header_.trackId_);
        }
        TrackHeader& header_;
    } selectOnPress_{*this};
    core::MonitorMode monitor_ = core::MonitorMode::automatic;
    juce::Colour colour_;
    bool updating_ = false;
    bool monitoring_ = false;
    bool recording_ = false;
    bool armed_ = false;
    bool midi_ = false;
    bool aux_ = false;
    bool instrument_ = false; // an instrument channel: no input or arm (its MIDI tracks have them)
    bool folder_ = false;     // a folder track: M, S and its disclosure only
    bool folderOpen_ = true;
    int depth_ = 0; // folders it sits in: the header is indented by that much
    int indent() const noexcept { return depth_ * 12; }
    bool muted_ = false;
    bool selected_ = false;
    bool hovered_ = false;
    bool missing_ = false;
    bool reordering_ = false;
    int index_ = 0;
    core::TrackKind kind_ = core::TrackKind::audio;
    int sends_ = 0;
    float pulse_ = 0.0f; // the stripe pulse when arming, 1 -> 0 over toggleMs
    double pulseStartMs_ = 0.0;
    std::vector<std::string> midiNames_;
    std::vector<core::Id> busIds_;
    juce::Label name_;
    juce::ComboBox input_, output_;
    std::vector<InputChoice> inputChoices_;
    juce::TextButton mute_{"M"}, solo_{"S"}, arm_{"R"}, monitorButton_;
    AutomationButton automation_;
    int automationLanes_ = 0; // the A toggle's badge
    juce::Slider volume_;
    Meter meter_;
    juce::TextButton target_; // a MIDI track's: "→ Drums: Pulsar", what it plays
    juce::TextButton fold_;   // a folder's disclosure: open or close it
    Knob pan_;
};

} // namespace northlight::ui
