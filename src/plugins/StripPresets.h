#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"

#include <memory>
#include <string>
#include <vector>

// Factory presets for the channel strip (screens/channel-strip.md 12.10): starting
// points for common sources. A preset is every module's on/off state and
// parameters (modules it does not use are off at their defaults); applying one
// keeps the channel's module order and ids. JUCE-free.

namespace northlight::plugins::strip
{

struct Preset
{
    std::string name;
    std::string category; // "Vocals", "Instruments", "Drums", "Buses"
    std::string description;
    std::vector<core::Insert> modules; // one per slot (in the model it uses), no ids
};

const std::vector<Preset>& factoryPresets();
const Preset* findFactoryPreset(const std::string& name);

// The commands that set a channel's strip to `modules` (a preset): a slot
// running another model than the preset names swaps to it first, then every
// module takes the preset's state. Run them as one group.
std::vector<std::unique_ptr<core::Command>> presetCommands(const core::Channel& channel,
                                                           const std::vector<core::Insert>& modules,
                                                           const std::string& name);

} // namespace northlight::plugins::strip
