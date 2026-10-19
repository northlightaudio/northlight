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

    void setTrack(const core::Track& track, const std::vector<std::string>& inputNames);
    core::Id trackId() const noexcept { return trackId_; }

    std::function<void(core::Id, int)> onInputChanged;
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
    juce::Label name_;
    juce::ComboBox input_;
    juce::TextButton arm_{"R"}, monitorButton_{"I"};
};

} // namespace aural::ui
