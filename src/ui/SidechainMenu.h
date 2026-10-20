#pragma once

#include "core/Command.h"
#include "core/Model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace aural::ui
{

// The key (sidechain) sources an insert can listen to, as menu items (the
// mixer's insert menu and the compressor's Sidechain button): None, every
// track, and on a bus every other bus; a source that would form a loop is
// shown greyed. Then the tap, Pre-fader or Post-fader, which a new source
// keeps. Item ids follow `actions`: item `actions.size() + 1` runs the action
// pushed with it.
inline void addSidechainItems(juce::PopupMenu& menu,
                              std::vector<std::function<void()>>& actions,
                              const core::Project& project,
                              core::Id channelId,
                              const core::Insert& insert,
                              const std::function<void(std::unique_ptr<core::Command>)>& onCommand)
{
    const core::Id insertId = insert.id;
    const auto add = [&](const juce::String& text, bool enabled, bool ticked, core::Id source, bool post)
    {
        menu.addItem(static_cast<int>(actions.size()) + 1, text, enabled, ticked);
        actions.push_back(
            [onCommand, channelId, insertId, source, post]
            {
                if (onCommand)
                    onCommand(std::make_unique<core::SetInsertSidechain>(channelId, insertId, source, post));
            });
    };
    add("None", true, insert.sidechain == core::invalidId, core::invalidId, false);
    const auto offer = [&](const core::Channel& source, const juce::String& prefix)
    {
        if (source.id == channelId)
            return;
        add(prefix + juce::String::fromUTF8(source.name.c_str()),
            !project.keyWouldCycle(channelId, source.id),
            insert.sidechain == source.id,
            source.id,
            insert.sidechainPostFader);
    };
    for (const auto& t : project.tracks)
        offer(t, "");
    if (project.findBus(channelId) != nullptr)
        for (const auto& b : project.buses)
            offer(b, "Bus: ");
    // The tap: before the source's fader (the key ignores it) or after it (the
    // key follows the fader and its automation).
    menu.addSeparator();
    const bool keyed = insert.sidechain != core::invalidId;
    for (const bool post : {false, true})
        add(post ? "Post-fader" : "Pre-fader",
            keyed,
            keyed && insert.sidechainPostFader == post,
            insert.sidechain,
            post);
}

// The key source's name, with "(post)" for a post-fader tap; empty without a key.
inline juce::String sidechainLabel(const core::Project& project, const core::Insert& insert)
{
    if (insert.sidechain == core::invalidId)
        return {};
    const auto* source = project.findChannel(insert.sidechain);
    return (source != nullptr ? juce::String::fromUTF8(source->name.c_str()) : juce::String("?")) +
           (insert.sidechainPostFader ? " (post)" : "");
}

} // namespace aural::ui
