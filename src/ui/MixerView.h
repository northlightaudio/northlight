#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Meters.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace aural::ui
{

// The mixer: one strip per track and bus (fader, pan, mute, solo, phase, output,
// sends, meter) and the master strip. Every change is issued as a core::Command
// through onCommand; the view refreshes from the project it is given and drains
// the meter block on a timer.
class MixerView final : public juce::Component, private juce::Timer
{
public:
    explicit MixerView(rt::MeterBlock& meters);
    ~MixerView() override;

    void setProject(const core::Project* project);

    std::function<void(std::unique_ptr<core::Command>)> onCommand;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static constexpr int stripWidth = 104;

    // Opens (or fronts) the parameter editor window for an insert.
    void openInsertEditor(core::Id channelId, core::Id insertId);

private:
    class Strip;
    class MasterStrip;
    class EditorWindow;

    void timerCallback() override;
    void rebuildStrips();
    void layoutStrips();
    void addBus();

    const Theme& theme_ = Theme::dark();
    rt::MeterBlock& meters_;
    const core::Project* project_ = nullptr;
    std::vector<std::unique_ptr<Strip>> strips_;
    std::unique_ptr<MasterStrip> master_;
    juce::Viewport viewport_;
    juce::Component stripsHolder_;
    juce::TextButton addBusButton_{"+ Bus"};
    std::vector<std::unique_ptr<EditorWindow>> editors_;
};

} // namespace aural::ui
