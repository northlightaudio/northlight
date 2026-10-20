#pragma once

#include "plugins/StripPresets.h"

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

// The user's own channel strip presets: one JSON file each in
// Documents/Aural/Strip Presets (name, then each module's type, on/off and
// parameters). A preset saved by an older version simply lacks the modules
// added since; applying it leaves those alone.

namespace aural::ui::strip
{

juce::File userPresetFolder();
std::vector<plugins::strip::Preset> loadUserPresets(const juce::File& folder);
// Writes (or replaces) the preset; returns an error or empty.
std::string
saveUserPreset(const juce::File& folder, const juce::String& name, const std::vector<core::Insert>& modules);

} // namespace aural::ui::strip
