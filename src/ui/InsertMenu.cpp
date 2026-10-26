#include "ui/InsertMenu.h"

#include "core/Command.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "ui/InstrumentOutputs.h"
#include "ui/Number.h"
#include "ui/SidechainMenu.h"
#include "ui/strip/EffectPresetLibrary.h"

#include <cmath>

namespace northlight::ui
{

// ----- a slot's state ----------------------------------------------------------------------------

const char* InsertTrouble::label() const noexcept
{
    switch (kind)
    {
        case Kind::missing:
            return "Not installed";
        case Kind::failed:
            return "Failed";
        case Kind::crashed:
            return "Crashed";
        case Kind::none:
            break;
    }
    return "";
}

InsertTrouble insertTrouble(const core::Insert& insert, const PluginActions* actions)
{
    InsertTrouble t;
    if (insert.type.empty())
        return t;
    const auto live = actions != nullptr && actions->live ? actions->live(insert.id) : nullptr;
    if (live != nullptr)
    {
        if (live->crashed())
        {
            t.kind = InsertTrouble::Kind::crashed;
            t.reason = "It crashed while processing and was switched off; its audio passes through. Reload "
                       "plugin starts it again from its last saved state.";
        }
        return t;
    }
    const auto* host = actions != nullptr ? actions->host : nullptr;
    if (!hosting::PluginRegistry::isHostedType(insert.type))
    {
        // A built-in this version does not have (a project from a newer one).
        if (plugins::findBuiltIn(insert.type) == nullptr && !plugins::strip::isStripType(insert.type))
        {
            t.kind = InsertTrouble::Kind::missing;
            t.reason = "This version of Northlight has no " + insert.type + "; its settings are kept.";
        }
        return t;
    }
    if (host == nullptr)
        return t;
    if (host->registry().findByType(insert.type) == nullptr)
    {
        t.kind = InsertTrouble::Kind::missing;
        t.reason = "Not installed on this computer, or not scanned yet. Its settings are kept; scan for "
                   "plugins once it is installed.";
        return t;
    }
    if (const auto reason = host->failure(insert.type); !reason.empty())
    {
        t.kind = InsertTrouble::Kind::failed;
        t.reason = "It failed to load: " + reason + ".";
    }
    return t;
}

std::string insertName(const core::Insert& insert, const PluginActions* actions)
{
    if (hosting::PluginRegistry::isHostedType(insert.type))
    {
        const auto* host = actions != nullptr ? actions->host : nullptr;
        if (const auto* info = host != nullptr ? host->registry().findByType(insert.type) : nullptr)
            return info->name;
        return insert.pluginName.empty() ? std::string("Missing plugin") : insert.pluginName;
    }
    return plugins::BuiltInFactory::displayName(insert.type);
}

// ----- copied inserts ----------------------------------------------------------------------------

namespace
{
std::vector<core::Insert>& clipboard()
{
    static std::vector<core::Insert> copied;
    return copied;
}
} // namespace

const std::vector<core::Insert>& copiedInserts()
{
    return clipboard();
}

void copyInserts(std::vector<core::Insert> inserts)
{
    clipboard() = std::move(inserts);
}

core::Insert snapshotInsert(const core::Insert& insert, const PluginActions* actions)
{
    auto copy = insert;
    if (const auto live = actions != nullptr && actions->live ? actions->live(insert.id) : nullptr;
        live != nullptr && !live->crashed() && live->ownsParameters())
        if (auto state = live->state(); !state.empty())
            copy.state = std::move(state);
    return copy;
}

// ----- hosted presets ------------------------------------------------------------------------------

namespace
{
std::unique_ptr<juce::FileChooser>& presetChooser()
{
    static std::unique_ptr<juce::FileChooser> chooser;
    return chooser;
}
} // namespace

void loadHostedPreset(hosting::PluginPresets& presets,
                      const std::shared_ptr<engine::InsertProcessor>& live,
                      core::Id channelId,
                      core::Id insertId,
                      const std::string& name,
                      const std::function<bool()>& load,
                      const std::function<void(std::unique_ptr<core::Command>)>& onCommand)
{
    if (live == nullptr || live->crashed())
        return;
    auto before = live->state();
    if (!load())
        return;
    presets.noteLoadedPreset(name);
    auto after = live->state();
    if (before != after && onCommand)
        onCommand(std::make_unique<core::SetPluginState>(
            channelId, insertId, std::move(before), std::move(after), "Preset " + name));
}

void addHostedPresetItems(juce::PopupMenu& menu,
                          std::vector<std::function<void()>>& actions,
                          hosting::PluginPresets& presets,
                          std::shared_ptr<engine::InsertProcessor> live,
                          core::Id channelId,
                          core::Id insertId,
                          std::function<void(std::unique_ptr<core::Command>)> onCommand)
{
    const auto next = [&] { return static_cast<int>(actions.size()) + 1; };
    auto* p = &presets;
    const auto programs = presets.factoryPrograms();
    const auto current = presets.currentPresetName();
    if (!programs.empty())
    {
        juce::PopupMenu factory;
        const int at = presets.currentProgram();
        for (size_t i = 0; i < programs.size(); ++i)
        {
            const int index = static_cast<int>(i);
            factory.addItem(next(), juce::String::fromUTF8(programs[i].c_str()), true, index == at);
            const auto name = programs[i];
            actions.push_back(
                [p, live, channelId, insertId, index, name, onCommand]
                {
                    loadHostedPreset(
                        *p,
                        live,
                        channelId,
                        insertId,
                        name,
                        [p, index]
                        {
                            p->setProgram(index);
                            return true;
                        },
                        onCommand);
                });
        }
        menu.addSubMenu("Factory", factory);
    }
    if (presets.supportsPresetFiles())
    {
        const auto folder = presets.userPresetFolder();
        const auto files = presets.userPresetFiles();
        const juce::String extension = "." + juce::String(presets.presetFileExtension());
        if (!files.empty())
        {
            juce::PopupMenu user;
            for (const auto& file : files)
            {
                const auto shown =
                    file.getRelativePathFrom(folder).upToLastOccurrenceOf(extension, false, false);
                user.addItem(next(), shown, true, shown.toStdString() == current);
                actions.push_back(
                    [p, live, channelId, insertId, file, onCommand]
                    {
                        loadHostedPreset(
                            *p,
                            live,
                            channelId,
                            insertId,
                            file.getFileNameWithoutExtension().toStdString(),
                            [p, file] { return p->loadPresetFile(file); },
                            onCommand);
                    });
            }
            menu.addSubMenu("User", user);
        }
        if (!programs.empty() || !files.empty())
            menu.addSeparator();
        for (const bool save : {false, true})
        {
            menu.addItem(
                next(),
                juce::String::fromUTF8(save ? "Save preset as\xe2\x80\xa6" : "Load preset file\xe2\x80\xa6"));
            actions.push_back(
                [p, live, channelId, insertId, onCommand, save]
                {
                    const auto dir = p->userPresetFolder();
                    dir.createDirectory();
                    const juce::String ext = juce::String(p->presetFileExtension());
                    auto& chooser = presetChooser();
                    chooser = std::make_unique<juce::FileChooser>(
                        save ? "Save preset" : "Load preset", dir.getChildFile("Preset." + ext), "*." + ext);
                    const int mode = save ? (juce::FileBrowserComponent::saveMode |
                                             juce::FileBrowserComponent::canSelectFiles |
                                             juce::FileBrowserComponent::warnAboutOverwriting)
                                          : (juce::FileBrowserComponent::openMode |
                                             juce::FileBrowserComponent::canSelectFiles);
                    // The instance is held while the chooser is up, so the
                    // presets object it owns stays valid.
                    chooser->launchAsync(
                        mode,
                        [p, live, channelId, insertId, onCommand, save, ext](const juce::FileChooser& fc)
                        {
                            auto file = fc.getResult();
                            if (file == juce::File())
                                return;
                            if (save)
                            {
                                if (!file.hasFileExtension(ext))
                                    file = file.withFileExtension(ext);
                                if (p->savePresetFile(file))
                                    p->noteLoadedPreset(file.getFileNameWithoutExtension().toStdString());
                                return;
                            }
                            loadHostedPreset(
                                *p,
                                live,
                                channelId,
                                insertId,
                                file.getFileNameWithoutExtension().toStdString(),
                                [p, file] { return p->loadPresetFile(file); },
                                onCommand);
                        });
                });
        }
    }
    if (programs.empty() && !presets.supportsPresetFiles())
        menu.addItem(next(), "No presets", false);
}

juce::String latencyText(int samples, double sampleRate)
{
    const double ms = sampleRate > 0.0 ? 1000.0 * samples / sampleRate : 0.0;
    return juce::String(samples) + " smp (" + fixedText(ms, ms >= 10.0 ? 0 : 1) + " ms)";
}

// ----- the menu ----------------------------------------------------------------------------------

void addInsertMenuItems(juce::PopupMenu& menu,
                        std::vector<std::function<void()>>& actions,
                        const InsertMenuRequest& r)
{
    if (r.project == nullptr)
        return;
    const auto* channel = r.project->findChannel(r.channelId);
    const auto* insert = channel != nullptr ? channel->findInsert(r.insertId) : nullptr;
    if (insert == nullptr)
        return;
    const auto next = [&] { return static_cast<int>(actions.size()) + 1; };
    const auto command = [onCommand = r.onCommand](std::unique_ptr<core::Command> c)
    {
        if (onCommand)
            onCommand(std::move(c));
    };
    const auto* pa = r.actions;
    const core::Id channelId = r.channelId, insertId = r.insertId;
    const auto type = insert->type;
    const auto live = pa != nullptr && pa->live ? pa->live(insertId) : nullptr;
    const auto trouble = insertTrouble(*insert, pa);
    const bool loaded = live != nullptr && !trouble.any();
    const bool hosted = hosting::PluginRegistry::isHostedType(type);

    // What is wrong, and what can be done about it, first.
    if (trouble.any())
    {
        menu.addSectionHeader(juce::String(trouble.label()) + juce::String::fromUTF8(" \xc2\xb7 ") +
                              juce::String::fromUTF8(insertName(*insert, pa).c_str()));
        if (trouble.kind == InsertTrouble::Kind::crashed && pa != nullptr && pa->reload)
        {
            menu.addItem(next(), "Reload plugin");
            actions.push_back([pa, insertId] { pa->reload(insertId); });
        }
        if (trouble.kind == InsertTrouble::Kind::failed && pa != nullptr)
        {
            menu.addItem(next(), "Retry", static_cast<bool>(pa->retry));
            actions.push_back(
                [pa, type]
                {
                    if (pa->retry)
                        pa->retry(type);
                });
            menu.addItem(next(), "Blocklist", static_cast<bool>(pa->blocklist));
            actions.push_back(
                [pa, type]
                {
                    if (pa->blocklist)
                        pa->blocklist(type);
                });
        }
        if (trouble.kind == InsertTrouble::Kind::missing && hosted && pa != nullptr)
        {
            menu.addItem(
                next(), juce::String::fromUTF8("Scan for plugins\xe2\x80\xa6"), static_cast<bool>(pa->scan));
            actions.push_back(
                [pa]
                {
                    if (pa->scan)
                        pa->scan();
                });
        }
        menu.addSeparator();
    }

    menu.addItem(next(), "Open", loaded);
    actions.push_back(
        [pa, channelId, insertId]
        {
            if (pa != nullptr && pa->openEditor)
                pa->openEditor(channelId, insertId);
        });
    const bool bypass = insert->bypass;
    menu.addItem(next(), "Bypass", true, bypass);
    actions.push_back([command, channelId, insertId, bypass]
                      { command(std::make_unique<core::SetInsertBypass>(channelId, insertId, !bypass)); });

    // Presets: a built-in's library, a hosted plugin's programs and files.
    if (loaded && hosted)
        if (auto* presets = hosting::HostFactory::presetsFor(*live))
        {
            juce::PopupMenu sub;
            addHostedPresetItems(sub, actions, *presets, live, channelId, insertId, r.onCommand);
            const auto name = presets->currentPresetName();
            menu.addSubMenu(name.empty() ? juce::String("Presets")
                                         : "Presets: " + juce::String::fromUTF8(name.c_str()),
                            sub);
        }
    if (!hosted && !plugins::factoryPresets(type).empty())
    {
        auto& library = strip::EffectPresetLibrary::instance();
        library.reload(type);
        const auto& list = library.presets(type);
        const int current = library.match(type, insert->params);
        juce::PopupMenu sub;
        bool yours = false;
        for (size_t i = 0; i < list.size(); ++i)
        {
            if (list[i].user && !yours)
            {
                sub.addSectionHeader("Yours");
                yours = true;
            }
            sub.addItem(next(), list[i].name, true, static_cast<int>(i) == current);
            const auto values = list[i].values;
            const auto name = list[i].name.toStdString();
            actions.push_back(
                [command, channelId, insertId, values, name]
                {
                    command(std::make_unique<core::SetInsertParameters>(
                        channelId, insertId, values, "Preset " + name));
                });
        }
        menu.addSubMenu(current >= 0 ? "Presets: " + list[static_cast<size_t>(current)].name
                                     : juce::String("Presets: Custom"),
                        sub);
    }

    // Replace: the same menu as adding one, the choice taking this slot.
    if (r.instrument)
    {
        auto replace = buildPluginMenu(PluginKind::instrument, r.sources, next());
        for (const auto& t : replace.types)
            actions.push_back(
                [onReplace = r.onReplaceInstrument, t]
                {
                    if (onReplace)
                        onReplace(t);
                });
        menu.addSubMenu("Replace instrument", replace.menu);
    }
    else
    {
        auto replace = buildPluginMenu(PluginKind::effect, r.sources, next());
        for (const auto& t : replace.types)
            actions.push_back(
                [command, channelId, insertId, t]
                {
                    core::Insert replacement;
                    replacement.type = t;
                    replacement.params = plugins::defaultParameters(t);
                    command(std::make_unique<core::ReplaceInsert>(channelId, insertId, replacement));
                });
        menu.addSubMenu("Replace", replace.menu);
    }

    if (!r.instrument)
    {
        menu.addSeparator();
        const int index = channel->insertIndex(insertId);
        menu.addItem(next(), "Move up", index > 0);
        actions.push_back([command, channelId, insertId, index]
                          { command(std::make_unique<core::MoveInsert>(channelId, insertId, index - 1)); });
        menu.addItem(next(), "Move down", index + 1 < static_cast<int>(channel->inserts.size()));
        actions.push_back([command, channelId, insertId, index]
                          { command(std::make_unique<core::MoveInsert>(channelId, insertId, index + 1)); });
        juce::PopupMenu mix;
        for (const int percent : {0, 25, 50, 75, 100})
        {
            mix.addItem(
                next(), juce::String(percent) + "%", true, std::abs(insert->mix - percent / 100.0f) < 0.01f);
            actions.push_back(
                [command, channelId, insertId, percent]
                { command(std::make_unique<core::SetInsertMix>(channelId, insertId, percent / 100.0f)); });
        }
        menu.addSubMenu("Wet/dry", mix);
    }
    // Key (sidechain) source, for inserts that take one; the master's take none.
    if (live != nullptr && live->acceptsSidechain() && channelId != core::masterId)
    {
        juce::PopupMenu key;
        addSidechainItems(key, actions, *r.project, channelId, *insert, r.onCommand);
        menu.addSubMenu(insert->sidechain != core::invalidId
                            ? "Sidechain: " + sidechainLabel(*r.project, *insert)
                            : juce::String("Sidechain"),
                        key);
    }
    // A multi-output instrument's extra pairs as output tracks.
    if (live != nullptr && live->auxOutputCount() > 0 && r.offerOutputs)
        if (const auto* track = r.project->findTrack(channelId))
        {
            juce::PopupMenu outputs;
            for (int output = 1; output <= live->auxOutputCount(); ++output)
            {
                bool exists = false;
                for (const auto& t : r.project->tracks)
                    if (t.isAux() && t.auxTrack == channelId && t.auxInsert == insertId &&
                        t.auxOutput == output)
                        exists = true;
                outputs.addItem(next(), "Track for " + juce::String(outputPairName(output)), !exists, exists);
                auto made = makeOutputTrack(*track, output, live->auxOutputName(output - 1));
                made.auxInsert = insertId; // the slot's own (an insert's, not only the instrument's)
                actions.push_back([command, made] { command(std::make_unique<core::AddTrack>(made)); });
            }
            menu.addSubMenu("Outputs", outputs);
        }

    // Copy and paste: the slot, the whole chain, and what was copied after it.
    if (!r.instrument)
    {
        menu.addSeparator();
        menu.addItem(next(), "Copy");
        actions.push_back([pa, i = *insert] { copyInserts({snapshotInsert(i, pa)}); });
        menu.addItem(next(), "Copy chain", channel->inserts.size() > 1);
        actions.push_back(
            [pa, chain = channel->inserts]
            {
                std::vector<core::Insert> copies;
                for (const auto& i : chain)
                    copies.push_back(snapshotInsert(i, pa));
                copyInserts(std::move(copies));
            });
        const auto& copied = copiedInserts();
        const auto pasteName =
            copied.size() == 1
                ? "Paste " + juce::String::fromUTF8(insertName(copied.front(), pa).c_str()) + " after"
            : copied.empty() ? juce::String("Paste after")
                             : "Paste " + juce::String(copied.size()) + " inserts after";
        menu.addItem(next(), pasteName, !copied.empty());
        const int after = channel->insertIndex(insertId) + 1;
        actions.push_back(
            [command, channelId, after]
            {
                const auto& c = copiedInserts();
                if (!c.empty())
                    command(std::make_unique<core::AddInserts>(
                        channelId, c, after, c.size() == 1 ? "Paste insert" : "Paste inserts"));
            });
    }
    if (hosted && pa != nullptr && pa->showInBrowser)
    {
        menu.addItem(next(), "Show in plugin browser");
        actions.push_back([pa, type] { pa->showInBrowser(type); });
    }
    if (live != nullptr && live->latencySamples() > 0)
    {
        menu.addItem(next(), "Latency " + latencyText(live->latencySamples(), r.project->sampleRate), false);
        actions.push_back([] {}); // a line to read, not to pick
    }

    menu.addSeparator();
    if (r.instrument)
    {
        menu.addItem(next(), "Remove instrument");
        actions.push_back([command, channelId]
                          { command(std::make_unique<core::SetInstrument>(channelId, core::Insert{})); });
    }
    else
    {
        menu.addItem(next(), "Remove");
        actions.push_back([command, channelId, insertId]
                          { command(std::make_unique<core::RemoveInsert>(channelId, insertId)); });
    }
}

void showInsertMenu(const InsertMenuRequest& request, juce::Component* target)
{
    juce::PopupMenu menu;
    std::vector<std::function<void()>> actions;
    addInsertMenuItems(menu, actions, request);
    if (actions.empty())
        return;
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

} // namespace northlight::ui
