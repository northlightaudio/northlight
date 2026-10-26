#include "ui/arrange/ArrangeMenus.h"

#include "core/Command.h"
#include "core/Comping.h"
#include "plugins/FxTracks.h"
#include "ui/ArrangeView.h"
#include "ui/ColourMenu.h"
#include "ui/Icons.h"
#include "ui/PluginMenu.h"
#include "ui/Shortcuts.h"

namespace northlight::ui
{

namespace
{
using Safe = juce::Component::SafePointer<ArrangeView>;

// Adds items that act themselves; ids only need to be unique within the menu.
struct Items
{
    juce::PopupMenu& menu;
    int& nextId;

    void add(const juce::String& text,
             std::function<void()> action,
             bool enabled = true,
             bool ticked = false,
             const juce::String& keys = {}) const
    {
        juce::PopupMenu::Item item(text);
        item.itemID = nextId++;
        item.isEnabled = enabled;
        item.isTicked = ticked;
        item.shortcutKeyDescription = keys;
        item.action = std::move(action);
        menu.addItem(std::move(item));
    }
};

// What an instrument channel is called in a menu: its name and its instrument's.
juce::String channelLabel(const core::Track& t,
                          const std::function<std::string(const std::string&)>& displayName)
{
    juce::String text = juce::String::fromUTF8(t.name.c_str());
    if (t.hasInstrument())
        text << "  ("
             << juce::String::fromUTF8(displayName ? displayName(t.instrument.type).c_str()
                                                   : t.instrument.type.c_str())
             << ")";
    return text;
}

juce::PopupMenu::Options at(juce::Point<int> screen)
{
    return juce::PopupMenu::Options().withTargetScreenArea({screen.x, screen.y, 1, 1});
}

// Plugin menus map their own ids back to types; the rest of the menu's items act
// themselves, from ids below the plugins'.
constexpr int pluginIds = 5000;
} // namespace

void ArrangeMenus::addFxItems(juce::PopupMenu& menu, int& nextId, std::vector<core::Id> sendFrom) const
{
    const Items items{menu, nextId};
    Safe safe(&view_);
    for (const auto& choice : plugins::fxTrackEffects())
    {
        const std::string type = choice.type, name = choice.name;
        items.add(juce::String(choice.name),
                  [safe, type, name, sendFrom]
                  {
                      if (safe == nullptr || safe->project_ == nullptr)
                          return;
                      safe->emit(plugins::addFxTrackCommands(*safe->project_, type, name, sendFrom),
                                 sendFrom.empty() ? "Add FX track" : "Add FX track and send");
                  });
    }
}

void ArrangeMenus::clipMenu(const edit::ClipRef& ref, juce::Point<int> screen)
{
    auto& v = view_;
    const auto* project = v.project_;
    const auto* track = project != nullptr ? project->findTrack(ref.track) : nullptr;
    const auto* clip = track != nullptr && !ref.midi ? track->findClip(ref.clip) : nullptr;
    const auto* midiClip = track != nullptr && ref.midi ? track->findMidiClip(ref.clip) : nullptr;
    const auto span = v.spanOf(ref);
    if (track == nullptr || !span || (clip == nullptr && midiClip == nullptr))
        return;
    // Where the menu opened, for "Split here".
    const auto local = v.getLocalPoint(nullptr, screen);
    const auto splitHere = v.splitPosition(local.x, *span);
    const auto selection = v.selection_; // the menu acts on the selection (the clip is in it)
    const bool muted = edit::isMuted(*project, ref);
    const bool anyPlaying = std::any_of(selection.begin(),
                                        selection.end(),
                                        [&](const edit::ClipRef& r) { return !edit::isMuted(*project, r); });
    const bool many = selection.size() > 1;
    Safe safe(&v);
    // Runs an edit made from the project when the item is chosen.
    const auto run = [safe](std::function<edit::Edit(const core::Project&)> make)
    {
        return [safe, make]
        {
            if (safe == nullptr || safe->project_ == nullptr)
                return;
            auto e = make(*safe->project_);
            safe->emit(std::move(e.commands), e.name);
        };
    };
    const auto original = clip != nullptr ? *clip : core::AudioClip{};
    const auto trackId = ref.track;
    // Sets the clip's stretch, transposition, length and tempo following.
    const auto setStretch = [safe, trackId, id = original.id](
                                double stretch, double transpose, core::SamplePosition length, bool follow)
    {
        return [=]
        {
            if (safe == nullptr)
                return;
            std::vector<std::unique_ptr<core::Command>> commands;
            commands.push_back(
                std::make_unique<core::SetClipStretch>(trackId, id, stretch, transpose, length, follow));
            safe->emit(std::move(commands), "Stretch clip");
        };
    };

    juce::PopupMenu menu;
    int next = 1;
    const Items items{menu, next};
    menu.addSectionHeader(
        juce::String::fromUTF8((clip != nullptr ? clip->name : midiClip->name).c_str()) +
        (many ? "  +" + juce::String(static_cast<int>(selection.size()) - 1) : juce::String()));
    items.add("Split here",
              run([selection, splitHere](const core::Project& p)
                  { return edit::splitAt(p, selection, splitHere); }),
              splitHere > span->start && splitHere < span->end());
    items.add(
        many ? "Join" : "Join with next",
        run([selection, ref](const core::Project& p)
            { return selection.size() > 1 ? edit::joinClips(p, selection) : edit::joinWithNext(p, ref); }),
        many ? !edit::joinClips(*project, selection).empty() : edit::canJoinWithNext(*project, ref),
        false,
        many ? Shortcuts::keyText(Command::join) : juce::String());
    if (!ref.midi)
        items.add("Crossfade",
                  run([selection](const core::Project& p) { return edit::crossfadeClips(p, selection); }),
                  !edit::crossfadeClips(*project, selection).empty(),
                  false,
                  Shortcuts::keyText(Command::crossfade));
    items.add(many ? (anyPlaying ? "Mute clips" : "Unmute clips") : (muted ? "Unmute clip" : "Mute clip"),
              run([selection](const core::Project& p) { return edit::toggleMuted(p, selection); }),
              true,
              false,
              Shortcuts::keyText(Command::muteClips));
    items.add("Duplicate",
              run([selection](const core::Project& p) { return edit::duplicateClips(p, selection); }),
              true,
              false,
              Shortcuts::keyText(Command::duplicate));
    if (!ref.midi)
        items.add(
            "Normalise",
            [safe]
            {
                if (safe != nullptr)
                    safe->normaliseSelection();
            },
            static_cast<bool>(v.onNormalise),
            false,
            Shortcuts::keyText(Command::normalise));
    menu.addSeparator();
    items.add(
        "Cut",
        [safe]
        {
            if (safe != nullptr)
                safe->cutSelection();
        },
        true,
        false,
        Shortcuts::keyText(Command::cut));
    items.add(
        "Copy",
        [safe]
        {
            if (safe != nullptr)
                safe->copySelection();
        },
        true,
        false,
        Shortcuts::keyText(Command::copy));
    items.add(
        "Delete",
        [safe, selection]
        {
            if (safe != nullptr)
                safe->removeClips(selection);
        },
        true,
        false,
        Shortcuts::keyText(Command::deleteSelection));
    menu.addSeparator();
    if (clip != nullptr)
    {
        items.add(
            "Stretch to project tempo...",
            [safe, trackId, id = original.id]
            {
                if (safe != nullptr && safe->onConformClip)
                    safe->onConformClip(trackId, id);
            },
            static_cast<bool>(v.onConformClip));
        items.add("Follow tempo changes",
                  setStretch(original.stretch, original.transpose, original.length, !original.followTempo),
                  true,
                  clip->followTempo);
        juce::PopupMenu transpose;
        const Items semitones{transpose, next};
        for (const int semis : {12, 7, 5, 2, 1, 0, -1, -2, -5, -7, -12})
            semitones.add(semis == 0 ? juce::String("Original pitch")
                                     : (semis > 0 ? "+" : "") + juce::String(semis) + " semitones",
                          setStretch(original.stretch, semis, original.length, original.followTempo),
                          true,
                          std::abs(clip->transpose - semis) < 1.0e-6);
        menu.addSubMenu("Transpose", transpose);
        items.add("Reset stretch and pitch",
                  setStretch(1.0, 0.0, original.sourceLength(), false),
                  clip->isStretched());
        menu.addSeparator();
        items.add(
            "Open in Sampler",
            [safe, trackId, id = original.id]
            {
                if (safe != nullptr && safe->onOpenInSampler)
                    safe->onOpenInSampler(trackId, id);
            },
            static_cast<bool>(v.onOpenInSampler));
    }
    else
    {
        items.add(
            "Open in piano roll",
            [safe, trackId, id = ref.clip]
            {
                if (safe != nullptr && safe->onOpenMidiClip)
                    safe->onOpenMidiClip(trackId, id);
            },
            static_cast<bool>(v.onOpenMidiClip));
    }
    items.add("Track...",
              [safe, trackId, screen]
              {
                  if (safe != nullptr)
                      safe->menus_.trackMenu(trackId, screen);
              });
    menu.showMenuAsync(at(screen));
}

void ArrangeMenus::trackMenu(core::Id trackId, juce::Point<int> screen, core::Id clipId)
{
    auto& v = view_;
    const auto* project = v.project_;
    const auto* track = project != nullptr ? project->findTrack(trackId) : nullptr;
    if (track == nullptr)
        return;
    Safe safe(&v);
    // Runs a command on the track when the item is chosen.
    const auto command = [safe](std::function<std::unique_ptr<core::Command>()> make)
    {
        return [safe, make]
        {
            if (safe != nullptr && safe->onCommand)
                safe->onCommand(make());
        };
    };
    juce::PopupMenu menu;
    int next = 1;
    const Items items{menu, next};
    if (clipId != core::invalidId && track->findClip(clipId) != nullptr && v.onOpenInSampler)
    {
        menu.addSectionHeader("Clip");
        items.add("Open in Sampler",
                  [safe, trackId, clipId]
                  {
                      if (safe != nullptr && safe->onOpenInSampler)
                          safe->onOpenInSampler(trackId, clipId);
                  });
        menu.addSeparator();
        menu.addSectionHeader("Track");
    }
    items.add("Rename",
              [safe, trackId]
              {
                  if (safe != nullptr)
                      for (auto& header : safe->headers_)
                          if (header->trackId() == trackId)
                              header->startRename();
              });
    juce::PopupMenu colours;
    next = addColourItems(colours,
                          v.theme_,
                          v.trackColour(*track, project->trackIndex(trackId)),
                          next,
                          [safe, trackId](std::uint32_t argb)
                          {
                              if (safe != nullptr && safe->onCommand)
                                  safe->onCommand(std::make_unique<core::SetColour>(trackId, argb));
                          });
    menu.addSubMenu("Colour", colours);
    if (track->isMidi())
    {
        // MIDI output: the device chosen in Audio Setup, on a channel.
        const auto device = v.midiOutputName ? v.midiOutputName() : std::string{};
        juce::PopupMenu out;
        const Items outputs{out, next};
        if (device.empty())
            outputs.add("Choose a MIDI output in Audio Setup", {}, false);
        else
        {
            out.addSectionHeader(juce::String::fromUTF8(device.c_str()));
            outputs.add(
                "Off",
                command([trackId] { return std::make_unique<core::SetTrackMidiOutput>(trackId, false, 1); }),
                true,
                !track->midiOut);
            for (int ch = 1; ch <= 16; ++ch)
                outputs.add(
                    "Channel " + juce::String(ch),
                    command([trackId, ch]
                            { return std::make_unique<core::SetTrackMidiOutput>(trackId, true, ch); }),
                    true,
                    track->midiOut && track->midiOutChannel == ch);
        }
        menu.addSubMenu("MIDI output", out);
        items.add("Plays...",
                  [safe, trackId]
                  {
                      if (safe == nullptr)
                          return;
                      const auto mouse = juce::Desktop::getMousePosition();
                      safe->menus_.targetMenu(trackId, {mouse.x, mouse.y, 1, 1});
                  });
    }
    // Sends: tick a bus or FX track to send to it (post-fader, -6 dB), untick to stop.
    if (track->hasAudio())
    {
        juce::PopupMenu sends;
        const Items sendItems{sends, next};
        for (const auto& bus : project->buses)
        {
            const bool sending = track->findSend(bus.id) != nullptr;
            const auto busId = bus.id;
            sendItems.add(
                juce::String::fromUTF8(bus.name.c_str()) + (bus.fx ? "" : "  (bus)"),
                [safe, trackId, busId]
                {
                    const auto* t = safe != nullptr && safe->project_ != nullptr
                                        ? safe->project_->findTrack(trackId)
                                        : nullptr;
                    if (t == nullptr || !safe->onCommand)
                        return;
                    if (t->findSend(busId) != nullptr)
                        safe->onCommand(std::make_unique<core::RemoveSend>(trackId, busId));
                    else
                        safe->onCommand(std::make_unique<core::SetSend>(trackId, busId, 0.5f, false));
                },
                sending || !project->wouldCycle(trackId, bus.id),
                sending);
        }
        sends.addSeparator();
        juce::PopupMenu newFx;
        addFxItems(newFx, next, {trackId});
        sends.addSubMenu("New FX track", newFx);
        menu.addSubMenu("Send to", sends);
        items.add(track->automationOpen ? "Hide automation lanes" : "Show automation lanes",
                  [safe, trackId]
                  {
                      if (const auto* t = safe != nullptr && safe->project_ != nullptr
                                              ? safe->project_->findChannel(trackId)
                                              : nullptr)
                          safe->showAutomation(trackId, !t->automationOpen);
                  });
        items.add(track->isFrozen() ? "Unfreeze track" : "Freeze track",
                  [safe, trackId]
                  {
                      if (const auto* t = safe != nullptr && safe->project_ != nullptr
                                              ? safe->project_->findTrack(trackId)
                                              : nullptr;
                          t != nullptr && safe->onFreezeTrack)
                          safe->onFreezeTrack(trackId, !t->isFrozen());
                  });
    }
    if (track->isFolder())
        items.add(track->folderOpen ? "Close folder" : "Open folder",
                  [safe, trackId]
                  {
                      if (const auto* t = safe != nullptr && safe->project_ != nullptr
                                              ? safe->project_->findTrack(trackId)
                                              : nullptr;
                          t != nullptr && safe->onCommand)
                          safe->onCommand(std::make_unique<core::SetFolderOpen>(trackId, !t->folderOpen));
                  });
    {
        juce::PopupMenu folders;
        const Items folderItems{folders, next};
        folderItems.add("New folder containing this track",
                        command(
                            [trackId]
                            {
                                return std::make_unique<core::AddFolderTrack>("Folder",
                                                                              std::vector<core::Id>{trackId});
                            }));
        folders.addSeparator();
        folderItems.add(
            "Not in a folder",
            command([trackId] { return std::make_unique<core::SetTrackFolder>(trackId, core::invalidId); }),
            track->folder != core::invalidId,
            track->folder == core::invalidId);
        for (const auto* folder : v.folderChoices(trackId))
        {
            const auto folderId = folder->id;
            folderItems.add(juce::String::fromUTF8(folder->name.c_str()),
                            command([trackId, folderId]
                                    { return std::make_unique<core::SetTrackFolder>(trackId, folderId); }),
                            true,
                            track->folder == folderId);
        }
        menu.addSubMenu("Folder", folders);
    }
    if (!track->takeGroups.empty())
        items.add(v.takes_.laneCount(*track) > 0 ? "Hide take lanes" : "Show take lanes",
                  [safe, trackId]
                  {
                      if (const auto* t = safe != nullptr && safe->project_ != nullptr
                                              ? safe->project_->findTrack(trackId)
                                              : nullptr)
                          safe->takes_.toggleLanes(*t);
                  });
    menu.addSeparator();
    items.add("Delete track", command([trackId] { return std::make_unique<core::RemoveTrack>(trackId); }));
    items.add("Add bus",
              [safe]
              {
                  if (safe != nullptr && safe->onAddBus)
                      safe->onAddBus();
              });
    menu.showMenuAsync(at(screen));
}

void ArrangeMenus::targetMenu(core::Id trackId, juce::Rectangle<int> screenArea)
{
    auto& v = view_;
    const auto* track = v.project_ != nullptr ? v.project_->findTrack(trackId) : nullptr;
    if (track == nullptr || !track->isMidi())
        return;
    Safe safe(&v);
    // Aims the track at a channel (or nothing) and a MIDI channel when chosen.
    const auto target = [safe, trackId](std::function<std::pair<core::Id, int>(const core::Track&)> pick)
    {
        return [safe, trackId, pick]
        {
            const auto* t =
                safe != nullptr && safe->project_ != nullptr ? safe->project_->findTrack(trackId) : nullptr;
            if (t == nullptr || !safe->onCommand)
                return;
            const auto [channel, midiChannel] = pick(*t);
            safe->onCommand(std::make_unique<core::SetMidiTarget>(trackId, channel, midiChannel));
        };
    };
    juce::PopupMenu menu;
    int next = 1;
    const Items items{menu, next};
    menu.addSectionHeader("Plays");
    for (const auto& t : v.project_->tracks)
        if (t.isInstrument())
        {
            const auto channel = t.id;
            items.add(
                channelLabel(t, v.displayName_),
                target([channel](const core::Track& m) { return std::make_pair(channel, m.midiChannel); }),
                true,
                track->midiTarget == t.id);
        }
    items.add("Nothing",
              target([](const core::Track& m) { return std::make_pair(core::invalidId, m.midiChannel); }),
              true,
              track->midiTarget == core::invalidId);
    menu.addSeparator();
    juce::PopupMenu midiChannels;
    const Items channelItems{midiChannels, next};
    for (int ch = 1; ch <= 16; ++ch)
        channelItems.add("Channel " + juce::String(ch),
                         target([ch](const core::Track& m) { return std::make_pair(m.midiTarget, ch); }),
                         true,
                         track->midiChannel == ch);
    menu.addSubMenu("MIDI channel", midiChannels);
    auto instruments = buildPluginMenu(PluginKind::instrument, v.pluginSources_, pluginIds);
    menu.addSubMenu("New instrument", instruments.menu);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screenArea),
                       [safe, trackId, instruments](int choice)
                       {
                           if (safe == nullptr)
                               return;
                           if (const auto type = instruments.typeFor(choice);
                               !type.empty() && safe->onPlayNewInstrument)
                               safe->onPlayNewInstrument(trackId, type);
                       });
}

void ArrangeMenus::rulerMenu(juce::Point<int> screen)
{
    auto& v = view_;
    Safe safe(&v);
    const auto act = [safe](std::function<void(ArrangeView&)> f)
    {
        return [safe, f]
        {
            if (safe != nullptr)
                f(*safe);
        };
    };
    juce::PopupMenu menu;
    int next = 1;
    const Items items{menu, next};
    items.add("Follow playhead",
              act([](ArrangeView& a) { a.followPlayhead_ = !a.followPlayhead_; }),
              true,
              v.followPlayhead_);
    items.add("Zoom to fit", act([](ArrangeView& a) { a.zoomToFit(); }));
    menu.addSeparator();
    items.add("Add marker at playhead", act([](ArrangeView& a) { a.addMarkerAtPlayhead(false); }));
    items.add("Add region from loop",
              act([](ArrangeView& a) { a.addMarkerAtPlayhead(true); }),
              v.regionEnd_ > v.regionStart_);
    menu.addSeparator();
    items.add("Show tempo track",
              act([](ArrangeView& a) { a.setTempoTrackShown(!a.tempoShown_); }),
              true,
              v.tempoShown_);
    menu.showMenuAsync(at(screen));
}

void ArrangeMenus::addTrackMenu()
{
    auto& v = view_;
    Safe safe(&v);
    juce::PopupMenu menu;
    int next = 1;
    const Items items{menu, next};
    items.add("Audio track",
              [safe]
              {
                  if (safe != nullptr && safe->onAddAudioTrack)
                      safe->onAddAudioTrack();
              });
    // A MIDI track plays an instrument channel: a new one, one already here, or
    // nothing yet.
    juce::PopupMenu midi;
    const Items midiItems{midi, next};
    auto instruments = buildPluginMenu(PluginKind::instrument, v.pluginSources_, pluginIds);
    midi.addSubMenu("With a new instrument", instruments.menu);
    bool playing = false;
    if (v.project_ != nullptr)
        for (const auto& t : v.project_->tracks)
            if (t.isInstrument())
            {
                if (!std::exchange(playing, true))
                    midi.addSectionHeader("Playing");
                const auto channel = t.id;
                midiItems.add(channelLabel(t, v.displayName_),
                              [safe, channel]
                              {
                                  if (safe != nullptr && safe->onAddMidiTrackPlaying)
                                      safe->onAddMidiTrackPlaying(channel);
                              });
            }
    midi.addSeparator();
    midiItems.add("Playing nothing yet",
                  [safe]
                  {
                      if (safe != nullptr && safe->onAddMidiTrackPlaying)
                          safe->onAddMidiTrackPlaying(core::invalidId);
                  });
    menu.addSubMenu("MIDI track", midi);
    juce::PopupMenu fx;
    addFxItems(fx, next, {});
    menu.addSubMenu("FX track", fx);
    items.add("Bus",
              [safe]
              {
                  if (safe != nullptr && safe->onAddBus)
                      safe->onAddBus();
              });
    items.add(
        v.selectedTrack() != core::invalidId ? "Folder track (holding the selected track)" : "Folder track",
        [safe]
        {
            if (safe == nullptr || !safe->onCommand)
                return;
            std::vector<core::Id> members;
            if (safe->project_ != nullptr && safe->project_->findTrack(safe->selectedTrack()) != nullptr)
                members.push_back(safe->selectedTrack());
            safe->onCommand(std::make_unique<core::AddFolderTrack>("Folder", members));
        });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&v.addTrack_),
                       [safe, instruments](int choice)
                       {
                           if (safe == nullptr)
                               return;
                           if (const auto type = instruments.typeFor(choice);
                               !type.empty() && safe->onAddMidiTrack)
                               safe->onAddMidiTrack(type);
                       });
}

void ArrangeMenus::busMenu(core::Id busId, juce::Point<int> screen)
{
    auto& v = view_;
    const auto* bus = v.project_ != nullptr ? v.project_->findBus(busId) : nullptr;
    if (bus == nullptr)
        return;
    Safe safe(&v);
    juce::PopupMenu menu;
    int next = 1;
    const Items items{menu, next};
    items.add("Rename",
              [safe, busId]
              {
                  if (safe != nullptr)
                      for (auto& header : safe->fxHeaders_)
                          if (header->trackId() == busId)
                              header->startRename();
              });
    items.add(bus->automationOpen ? "Hide automation lanes" : "Show automation lanes",
              [safe, busId]
              {
                  if (const auto* b = safe != nullptr && safe->project_ != nullptr
                                          ? safe->project_->findBus(busId)
                                          : nullptr)
                      safe->showAutomation(busId, !b->automationOpen);
              });
    menu.addSeparator();
    items.add(bus->fx ? "Remove FX track" : "Remove bus",
              [safe, busId]
              {
                  if (safe != nullptr && safe->onCommand)
                      safe->onCommand(std::make_unique<core::RemoveBus>(busId));
              });
    menu.showMenuAsync(at(screen));
}

void ArrangeMenus::masterMenu(juce::Point<int> screen)
{
    auto& v = view_;
    if (v.project_ == nullptr)
        return;
    Safe safe(&v);
    juce::PopupMenu menu;
    int next = 1;
    const Items items{menu, next};
    menu.addSectionHeader("Master");
    items.add(v.project_->master.automationOpen ? "Hide automation lanes" : "Show automation lanes",
              [safe]
              {
                  if (safe != nullptr && safe->project_ != nullptr)
                      safe->showAutomation(core::masterId, !safe->project_->master.automationOpen);
              });
    menu.showMenuAsync(at(screen));
}

void ArrangeMenus::toolsMenu()
{
    auto& v = view_;
    static constexpr const char* labels[ArrangeView::toolCount] = {
        "Select", "Range", "Pencil", "Erase", "Split", "Glue", "Mute"};
    Safe safe(&v);
    juce::PopupMenu menu;
    const auto tools = v.toolButtons();
    for (int i = 0; i < ArrangeView::toolCount; ++i)
    {
        const auto tool = static_cast<ArrangeView::Tool>(i);
        juce::PopupMenu::Item item(labels[i]);
        item.itemID = i + 1;
        item.isTicked = tool == v.tool_;
        item.shortcutKeyDescription = juce::String(i + 1);
        bool filled = false;
        auto icon = std::make_unique<juce::DrawablePath>();
        icon->setPath(Icons::path(tools[static_cast<size_t>(i)]->icon(), filled));
        icon->setStrokeFill(juce::FillType(v.theme_.colours.textSecondary));
        icon->setStrokeType(
            juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        icon->setFill(filled ? juce::FillType(v.theme_.colours.textSecondary) : juce::FillType());
        item.image = std::move(icon);
        item.action = [safe, tool]
        {
            if (safe != nullptr)
                safe->setTool(tool);
        };
        menu.addItem(std::move(item));
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&v.toolMenu_));
}

} // namespace northlight::ui
