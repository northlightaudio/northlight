#include "ui/MixerView.h"

#include "core/Command.h"
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

private:
    MixerView& owner_;
    InsertEditor* editor_;
};

// ----- view ---------------------------------------------------------------------------------

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
                if (safe == nullptr || !safe->onCommand)
                    return;
                core::Insert insert;
                insert.type = type;
                insert.params = plugins::defaultParameters(type);
                safe->onCommand(std::make_unique<core::AddInsert>(channelId, insert));
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

void MixerView::showInsertMenu(core::Id channelId, core::Id insertId, bool offerOutputs)
{
    if (project_ == nullptr)
        return;
    const auto* channel = project_->findChannel(channelId);
    const auto* insert = channel != nullptr ? channel->findInsert(insertId) : nullptr;
    if (insert == nullptr)
        return;
    juce::Component::SafePointer<MixerView> safe(this);
    const auto command = [safe](std::unique_ptr<core::Command> c)
    {
        if (safe != nullptr && safe->onCommand)
            safe->onCommand(std::move(c));
    };
    juce::PopupMenu menu;
    std::vector<std::function<void()>> actions;
    int item = 1;
    menu.addItem(item++, "Open");
    actions.push_back(
        [safe, channelId, insertId]
        {
            if (safe != nullptr)
                safe->openInsertEditor(channelId, insertId);
        });
    menu.addItem(item++, "Bypass", true, insert->bypass);
    const bool bypass = insert->bypass;
    actions.push_back([command, channelId, insertId, bypass]
                      { command(std::make_unique<core::SetInsertBypass>(channelId, insertId, !bypass)); });
    const int index = channel->insertIndex(insertId);
    menu.addItem(item++, "Move up", index > 0);
    actions.push_back([command, channelId, insertId, index]
                      { command(std::make_unique<core::MoveInsert>(channelId, insertId, index - 1)); });
    menu.addItem(item++, "Move down", index + 1 < static_cast<int>(channel->inserts.size()));
    actions.push_back([command, channelId, insertId, index]
                      { command(std::make_unique<core::MoveInsert>(channelId, insertId, index + 1)); });
    juce::PopupMenu mix;
    for (const int percent : {0, 25, 50, 75, 100})
    {
        mix.addItem(
            item++, juce::String(percent) + "%", true, std::abs(insert->mix - percent / 100.0f) < 0.01f);
        actions.push_back(
            [command, channelId, insertId, percent]
            { command(std::make_unique<core::SetInsertMix>(channelId, insertId, percent / 100.0f)); });
    }
    menu.addSubMenu("Wet/dry", mix);
    // Key (sidechain) source, for inserts that take one: tracks for anyone, buses
    // too when the channel is a bus. The master's inserts run on the finished mix
    // and take no key.
    const auto live = liveInsert ? liveInsert(insertId) : nullptr;
    if (live != nullptr && live->acceptsSidechain() && channelId != core::masterId)
    {
        juce::PopupMenu key;
        addSidechainItems(key, actions, *project_, channelId, *insert, onCommand);
        item = static_cast<int>(actions.size()) + 1;
        menu.addSubMenu("Sidechain", key);
    }
    // Extra outputs of a multi-output instrument: one aux track per pair.
    if (live != nullptr && live->auxOutputCount() > 0 && offerOutputs)
    {
        juce::PopupMenu outputs;
        for (int output = 1; output <= live->auxOutputCount(); ++output)
        {
            bool exists = false;
            for (const auto& t : project_->tracks)
                if (t.isAux() && t.auxTrack == channelId && t.auxInsert == insertId && t.auxOutput == output)
                    exists = true;
            outputs.addItem(item++, "Track for output " + juce::String(output + 1), !exists, exists);
            const std::string name = channel->name;
            actions.push_back(
                [command, channelId, insertId, output, name]
                {
                    core::Track aux;
                    aux.kind = core::TrackKind::aux;
                    aux.name = name + " out " + std::to_string(output + 1);
                    aux.auxTrack = channelId;
                    aux.auxInsert = insertId;
                    aux.auxOutput = output;
                    command(std::make_unique<core::AddTrack>(aux));
                });
        }
        menu.addSubMenu("Outputs", outputs);
    }
    menu.addSeparator();
    menu.addItem(item++, "Remove");
    actions.push_back([command, channelId, insertId]
                      { command(std::make_unique<core::RemoveInsert>(channelId, insertId)); });
    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [actions](int result)
                       {
                           if (result >= 1 && static_cast<size_t>(result) <= actions.size())
                               actions[static_cast<size_t>(result) - 1]();
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
        if (presets == nullptr)
            return; // not loaded (missing plugin): nothing to show
        const juce::String pluginName = juce::String::fromUTF8(insertDisplayName(insert->type).c_str());
        const juce::String trackName = juce::String::fromUTF8(channel->name.c_str());
        const juce::String format = hosting::PluginRegistry::isClapType(insert->type) ? "CLAP" : "VST3";
        const auto setting = [this](const std::string& key)
        { return loadSetting ? loadSetting(key) : std::string{}; };
        const bool pinned = setting("plugins.pin") == "1";

        // The plugin's own editor, whatever its format.
        auto plugin = hosting::HostFactory::createEditor(*live);
        if (!plugin)
            return;
        std::unique_ptr<juce::Component> editor = std::move(plugin->component);
        const bool resizable = plugin->resizable;
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
                                                     pinned);
        juce::Component::SafePointer<MixerView> safe(this);
        if (plugin->events != nullptr)
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
        window->onPinChanged = [safe](bool pin)
        {
            if (safe != nullptr && safe->saveSetting)
                safe->saveSetting("plugins.pin", pin ? "1" : "0");
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
    size_t audible = 0;
    if (project_ != nullptr)
        for (const auto& t : project_->tracks)
            audible += t.hasAudio() ? 1 : 0;
    const size_t count = project_ != nullptr ? audible + project_->buses.size() : 0;
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
            if (!project_->tracks[t].hasAudio())
                continue;
            strips_[i++]->setChannel(
                project_->tracks[t], false, *project_, engine::trackMeterSlot(static_cast<int>(t)));
        }
        for (size_t b = 0; b < project_->buses.size(); ++b, ++i)
            strips_[i]->setChannel(
                project_->buses[b], true, *project_, engine::busMeterSlot(*project_, static_cast<int>(b)));
        master_->setMaster(project_->master);
    }
    layoutStrips();
    repaint();
}
void MixerView::layoutStrips()
{
    // Strips at the strip width with 2 px gaps; the bus group after a lineSubtle
    // gap; the "+ Bus" ghost at the end.
    const int width = theme_.sizes.mixerStripWidth;
    const int gap = theme_.spacing.xxs;
    // When they are wider than the view its horizontal scrollbar shows along the
    // bottom: the strips stop above it, so it never covers the output selectors.
    const bool anyBus =
        std::any_of(strips_.begin(), strips_.end(), [](const auto& strip) { return strip->isBus(); });
    const int contentWidth =
        static_cast<int>(strips_.size()) * (width + gap) + (anyBus ? theme_.spacing.sm : 0) + 56 + gap;
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
    addBus_.setBounds(x, theme_.spacing.xs, 56, theme_.sizes.controlCompact);
    stripsHolder_.setSize(std::max(1, x + 56 + gap), std::max(1, height));
}
void MixerView::pushMeters()
{
    const double elapsed = meters_.elapsedMs();
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
        g.drawText("Add a track to start mixing", viewport_.getBounds(), juce::Justification::centred);
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
