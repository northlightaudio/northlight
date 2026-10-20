#pragma once

#include "core/Model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace aural::ui
{

// The buses and FX tracks a channel can send to, as a menu (the mixer's and the
// Editor panel's "+ Send"): one that would feed the channel back into itself is
// left out, one it already sends to is ticked and disabled, a group bus is
// marked "(group)". Item `firstItem + i` picks targets[i].
struct SendMenu
{
    juce::PopupMenu menu;
    std::vector<core::Id> targets;
    int firstItem = 1;
    core::Id busFor(int item) const noexcept
    {
        const int i = item - firstItem;
        return i >= 0 && static_cast<size_t>(i) < targets.size() ? targets[static_cast<size_t>(i)]
                                                                 : core::invalidId;
    }
};

inline SendMenu buildSendMenu(const core::Project& project, core::Id channelId, int firstItem = 1)
{
    SendMenu result;
    result.firstItem = firstItem;
    const auto* channel = project.findChannel(channelId);
    if (channel != nullptr)
        for (const auto& bus : project.buses)
        {
            if (bus.id == channelId || project.wouldCycle(channelId, bus.id))
                continue;
            const bool has = channel->findSend(bus.id) != nullptr;
            result.menu.addItem(firstItem + static_cast<int>(result.targets.size()),
                                juce::String::fromUTF8(bus.name.empty() ? "Bus" : bus.name.c_str()) +
                                    (bus.fx ? "" : "  (group)"),
                                !has,
                                has);
            result.targets.push_back(bus.id);
        }
    if (result.targets.empty())
        result.menu.addItem(-1, "No FX tracks yet (add one from the track list)", false); // disabled
    return result;
}

} // namespace aural::ui
