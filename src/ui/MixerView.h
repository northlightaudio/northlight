#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"
#include "engine/Loudness.h"
#include "engine/Meters.h"
#include "hosting/HostFactory.h"
#include "ui/InputChoices.h"
#include "ui/InsertMenu.h"
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
    // What the slots' menus do beyond commands (reload, retry, blocklist, the
    // browser); may be null (the mixer then offers only what it can do itself).
    void setPluginActions(const PluginActions* actions) noexcept { actions_ = actions; }
    // An insert was added from the mixer (a menu or a drop): the shell remembers
    // the type as recent.
    std::function<void(const std::string& type)> onInserted;
    // The project's total plugin delay compensation in samples (the master's
    // readout); may be null.
    std::function<int()> outputLatency;
    // The device's stereo output pairs, by name ("Speakers 3/4"), pair 0 first
    // (the master's): the output selectors offer the others as hardware outputs.
    void setOutputChoices(std::vector<std::string> pairs);
    // Wide strips (screens/mixer.md): 112 px with the dB scale beside the meter
    // and a pan column on the send rows. Remembered in the settings.
    void setWideStrips(bool wide);
    bool wideStrips() const noexcept { return wide_; }
    // Strips the user hid (Channel::mixerHidden), shown again from the mixer's
    // "Hidden" button or its background menu.
    int hiddenStripCount() const noexcept { return hiddenCount_; }
    // The windows a project opened with (Insert::windowOpen) open once their
    // plugins are loaded; setProject() with a new project arms it.
    void reopenSavedWindows();
    // The master clipped since the last clear (the header's indicator mirrors it).
    bool masterClipped() const noexcept { return masterClipped_; }
    void clearMasterClip() { masterClipped_ = false; }
    // The strips' and the master's meters take the readings' latest frame.
    void pushMeters();

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Opens (or fronts) the parameter editor window for an insert.
    void openInsertEditor(core::Id channelId, core::Id insertId);
    // Closes every editor window (before a project is replaced, and the "Close
    // all plugin windows" command).
    void closeEditors();
    int openEditorCount() const noexcept { return static_cast<int>(pluginWindows_.size() + editors_.size()); }
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
    void showInsertMenu(core::Id channelId,
                        core::Id insertId,
                        bool offerOutputs,
                        juce::Component* target = nullptr);
    InsertMenuRequest insertMenuRequest(core::Id channelId, core::Id insertId, bool offerOutputs);
    // The shell's plugin actions, or the mixer's own (open, live) without one.
    const PluginActions* actions();
    // A drop on a strip: a browser row ("plugin:<type>") or an insert slot
    // ("insert:<channel>:<insert>", moved, or copied with Ctrl) at `index`.
    void dropOnStrip(core::Id channelId, const juce::String& description, int index, bool copy);
    // The menu of the mixer's background: wide strips, the hidden strips.
    void showViewMenu(juce::Component* target);
    // An insert added from the mixer (a menu or a drop).
    void addInsert(core::Id channelId, const std::string& type, int index = -1);

    void rebuildStrips();
    void layoutStrips();
    void addBus();
    // The "New FX track" items of a send menu: a new FX track with a send to it
    // from `channelId`, one step.
    void
    addFxSendItems(juce::PopupMenu& menu, std::vector<std::function<void()>>& actions, core::Id channelId);
    void openSavedWindows();

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
        bool input = false, stripRow = false, instrument = false, latency = false;
        int inserts = 0, sends = 0;
        bool operator==(const RowPlan&) const = default;
    };
    RowPlan rows_;
    std::vector<std::unique_ptr<Strip>> strips_;
    std::unique_ptr<MasterStrip> master_;
    juce::Viewport viewport_;
    // The strips' holder; a right-click on its empty part shows the view menu.
    struct Holder final : juce::Component
    {
        std::function<void()> onMenu;
        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onMenu)
                onMenu();
        }
    };
    Holder stripsHolder_;
    juce::TextButton addBus_{"+ Bus"};
    juce::TextButton hidden_; // "2 hidden": shows them again
    int hiddenCount_ = 0;
    bool wide_ = false, wideRead_ = false;
    std::vector<std::string> outputPairs_; // the device's output pairs, pair 0 first
    std::vector<std::unique_ptr<EditorWindow>> editors_;
    std::vector<std::unique_ptr<PluginWindow>> pluginWindows_;
    // Hosted plugins shown in the generic editor instead of their own (the
    // window's toggle), for the session.
    std::vector<core::Id> genericEditors_;
    // Windows to open once their plugins load (a project just opened).
    std::vector<std::pair<core::Id, core::Id>> pendingWindows_; // channel, insert
    const hosting::HostFactory* host_ = nullptr;
    const PluginSources* pluginSources_ = nullptr;
    const PluginActions* actions_ = nullptr;
    PluginActions localActions_; // without the shell's: open and live from the mixer
};

} // namespace northlight::ui
