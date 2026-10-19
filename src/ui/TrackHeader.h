#pragma once

#include "core/Model.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace aural::ui
{

// The controls at the left of a track lane: name, input selector, arm and monitor.
// Phase 3 scope; gain/pan/mute/solo join with the mixer in Phase 4.
class TrackHeader final : public juce::Component
{
public:
    TrackHeader();

    // `inputNames`: the audio inputs (callback order); `midiInputNames`: the open
    // MIDI inputs. A MIDI track's selector lists the latter plus "All inputs".
    void setTrack(const core::Track& track,
                  const std::vector<std::string>& inputNames,
                  const std::vector<std::string>& midiInputNames = {});
    // Live indicators: input being heard (green dot by the monitor button) and a
    // recording in progress (the arm button reads REC while armed).
    void setLiveState(bool monitoring, bool recording);
    core::Id trackId() const noexcept { return trackId_; }

    std::function<void(core::Id, int)> onInputChanged;
    std::function<void(core::Id, const std::string&)> onMidiInputChanged; // empty = all
    std::function<void(core::Id, bool)> onArmChanged;
    std::function<void(core::Id, core::MonitorMode)> onMonitorChanged;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void refreshMonitorButton();

    const Theme& theme_ = Theme::dark();
    core::Id trackId_ = core::invalidId;
    core::MonitorMode monitor_ = core::MonitorMode::automatic;
    bool updating_ = false;
    bool monitoring_ = false;
    bool recording_ = false;
    bool armed_ = false;
    bool midi_ = false;
    bool aux_ = false;
    std::vector<std::string> midiNames_;
    juce::Label name_;
    juce::ComboBox input_;
    juce::TextButton arm_{"R"}, monitorButton_{"I"};
};

} // namespace aural::ui
