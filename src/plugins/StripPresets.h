#pragma once

#include "core/Model.h"

#include <string>
#include <vector>

// Factory presets for the channel strip (12-channel-strip.md 12.10): starting
// points for common sources. A preset is every module's on/off state and
// parameters (modules it does not use are off at their defaults); applying one
// keeps the channel's module order and ids. JUCE-free.

namespace aural::plugins::strip
{

struct Preset
{
    std::string name;
    std::string category; // "Vocals", "Instruments", "Drums", "Buses"
    std::string description;
    std::vector<core::Insert> modules; // one per module type, no ids
};

const std::vector<Preset>& factoryPresets();
const Preset* findFactoryPreset(const std::string& name);

} // namespace aural::plugins::strip
