#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"
#include "engine/Insert.h"
#include "hosting/HostFactory.h"
#include "ui/PluginMenu.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// What every insert slot shares, wherever it shows (a mixer strip's slot, an
// Editor panel card, the collapsed strip's list): the slot's state (a plugin
// missing, failed or crashed), its menu, built in one place so the mixer and
// the Editor panel offer the same items (screens/mixer.md, screens/plugins.md
// 7.1), the copied inserts, and a hosted plugin's presets.

namespace northlight::ui
{

// What the shell does for a slot that the views cannot do themselves. One
// object, owned by the shell and handed to every view that shows inserts.
struct PluginActions
{
    const hosting::HostFactory* host = nullptr; // names, the registry and load failures
    std::function<std::shared_ptr<engine::InsertProcessor>(core::Id insertId)> live;
    std::function<void(core::Id channelId, core::Id insertId)> openEditor;
    std::function<void(core::Id insertId)> reload;          // a crashed plugin, from its last saved state
    std::function<void(const std::string& type)> retry;     // a plugin that failed to load
    std::function<void(const std::string& type)> blocklist; // never load its file again
    std::function<void()> scan;                             // look for plugins (a missing one)
    // The plugin browser, its search set to the plugin (a format badge's click).
    std::function<void(const std::string& type)> showInBrowser;
    // Several commands as one undo step.
    std::function<void(const std::string& name, std::vector<std::unique_ptr<core::Command>>)> commands;
};

// What is wrong with a slot, if anything (screens/plugins.md 7.5).
struct InsertTrouble
{
    enum class Kind
    {
        none,
        missing, // not installed on this machine (or a built-in this version lacks)
        failed,  // installed, but it would not load
        crashed, // it crashed while processing and is switched off
    };
    Kind kind = Kind::none;
    std::string reason; // for the tooltip: the load error, or what to do
    bool any() const noexcept { return kind != Kind::none; }
    // "Not installed", "Failed", "Crashed": the slot's label.
    const char* label() const noexcept;
};
InsertTrouble insertTrouble(const core::Insert& insert, const PluginActions* actions);

// An insert's name: the registry's or the built-in's, the name a missing plugin
// had when the project was saved, else "Missing plugin".
std::string insertName(const core::Insert& insert, const PluginActions* actions);

// The inserts copied (Copy, Copy chain), with a hosted plugin's live state; a
// paste adds copies of them with new ids.
const std::vector<core::Insert>& copiedInserts();
void copyInserts(std::vector<core::Insert> inserts);
// The copied inserts that fit a chain: the MIDI effects for a MIDI FX chain
// (`midiEffects`), the rest for an insert chain.
std::vector<core::Insert> pastableInserts(bool midiEffects);
// A copy of an insert as it is now: a hosted plugin's state from its instance.
core::Insert snapshotInsert(const core::Insert& insert, const PluginActions* actions);

// The menu for one slot: the instrument slot (`instrument`) or an insert.
struct InsertMenuRequest
{
    const core::Project* project = nullptr;
    core::Id channelId = core::invalidId;
    core::Id insertId = core::invalidId;
    bool instrument = false;   // the instrument slot: replace and remove it as an instrument
    bool midiEffect = false;   // a MIDI track's MIDI FX slot: no wet/dry or key, MIDI effects to replace it
    bool offerOutputs = false; // an instrument's extra outputs as output tracks
    const PluginActions* actions = nullptr;
    const PluginSources* sources = nullptr;
    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    // The instrument slot's replacement (set by the view: a MIDI track may need
    // a channel made for it).
    std::function<void(const std::string& type)> onReplaceInstrument;
};
// Builds and shows it on `target` (or at the mouse).
void showInsertMenu(const InsertMenuRequest& request, juce::Component* target);
// The items alone, into `menu` (item ids from 1; `actions[id - 1]` runs one), for
// a menu that lists several inserts (the collapsed strip's).
void addInsertMenuItems(juce::PopupMenu& menu,
                        std::vector<std::function<void()>>& actions,
                        const InsertMenuRequest& request);

// A hosted plugin's presets (its programs and preset files, Load and Save),
// each load one undo step that names it ("Preset Warm Pad"): the plugin's state
// before and after goes into a SetPluginState. Item ids follow `actions`.
void addHostedPresetItems(juce::PopupMenu& menu,
                          std::vector<std::function<void()>>& actions,
                          hosting::PluginPresets& presets,
                          std::shared_ptr<engine::InsertProcessor> live,
                          core::Id channelId,
                          core::Id insertId,
                          std::function<void(std::unique_ptr<core::Command>)> onCommand);

// Loads a hosted preset (a program or a file) as one undo step.
void loadHostedPreset(hosting::PluginPresets& presets,
                      const std::shared_ptr<engine::InsertProcessor>& live,
                      core::Id channelId,
                      core::Id insertId,
                      const std::string& name,
                      const std::function<bool()>& load,
                      const std::function<void(std::unique_ptr<core::Command>)>& onCommand);

// The latency of an insert as the menus and strips show it: "32 smp (0.7 ms)".
juce::String latencyText(int samples, double sampleRate);

} // namespace northlight::ui
