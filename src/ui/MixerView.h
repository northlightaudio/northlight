#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "engine/Loudness.h"
#include "engine/Meters.h"
#include "hosting/HostFactory.h"
#include "ui/PluginWindow.h"
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
    MixerView(rt::MeterBlock& meters, std::function<engine::LoudnessMeter::Reading()> loudness);
    ~MixerView() override;

    void setProject(const core::Project* project);

    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    // The live processor for an insert id (a hosted plugin's editor needs it).
    std::function<std::shared_ptr<engine::InsertProcessor>(core::Id insertId)> liveInsert;
    // "Scan for plugins..." in the inserts menu.
    std::function<void()> onScanPlugins;
    // Names and the plugin list for the inserts menu; may be null (built-ins only).
    void setHost(const hosting::HostFactory* host) noexcept { host_ = host; }

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
    std::string insertDisplayName(const std::string& type) const;

    const Theme& theme_ = Theme::dark();
    rt::MeterBlock& meters_;
    std::function<engine::LoudnessMeter::Reading()> loudness_;
    const core::Project* project_ = nullptr;
    std::vector<std::unique_ptr<Strip>> strips_;
    std::unique_ptr<MasterStrip> master_;
    juce::Viewport viewport_;
    juce::Component stripsHolder_;
    juce::TextButton addBusButton_{"+ Bus"};
    std::vector<std::unique_ptr<EditorWindow>> editors_;
    std::vector<std::unique_ptr<PluginWindow>> pluginWindows_;
    const hosting::HostFactory* host_ = nullptr;
};

} // namespace aural::ui
