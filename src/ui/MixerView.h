#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"
#include "engine/Loudness.h"
#include "engine/Meters.h"
#include "hosting/HostFactory.h"
#include "ui/InputChoices.h"
#include "ui/MeterReadings.h"
#include "ui/PluginMenu.h"
#include "ui/PluginWindow.h"
#include "ui/Theme.h"
#include "ui/components/Fader.h"
#include "ui/components/Knob.h"
#include "ui/components/Meter.h"
#include "ui/components/NumericField.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui
{

// The mixer (screens/mixer.md): one strip per track and bus (name cap, input, insert
// slots, send rows, pan, M/S/R/phase, fader with a meter, value field, output)
// and the master strip (fader, scaled meter, loudness readout) pinned at the
// right. Every change is issued as a core::Command through onCommand; the view
// refreshes from the project it is given and drains the meter block on a timer.
// The selected strip follows the arrange selection and reports clicks back.
class MixerView final : public juce::Component
{
public:
    // The strips' meters come from `meters`, which the window ticks; it calls
    // pushMeters() after each tick.
    MixerView(const MeterReadings& meters,
              std::function<engine::LoudnessMeter::Reading()> loudness,
              std::function<int()> masterMeterSlot);
    ~MixerView() override;

    // valuesOnly: only live values changed (a fader drag): the strips update
    // their values without rebuilding.
    void setProject(const core::Project* project, bool valuesOnly = false);
    // Only parameters changed: the open editor windows follow, the strips stay.
    void refreshEditors();
    // The audio inputs (callback order) and the open MIDI inputs for the input combos.
    void setInputChoices(std::vector<InputChoice> audio, std::vector<std::string> midi);
    // The selected channel (its strip gets the accent outline); clicks on a name
    // cap report through onSelectChannel.
    void setSelectedChannel(core::Id channelId);
    std::function<void(core::Id)> onSelectChannel;

    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    // A slider drag starts and ends a gesture: its commands make one undo step.
    std::function<void(const std::string& name)> onGestureBegin;
    std::function<void()> onGestureEnd;
    // The live processor for an insert id (a hosted plugin's editor needs it).
    std::function<std::shared_ptr<engine::InsertProcessor>(core::Id insertId)> liveInsert;
    // "Scan for plugins..." in the inserts menu; "Plugins..." in the header.
    std::function<void()> onScanPlugins;
    std::function<void()> onOpenPluginBrowser;
    // "Edit channel strip" on a strip's module row: show the channel in the Editor panel.
    std::function<void(core::Id channelId)> onShowStrip;
    // What automation is playing on a channel: faders and pans follow it.
    struct Automated
    {
        bool gain = false, pan = false;
        float gainValue = 1.0f, panValue = 0.0f;
    };
    std::function<Automated(core::Id channelId)> automatedValues;
    // Per-user values (plugin window positions, the pin preference); may be null.
    std::function<std::string(const std::string& key)> loadSetting;
    std::function<void(const std::string& key, const std::string& value)> saveSetting;
    // Names and the plugin list for the inserts menu; may be null (built-ins only).
    void setHost(const hosting::HostFactory* host) noexcept { host_ = host; }
    void setPluginSources(const PluginSources* sources) noexcept { pluginSources_ = sources; }
    // The master clipped since the last clear (the header's indicator mirrors it).
    bool masterClipped() const noexcept { return masterClipped_; }
    void clearMasterClip() { masterClipped_ = false; }
    // The strips' and the master's meters take the readings' latest frame.
    void pushMeters();

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Opens (or fronts) the parameter editor window for an insert.
    void openInsertEditor(core::Id channelId, core::Id insertId);
    // Closes every editor window (before a project is replaced).
    void closeEditors();
    // Whether an insert's editor (plugin or generic window) is open.
    bool isEditorOpen(core::Id insertId) const;
    // An insert's editor window opened or closed (the Editor panel shows a popped
    // out insert as a note instead of its controls).
    std::function<void()> onEditorWindowsChanged;
    // The display name of an insert type (built-in or hosted), for the editor panel.
    std::string insertDisplayName(const std::string& type) const;

private:
    // Calls onEditorWindowsChanged when the set of open editor windows has changed.
    void noteEditorWindows();
    std::vector<core::Id> shownEditors_;
    class Strip;
    class MasterStrip;
    class EditorWindow;

    // The insert menus every strip shares, the master's included: adding an effect
    // (the menu opens on `target`), and an insert's own menu (open, bypass, order,
    // wet/dry, its key and, where `offerOutputs`, an instrument's extra outputs).
    void showAddInsertMenu(core::Id channelId, juce::Component& target);
    void showInsertMenu(core::Id channelId, core::Id insertId, bool offerOutputs);

    void rebuildStrips();
    void layoutStrips();
    void addBus();

    const Theme& theme_ = Theme::dark();
    const MeterReadings& meters_;
    std::function<engine::LoudnessMeter::Reading()> loudness_;
    std::function<int()> masterMeterSlot_;
    const core::Project* project_ = nullptr;
    bool masterClipped_ = false;
    std::vector<InputChoice> inputChoices_;
    std::vector<std::string> midiInputNames_;
    core::Id selected_ = core::invalidId;
    // The rows every strip reserves, sized to the busiest strip so that each
    // section (fader, pan, buttons) sits at the same height in all of them.
    struct RowPlan
    {
        bool input = false, stripRow = false, instrument = false;
        int inserts = 0, sends = 0;
        bool operator==(const RowPlan&) const = default;
    };
    RowPlan rows_;
    std::vector<std::unique_ptr<Strip>> strips_;
    std::unique_ptr<MasterStrip> master_;
    juce::Viewport viewport_;
    juce::Component stripsHolder_;
    juce::TextButton addBus_{"+ Bus"};
    std::vector<std::unique_ptr<EditorWindow>> editors_;
    std::vector<std::unique_ptr<PluginWindow>> pluginWindows_;
    const hosting::HostFactory* host_ = nullptr;
    const PluginSources* pluginSources_ = nullptr;
};

} // namespace northlight::ui
