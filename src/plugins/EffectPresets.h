#pragma once

#include <string>
#include <vector>

// The factory presets of the built-in effects (screens/plugins.md 7.2b): for each
// effect type a list of named settings, each a full set of parameter values in
// the type's parameter order. The first of a list is the effect's defaults. The
// instruments keep their own lists (aurora::presets() and the like). JUCE-free.

namespace northlight::plugins
{

struct EffectPreset
{
    std::string name;
    std::vector<float> values;
};

// The factory presets of a built-in effect (empty for a type with none).
const std::vector<EffectPreset>& factoryPresets(const std::string& type);

// The types that have factory presets: every built-in effect.
std::vector<std::string> typesWithPresets();

// Problems found while building the lists (a parameter or a choice named
// wrongly): empty when every preset resolved. For the tests.
const std::vector<std::string>& factoryPresetErrors();

} // namespace northlight::plugins
