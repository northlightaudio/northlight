#include "ui/MixerView.h"

#include "core/Command.h"
#include "core/Settings.h"
#include "engine/Meters.h"
#include "hosting/HostFactory.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/FxTracks.h"
#include "ui/ColourMenu.h"
#include "ui/Db.h"
#include "ui/FrameStats.h"
#include "ui/Icons.h"
#include "ui/InsertEditor.h"
#include "ui/InstrumentOutputs.h"
#include "ui/Motion.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/OnScreen.h"
#include "ui/PluginMenu.h"
#include "ui/SendMenu.h"
#include "ui/SidechainMenu.h"
#include "ui/components/Accessibility.h"
#include "ui/components/AddSlot.h"
#include "ui/mixer/MasterStrip.h"
#include "ui/mixer/MixerStrip.h"
#include "ui/mixer/StripParts.h"
#include "ui/strip/StripStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>

namespace northlight::ui
{

// ----- insert editor windows ------------------------------------------------------------------

class MixerView::EditorWindow final : public juce::DocumentWindow
{
public:
    EditorWindow(MixerView& owner, std::unique_ptr<InsertEditor> editor, const juce::String& title)
        : juce::DocumentWindow(
              title, Theme::dark().colours.background, juce::DocumentWindow::closeButton, !offscreenMode()),
          owner_(owner), editor_(editor.get())
    {
        setUsingNativeTitleBar(true);
        setContentOwned(editor.release(), true);
        setResizable(false, false);
        showWindow(*this);
    }

    InsertEditor& editor() noexcept { return *editor_; }

    void closeButtonPressed() override
    {
        opener_.restore(); // before it goes: the focus goes back where it was
        juce::Component::SafePointer<MixerView> owner(&owner_);
        const auto id = editor_->insertId();
        juce::MessageManager::callAsync(
            [owner, id]
            {
                if (owner == nullptr)
                    return;
                std::erase_if(owner->editors_, [id](const auto& w) { return w->editor().insertId() == id; });
                owner->noteEditorWindows();
            });
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            closeButtonPressed();
            return true;
        }
        return juce::DocumentWindow::keyPressed(key);
    }

private:
    FocusReturn opener_; // first: taken before the window shows
    MixerView& owner_;
    InsertEditor* editor_;
};

// ----- view ---------------------------------------------------------------------------------

void MixerView::addInsert(core::Id channelId, const std::string& type, int index)
{
    if (!onCommand || type.empty())
        return;
    core::Insert insert;
    insert.type = type;
    insert.params = plugins::defaultParameters(type);
    onCommand(std::make_unique<core::AddInsert>(channelId, insert, index));
    if (onInserted)
        onInserted(type);
}

const PluginActions* MixerView::actions()
{
    if (actions_ != nullptr)
        return actions_;
    localActions_.host = host_;
    localActions_.live = liveInsert;
    localActions_.openEditor = [this](core::Id channelId, core::Id insertId)
    { openInsertEditor(channelId, insertId); };
    localActions_.scan = onScanPlugins;
    return &localActions_;
}

void MixerView::showAddInsertMenu(core::Id channelId, juce::Component& target)
{
    // Effects only: an instrument has its own slot.
    auto built = buildPluginMenu(PluginKind::effect, pluginSources_);
    auto& menu = built.menu;
    juce::Component::SafePointer<MixerView> safe(this);
    std::vector<std::function<void()>> actions;
    for (const auto& type : built.types)
        actions.push_back(
            [safe, channelId, type]
            {
                if (safe != nullptr)
                    safe->addInsert(channelId, type);
            });
    int item = built.nextItem();
    menu.addSeparator();
    menu.addItem(item++, "Scan for plugins...");
    actions.push_back(
        [safe]
        {
            if (safe != nullptr && safe->onScanPlugins)
                safe->onScanPlugins();
        });
    menu.addItem(item++, "Plugin browser...");
    actions.push_back(
        [safe]
        {
            if (safe != nullptr && safe->onOpenPluginBrowser)
                safe->onOpenPluginBrowser();
        });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target),
                       [actions](int result)
                       {
                           if (result >= 1 && static_cast<size_t>(result) <= actions.size())
                               actions[static_cast<size_t>(result) - 1]();
                       });
}

InsertMenuRequest MixerView::insertMenuRequest(core::Id channelId, core::Id insertId, bool offerOutputs)
{
    InsertMenuRequest r;
    r.project = project_;
    r.channelId = channelId;
    r.insertId = insertId;
    r.offerOutputs = offerOutputs;
    r.actions = actions();
    r.sources = pluginSources_;
    juce::Component::SafePointer<MixerView> safe(this);
    r.onCommand = [safe](std::unique_ptr<core::Command> c)
    {
        if (safe != nullptr && safe->onCommand)
            safe->onCommand(std::move(c));
    };
    if (const auto* channel = project_ != nullptr ? project_->findChannel(channelId) : nullptr;
        channel != nullptr && channel->hasInstrument() && channel->instrument.id == insertId)
    {
        r.instrument = true;
        r.onReplaceInstrument = [safe, channelId](const std::string& type)
        {
            if (safe == nullptr || !safe->onCommand)
                return;
            core::Insert instrument;
            instrument.type = type;
            instrument.params = plugins::defaultParameters(type);
            safe->onCommand(std::make_unique<core::SetInstrument>(channelId, instrument));
            if (safe->onInserted)
                safe->onInserted(type);
        };
    }
    return r;
}

void MixerView::showInsertMenu(core::Id channelId,
                               core::Id insertId,
                               bool offerOutputs,
                               juce::Component* target)
{
    if (project_ != nullptr)
        ui::showInsertMenu(insertMenuRequest(channelId, insertId, offerOutputs), target);
}

void MixerView::dropOnStrip(core::Id channelId, const juce::String& description, int index, bool copy)
{
    if (project_ == nullptr || !onCommand)
        return;
    const auto* channel = project_->findChannel(channelId);
    if (channel == nullptr)
        return;
    if (description.startsWith("plugin:"))
    {
        const auto type = description.fromFirstOccurrenceOf("plugin:", false, false).toStdString();
        if (isInstrumentType(type, pluginSources_ != nullptr ? pluginSources_->registry : nullptr))
        {
            // An instrument goes in (or replaces) an instrument channel's slot;
            // elsewhere it is refused.
            const auto* track = project_->findTrack(channelId);
            if (track == nullptr || !track->isInstrument())
                return;
            core::Insert instrument;
            instrument.type = type;
            instrument.params = plugins::defaultParameters(type);
            onCommand(std::make_unique<core::SetInstrument>(channelId, instrument));
            if (onInserted)
                onInserted(type);
            return;
        }
        addInsert(channelId, type, index);
        return;
    }
    // "insert:<channel>:<insert>": a slot from a strip (this one or another).
    const auto parts = juce::StringArray::fromTokens(description, ":", "");
    if (parts.size() != 3 || parts[0] != "insert")
        return;
    const auto from = static_cast<core::Id>(parts[1].getLargeIntValue());
    const auto insertId = static_cast<core::Id>(parts[2].getLargeIntValue());
    const auto* source = project_->findChannel(from);
    const auto* insert = source != nullptr ? source->findInsert(insertId) : nullptr;
    if (insert == nullptr || source->insertIndex(insertId) < 0)
        return; // the instrument slot does not move
    if (copy)
    {
        onCommand(std::make_unique<core::AddInserts>(
            channelId, std::vector{snapshotInsert(*insert, actions())}, index, "Copy insert"));
        return;
    }
    if (from == channelId)
    {
        const int at = source->insertIndex(insertId);
        const int to = index < 0    ? static_cast<int>(source->inserts.size()) - 1
                       : index > at ? index - 1
                                    : index;
        if (to != at)
            onCommand(std::make_unique<core::MoveInsert>(channelId, insertId, to));
        return;
    }
    onCommand(std::make_unique<core::MoveInsertToChannel>(from, insertId, channelId, index));
}

void MixerView::addFxSendItems(juce::PopupMenu& menu,
                               std::vector<std::function<void()>>& actions,
                               core::Id channelId)
{
    // A send needs somewhere to go: an FX track made here, the send to it in the
    // same step (post-fader, at -6 dB).
    juce::PopupMenu fx;
    juce::Component::SafePointer<MixerView> safe(this);
    for (const auto& choice : plugins::fxTrackEffects())
    {
        fx.addItem(static_cast<int>(actions.size()) + 1, juce::String(choice.name));
        actions.push_back(
            [safe, channelId, choice]
            {
                if (safe == nullptr || safe->project_ == nullptr)
                    return;
                auto commands =
                    plugins::addFxTrackCommands(*safe->project_, choice.type, choice.name, {channelId});
                if (safe->actions_ != nullptr && safe->actions_->commands)
                    safe->actions_->commands("New FX track", std::move(commands));
                else if (safe->onCommand)
                    for (auto& c : commands)
                        safe->onCommand(std::move(c));
            });
    }
    menu.addSubMenu(juce::String::fromUTF8("New FX track\xe2\x80\xa6"), fx);
}

void MixerView::setOutputChoices(std::vector<std::string> pairs)
{
    if (pairs == outputPairs_)
        return;
    outputPairs_ = std::move(pairs);
    rebuildStrips();
}

void MixerView::setWideStrips(bool wide)
{
    wideRead_ = true; // chosen now: the setting no longer decides
    if (wide == wide_)
        return;
    wide_ = wide;
    if (saveSetting)
        saveSetting("mixer.wideStrips", wide ? "1" : "0");
    rebuildStrips();
}

void MixerView::showViewMenu(juce::Component* target)
{
    if (project_ == nullptr)
        return;
    juce::PopupMenu menu;
    std::vector<std::function<void()>> actions;
    juce::Component::SafePointer<MixerView> safe(this);
    menu.addItem(1, "Wide strips", true, wide_);
    actions.push_back(
        [safe]
        {
            if (safe != nullptr)
                safe->setWideStrips(!safe->wide_);
        });
    // The hidden strips: each to show again, or all.
    juce::PopupMenu hidden;
    std::vector<core::Id> ids;
    const auto look = [&](const core::Channel& c)
    {
        if (!c.mixerHidden)
            return;
        hidden.addItem(static_cast<int>(actions.size()) + 1, juce::String::fromUTF8(c.name.c_str()));
        const core::Id id = c.id;
        ids.push_back(id);
        actions.push_back(
            [safe, id]
            {
                if (safe != nullptr && safe->onCommand)
                    safe->onCommand(std::make_unique<core::SetMixerHidden>(id, false));
            });
    };
    for (const auto& t : project_->tracks)
        if (t.hasAudio())
            look(t);
    for (const auto& b : project_->buses)
        look(b);
    if (!ids.empty())
    {
        hidden.addSeparator();
        hidden.addItem(static_cast<int>(actions.size()) + 1, "Show all");
        actions.push_back(
            [safe, ids]
            {
                if (safe == nullptr || safe->project_ == nullptr || !safe->onCommand)
                    return;
                std::vector<std::unique_ptr<core::Command>> commands;
                for (const auto id : ids)
                    commands.push_back(std::make_unique<core::SetMixerHidden>(id, false));
                if (safe->actions_ != nullptr && safe->actions_->commands)
                    safe->actions_->commands("Show strips", std::move(commands));
                else
                    for (auto& c : commands)
                        safe->onCommand(std::move(c));
            });
    }
    menu.addSubMenu("Hidden strips", hidden, !ids.empty());
    menu.addSeparator();
    menu.addItem(static_cast<int>(actions.size()) + 1, "Close all plugin windows", openEditorCount() > 0);
    actions.push_back(
        [safe]
        {
            if (safe != nullptr)
                safe->closeEditors();
        });
    auto options = juce::PopupMenu::Options();
    if (target != nullptr)
        options = options.withTargetComponent(target);
    menu.showMenuAsync(options,
                       [actions](int result)
                       {
                           if (result >= 1 && static_cast<size_t>(result) <= actions.size())
                               actions[static_cast<size_t>(result) - 1]();
                       });
}

void MixerView::reopenSavedWindows()
{
    pendingWindows_.clear();
    if (project_ == nullptr)
        return;
    const auto look = [&](const core::Channel& c)
    {
        if (c.hasInstrument() && c.instrument.windowOpen)
            pendingWindows_.emplace_back(c.id, c.instrument.id);
        for (const auto& i : c.inserts)
            if (i.windowOpen)
                pendingWindows_.emplace_back(c.id, i.id);
    };
    for (const auto& t : project_->tracks)
        look(t);
    for (const auto& b : project_->buses)
        look(b);
    look(project_->master);
    openSavedWindows();
}

void MixerView::openSavedWindows()
{
    // A hosted plugin opens once its instance is made (a project loads them in
    // steps); a built-in at once.
    std::erase_if(pendingWindows_,
                  [this](const std::pair<core::Id, core::Id>& w)
                  {
                      const auto* channel = project_ != nullptr ? project_->findChannel(w.first) : nullptr;
                      const auto* insert = channel != nullptr ? channel->findInsert(w.second) : nullptr;
                      if (insert == nullptr)
                          return true;
                      const bool hosted = hosting::PluginRegistry::isHostedType(insert->type);
                      if (hosted && (!liveInsert || liveInsert(w.second) == nullptr))
                          return insertTrouble(*insert, actions()).any(); // wait, unless it never comes
                      openInsertEditor(w.first, w.second);
                      return true;
                  });
}

std::string MixerView::insertDisplayName(const std::string& type) const
{
    return host_ != nullptr ? host_->displayName(type) : plugins::BuiltInFactory::displayName(type);
}

void MixerView::openInsertEditor(core::Id channelId, core::Id insertId)
{
    for (auto& w : editors_)
        if (w->editor().insertId() == insertId)
        {
            bringToFront(*w);
            return;
        }
    for (auto& w : pluginWindows_)
        if (w->insertId() == insertId)
        {
            bringToFront(*w);
            return;
        }
    if (project_ == nullptr)
        return;
    const auto* channel = project_->findChannel(channelId);
    const auto* insert =
        channel != nullptr ? const_cast<core::Channel*>(channel)->findInsert(insertId) : nullptr;
    if (insert == nullptr)
        return;

    // A hosted plugin shows its own editor on its live instance.
    if (hosting::PluginRegistry::isHostedType(insert->type))
    {
        const auto live = liveInsert ? liveInsert(insertId) : nullptr;
        auto* presets = live != nullptr ? hosting::HostFactory::presetsFor(*live) : nullptr;
        if (presets == nullptr || live->crashed())
            return; // not loaded (missing plugin), or crashed (never called again): nothing to show
        const juce::String pluginName = juce::String::fromUTF8(insertDisplayName(insert->type).c_str());
        const juce::String trackName = juce::String::fromUTF8(channel->name.c_str());
        const juce::String format = hosting::PluginRegistry::isClapType(insert->type) ? "CLAP" : "VST3";
        const auto setting = [this](const std::string& key)
        { return loadSetting ? loadSetting(key) : std::string{}; };
        // Pinned per plugin; one never pinned or unpinned follows the preference.
        const std::string pinKey = "plugins.pin." + insert->type;
        const auto pinSetting = setting(pinKey);
        const bool pinned =
            pinSetting.empty() ? settings::Reader(setting).flag(settings::pluginsOnTop) : pinSetting == "1";
        // A plugin with a GUI and parameters can show either editor (the bar's toggle).
        const bool canSwitch = hosting::HostFactory::hasOwnEditor(*live) && !live->parameters().empty();
        const bool wantsGeneric =
            canSwitch &&
            std::find(genericEditors_.begin(), genericEditors_.end(), insertId) != genericEditors_.end();

        // The plugin's own editor, whatever its format (or none: the generic one).
        std::optional<hosting::PluginEditor> plugin;
        if (!wantsGeneric)
        {
            plugin = hosting::HostFactory::createEditor(*live);
            if (!plugin)
                return;
        }
        std::unique_ptr<juce::Component> editor = plugin ? std::move(plugin->component) : nullptr;
        const bool resizable = plugin && plugin->resizable;
        {
            if (editor == nullptr)
            {
                // No GUI: the generic parameter editor over the model insert.
                auto generic =
                    std::make_unique<InsertEditor>(channelId, insertId, *live, presets->displayName(), true);
                generic->onCommand = [this](std::unique_ptr<core::Command> c)
                {
                    if (onCommand)
                        onCommand(std::move(c));
                };
                generic->onGestureBegin = [this](const std::string& name)
                {
                    if (onGestureBegin)
                        onGestureBegin(name);
                };
                generic->onGestureEnd = [this]
                {
                    if (onGestureEnd)
                        onGestureEnd();
                };
                generic->onParameterGestureBegin = [live](int index) { live->beginParameterGesture(index); };
                generic->onParameterGestureEnd = [live](int index) { live->endParameterGesture(index); };
                generic->setInsert(*insert);
                editor = std::move(generic);
            }
        }
        auto window = std::make_unique<PluginWindow>(channelId,
                                                     insertId,
                                                     live,
                                                     *presets,
                                                     std::move(editor),
                                                     resizable,
                                                     pluginName,
                                                     trackName,
                                                     format,
                                                     pinned,
                                                     wantsGeneric,
                                                     canSwitch);
        juce::Component::SafePointer<MixerView> safe(this);
        window->onShowGeneric = [safe, channelId, insertId](bool generic)
        {
            if (safe == nullptr)
                return;
            auto& list = safe->genericEditors_;
            std::erase(list, insertId);
            if (generic)
                list.push_back(insertId);
            // The other editor in a new window where this one was.
            std::erase_if(safe->pluginWindows_,
                          [insertId](const auto& w) { return w->insertId() == insertId; });
            safe->openInsertEditor(channelId, insertId);
        };
        if (plugin && plugin->events != nullptr)
        {
            juce::Component::SafePointer<PluginWindow> weak(window.get());
            plugin->events->resized = [weak](int w, int h)
            {
                if (weak != nullptr)
                    weak->editorSizeChanged(w, h);
            };
            plugin->events->resizableKnown = [weak](bool canResize)
            {
                if (weak != nullptr)
                    weak->setResizable(canResize, false);
            };
            plugin->events->closed = [safe, insertId]
            {
                // From the editor's timer: drop the window on the next message.
                juce::MessageManager::callAsync(
                    [safe, insertId]
                    {
                        if (safe == nullptr)
                            return;
                        std::erase_if(safe->pluginWindows_,
                                      [insertId](const auto& w) { return w->insertId() == insertId; });
                        safe->noteEditorWindows();
                    });
            };
        }
        window->onClose = [safe, insertId]
        {
            if (safe == nullptr)
                return;
            std::erase_if(safe->pluginWindows_,
                          [insertId](const auto& w) { return w->insertId() == insertId; });
            safe->noteEditorWindows();
        };
        window->onCommand = [safe](std::unique_ptr<core::Command> c)
        {
            if (safe != nullptr && safe->onCommand)
                safe->onCommand(std::move(c));
        };
        window->onPinChanged = [safe, pinKey](bool pin)
        {
            if (safe != nullptr && safe->saveSetting)
                safe->saveSetting(pinKey, pin ? "1" : "0");
        };
        // Where this plugin's window was last left, and its size, per plugin type,
        // if still on screen.
        const std::string positionKey = "plugins.window." + insert->type;
        const auto saved = juce::StringArray::fromTokens(juce::String(setting(positionKey)), ",", "");
        const auto screen = juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
        if (saved.size() >= 2 && screen.contains(saved[0].getIntValue() + 20, saved[1].getIntValue() + 20))
        {
            window->setTopLeftPosition(saved[0].getIntValue(), saved[1].getIntValue());
            if (saved.size() == 4 && resizable && saved[2].getIntValue() > 100 && saved[3].getIntValue() > 50)
                window->setEditorSize(saved[2].getIntValue(), saved[3].getIntValue());
        }
        else
            window->centreAroundComponent(getTopLevelComponent(), window->getWidth(), window->getHeight());
        juce::Component::SafePointer<PluginWindow> weakWindow(window.get());
        window->onBoundsChanged = [safe, weakWindow, positionKey](juce::Rectangle<int> b)
        {
            if (safe == nullptr || !safe->saveSetting || weakWindow == nullptr)
                return;
            const auto* content = weakWindow->getContentComponent();
            const int w = content != nullptr ? content->getWidth() : 0;
            const int h = content != nullptr ? content->getHeight() - PluginWindow::barHeight : 0;
            safe->saveSetting(positionKey,
                              std::to_string(b.getX()) + "," + std::to_string(b.getY()) + "," +
                                  std::to_string(w) + "," + std::to_string(h));
        };
        window->setInsert(*insert);
        pluginWindows_.push_back(std::move(window));
        noteEditorWindows();
        return;
    }

    const auto processor = plugins::BuiltInFactory().create(insert->type);
    if (processor == nullptr)
        return;
    auto editor = std::make_unique<InsertEditor>(channelId, insertId, *processor);
    editor->onCommand = [this](std::unique_ptr<core::Command> c)
    {
        if (onCommand)
            onCommand(std::move(c));
    };
    editor->onGestureBegin = [this](const std::string& name)
    {
        if (onGestureBegin)
            onGestureBegin(name);
    };
    editor->onGestureEnd = [this]
    {
        if (onGestureEnd)
            onGestureEnd();
    };
    editor->project = [this] { return project_; };
    editor->showBuiltInBody(*insert, liveInsert);
    editor->setInsert(*insert);
    const juce::String title = juce::String(channel->name) + " - " +
                               juce::String(plugins::BuiltInFactory::displayName(insert->type));
    auto window = std::make_unique<EditorWindow>(*this, std::move(editor), title);
    window->centreAroundComponent(getTopLevelComponent(), window->getWidth(), window->getHeight());
    editors_.push_back(std::move(window));
    noteEditorWindows();
}

MixerView::MixerView(const MeterReadings& meters,
                     std::function<engine::LoudnessMeter::Reading()> loudness,
                     std::function<int()> masterMeterSlot)
    : meters_(meters), loudness_(std::move(loudness)), masterMeterSlot_(std::move(masterMeterSlot))
{
    setOpaque(true);
    viewport_.setViewedComponent(&stripsHolder_, false);
    viewport_.setScrollBarsShown(false, true);
    viewport_.setScrollBarThickness(theme_.sizes.scrollbar);
    addAndMakeVisible(viewport_);
    master_ = std::make_unique<MasterStrip>(*this, theme_);
    addAndMakeVisible(*master_);
    addBus_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    addBus_.setTooltip("Add a bus or an FX track");
    addBus_.onClick = [this] { addBus(); };
    stripsHolder_.addAndMakeVisible(addBus_);
    hidden_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    hidden_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    hidden_.setTooltip("Strips hidden from the mixer: show them again");
    hidden_.onClick = [this] { showViewMenu(&hidden_); };
    stripsHolder_.addChildComponent(hidden_);
    stripsHolder_.onMenu = [this] { showViewMenu(nullptr); };
}
MixerView::~MixerView()
{
    closeEditors();
}

bool MixerView::isEditorOpen(core::Id insertId) const
{
    for (const auto& w : pluginWindows_)
        if (w->insertId() == insertId)
            return true;
    for (const auto& w : editors_)
        if (w->editor().insertId() == insertId)
            return true;
    return false;
}

void MixerView::closeEditors()
{
    pluginWindows_.clear();
    editors_.clear();
    noteEditorWindows();
}

void MixerView::noteEditorWindows()
{
    std::vector<core::Id> open;
    for (const auto& w : pluginWindows_)
        open.push_back(w->insertId());
    for (const auto& w : editors_)
        open.push_back(w->editor().insertId());
    std::sort(open.begin(), open.end());
    if (open == shownEditors_)
        return;
    shownEditors_ = std::move(open);
    if (onEditorWindowsChanged)
        onEditorWindowsChanged();
}

void MixerView::setInputChoices(std::vector<InputChoice> audio, std::vector<std::string> midi)
{
    if (audio == inputChoices_ && midi == midiInputNames_)
        return;
    inputChoices_ = std::move(audio);
    midiInputNames_ = std::move(midi);
    rebuildStrips();
}

void MixerView::setSelectedChannel(core::Id channelId)
{
    if (selected_ == channelId)
        return;
    selected_ = channelId;
    rebuildStrips();
}

void MixerView::setProject(const core::Project* project, bool valuesOnly)
{
    if (!wideRead_ && loadSetting)
    {
        wideRead_ = true;
        wide_ = loadSetting("mixer.wideStrips") == "1";
    }
    if (valuesOnly && project == project_ && project_ != nullptr)
    {
        for (auto& strip : strips_)
            if (const auto* channel = project_->findChannel(strip->id()))
                strip->setValues(*channel);
        master_->setMaster(project_->master);
        refreshEditors();
        return;
    }
    project_ = project;
    rebuildStrips();
    refreshEditors();
}

void MixerView::refreshEditors()
{
    // Open editors follow the model (undo, a removed insert closes its window).
    // A plugin window must go before its instance, which the engine drops once no
    // snapshot references it (after this call).
    std::erase_if(
        pluginWindows_,
        [this](const auto& w)
        {
            const auto* channel = project_ != nullptr ? project_->findChannel(w->channelId()) : nullptr;
            const auto* insert =
                channel != nullptr ? const_cast<core::Channel*>(channel)->findInsert(w->insertId()) : nullptr;
            if (insert == nullptr)
                return true;
            // A restarted plugin is a new instance: the window on the old one closes.
            if (liveInsert)
                if (const auto live = liveInsert(w->insertId());
                    live != nullptr && live.get() != w->instance())
                    return true;
            w->setInsert(*insert);
            return false;
        });
    std::erase_if(editors_,
                  [this](const auto& w)
                  {
                      const auto* channel =
                          project_ != nullptr ? project_->findChannel(w->editor().channelId()) : nullptr;
                      const auto* insert =
                          channel != nullptr
                              ? const_cast<core::Channel*>(channel)->findInsert(w->editor().insertId())
                              : nullptr;
                      if (insert == nullptr)
                          return true;
                      w->editor().setInsert(*insert);
                      if (liveInsert)
                          if (const auto live = liveInsert(w->editor().insertId()))
                              w->editor().setLatency(live->latencySamples());
                      return false;
                  });
    noteEditorWindows();
}

void MixerView::addBus()
{
    if (!onCommand || project_ == nullptr)
        return;
    // A group bus, or an FX track that channels feed through sends.
    juce::PopupMenu menu;
    menu.addItem(1, "Bus");
    juce::PopupMenu fx;
    const auto choices = plugins::fxTrackEffects();
    for (size_t i = 0; i < choices.size(); ++i)
        fx.addItem(10 + static_cast<int>(i), juce::String(choices[i].name));
    menu.addSubMenu("FX track", fx);
    juce::Component::SafePointer<MixerView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addBus_),
                       [safe, choices](int choice)
                       {
                           if (safe == nullptr || safe->project_ == nullptr || !safe->onCommand)
                               return;
                           if (choice == 1)
                           {
                               core::Bus bus;
                               bus.name = "Bus " + std::to_string(safe->project_->buses.size() + 1);
                               safe->onCommand(std::make_unique<core::AddBus>(bus));
                           }
                           else if (choice >= 10 && static_cast<size_t>(choice - 10) < choices.size())
                           {
                               const auto& c = choices[static_cast<size_t>(choice - 10)];
                               // Only the AddBus: the strip has no sends to make.
                               safe->onCommand(std::make_unique<core::AddBus>(
                                   plugins::makeFxTrack(*safe->project_, c.type, c.name)));
                           }
                       });
}

void MixerView::rebuildStrips()
{
    // A strip per channel that makes audio: MIDI tracks only make notes (their
    // instrument channels have the strips).
    // Strips the user hid are left out (they still play).
    size_t count = 0;
    hiddenCount_ = 0;
    if (project_ != nullptr)
    {
        const auto tally = [&](const core::Channel& c)
        {
            if (c.mixerHidden)
                ++hiddenCount_;
            else
                ++count;
        };
        for (const auto& t : project_->tracks)
            if (t.hasAudio())
                tally(t);
        for (const auto& b : project_->buses)
            tally(b);
    }
    while (strips_.size() > count)
        strips_.pop_back();
    while (strips_.size() < count)
    {
        auto strip = std::make_unique<Strip>(*this, theme_);
        stripsHolder_.addAndMakeVisible(*strip);
        strips_.push_back(std::move(strip));
    }
    if (project_ != nullptr)
    {
        size_t i = 0;
        for (size_t t = 0; t < project_->tracks.size(); ++t)
        {
            if (!project_->tracks[t].hasAudio() || project_->tracks[t].mixerHidden)
                continue;
            strips_[i++]->setChannel(
                project_->tracks[t], false, *project_, engine::trackMeterSlot(static_cast<int>(t)));
        }
        for (size_t b = 0; b < project_->buses.size(); ++b)
            if (!project_->buses[b].mixerHidden)
                strips_[i++]->setChannel(project_->buses[b],
                                         true,
                                         *project_,
                                         engine::busMeterSlot(*project_, static_cast<int>(b)));
        master_->setMaster(project_->master);
    }
    hidden_.setButtonText(juce::String(hiddenCount_) + " hidden");
    hidden_.setVisible(hiddenCount_ > 0);
    layoutStrips();
    repaint();
}
void MixerView::layoutStrips()
{
    // Strips at the strip width with 2 px gaps; the bus group after a lineSubtle
    // gap; the "+ Bus" ghost at the end (and the hidden strips' button under it).
    const int width = wide_ ? theme_.sizes.mixerStripWideWidth : theme_.sizes.mixerStripWidth;
    const int gap = theme_.spacing.xxs;
    // When they are wider than the view its horizontal scrollbar shows along the
    // bottom: the strips stop above it, so it never covers the output selectors.
    const bool anyBus =
        std::any_of(strips_.begin(), strips_.end(), [](const auto& strip) { return strip->isBus(); });
    const int contentWidth =
        static_cast<int>(strips_.size()) * (width + gap) + (anyBus ? theme_.spacing.sm : 0) + 96 + gap;
    const int height =
        viewport_.getHeight() - (contentWidth > viewport_.getWidth() ? viewport_.getScrollBarThickness() : 0);
    RowPlan rows;
    for (const auto& strip : strips_)
        strip->addTo(rows);
    const bool rowsChanged = !(rows == rows_);
    rows_ = rows;
    int x = 0;
    bool busGap = false;
    for (auto& strip : strips_)
    {
        if (strip->isBus() && !busGap)
        {
            busGap = true;
            x += theme_.spacing.sm;
        }
        const bool moved = strip->getWidth() != width || strip->getHeight() != height;
        strip->setBounds(x, 0, width, height);
        if (rowsChanged && !moved)
            strip->resized();
        x += width + gap;
    }
    if (!strips_.empty())
        master_->alignTo(strips_.front()->faderBand(), strips_.front()->valueY());
    else
        master_->alignTo({}, 0);
    addBus_.setBounds(x, theme_.spacing.xs, 72, theme_.sizes.controlCompact);
    hidden_.setBounds(x, addBus_.getBottom() + theme_.spacing.xs, 96, theme_.sizes.controlCompact);
    stripsHolder_.setSize(std::max(1, x + 96 + gap), std::max(1, height));
}
void MixerView::pushMeters()
{
    const double elapsed = meters_.elapsedMs();
    if (!pendingWindows_.empty())
        openSavedWindows();
    if (outputLatency && project_ != nullptr)
        master_->setLatency(outputLatency(), project_->sampleRate);
    for (auto& strip : strips_)
        strip->pushMeter(meters_.reading(strip->meterSlot()), elapsed);
    if (project_ != nullptr)
    {
        const auto reading =
            meters_.reading(masterMeterSlot_ ? masterMeterSlot_() : engine::masterMeterSlot(*project_));
        if (reading.peakLeft >= 1.0f || reading.peakRight >= 1.0f)
            masterClipped_ = true;
        master_->pushMeter(reading, elapsed);
    }
    if (loudness_)
        master_->pushLoudness(loudness_());
}
void MixerView::paint(juce::Graphics& g)
{
    const FrameStats::Scope timing;
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    // The separator before the master, and the bus group's slate line.
    g.setColour(c.lineSubtle);
    g.fillRect(master_->getX() - theme_.spacing.sm, 0, 1, getHeight());
    bool firstBus = true;
    for (const auto& strip : strips_)
        if (strip->isBus() && firstBus)
        {
            firstBus = false;
            const int x = viewport_.getX() + strip->getX() - viewport_.getViewPositionX() - theme_.spacing.xs;
            if (x > viewport_.getX() && x < viewport_.getRight())
                g.fillRect(x, 0, 1, viewport_.getHeight());
        }
    if (strips_.empty())
    {
        g.setColour(c.textTertiary);
        g.setFont(theme_.body());
        g.drawText(hiddenCount_ > 0 ? juce::String("Every strip is hidden")
                                    : juce::String("Add a track to start mixing"),
                   viewport_.getBounds(),
                   juce::Justification::centred);
    }
}
void MixerView::resized()
{
    auto area = getLocalBounds();
    master_->setBounds(area.removeFromRight(theme_.sizes.mixerStripWideWidth));
    area.removeFromRight(theme_.spacing.lg);
    viewport_.setBounds(area);
    layoutStrips();
}
} // namespace northlight::ui
