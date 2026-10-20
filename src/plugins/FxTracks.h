#pragma once

#include "core/Command.h"
#include "core/Model.h"

#include <memory>
#include <string>
#include <vector>

// FX tracks: buses flagged `fx` that other channels feed through aux sends
// (core::Bus). This makes new ones: an effect as the first insert, fully wet
// (the dry signal stays on the sending channels), named after the effect.

namespace aural::plugins
{

struct FxChoice
{
    std::string type; // an insert type; empty = no insert
    std::string name;
};
// The effects an "Add FX track" menu offers first (the built-ins).
std::vector<FxChoice> fxTrackEffects();

// A new FX track (its id taken from the project's next id, so later commands
// in the same group can refer to it).
core::Bus
makeFxTrack(const core::Project& project, const std::string& effectType, const std::string& effectName);

// The commands that add one and, when `sendFrom` names channels, a send from
// each into it (post-fader, at -6 dB). One undo step when executed as a group.
std::vector<std::unique_ptr<core::Command>> addFxTrackCommands(const core::Project& project,
                                                               const std::string& effectType,
                                                               const std::string& effectName,
                                                               const std::vector<core::Id>& sendFrom = {});

} // namespace aural::plugins
