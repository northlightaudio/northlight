#pragma once

#include "plugins/EffectPresets.h"

#include <vector>

// Each built-in effect's factory preset list, built on first use by
// factoryPresets() (plugins/EffectPresets.h) through the built-ins' registry
// (builtInTypes() in plugins/BuiltIns.h). Not real-time.

namespace northlight::plugins::presetlists
{

std::vector<EffectPreset> gainPresets();
std::vector<EffectPreset> eqPresets();
std::vector<EffectPreset> compressorPresets();
std::vector<EffectPreset> optoPresets();
std::vector<EffectPreset> gluePresets();
std::vector<EffectPreset> widenerPresets();
std::vector<EffectPreset> patinaPresets();
std::vector<EffectPreset> exciterPresets();
std::vector<EffectPreset> orbitPresets();
std::vector<EffectPreset> limiterPresets();
std::vector<EffectPreset> reverbPresets();
std::vector<EffectPreset> delayPresets();
std::vector<EffectPreset> tapeEchoPresets();
std::vector<EffectPreset> bloomPresets();
std::vector<EffectPreset> vesperPresets();
std::vector<EffectPreset> ampPresets();
std::vector<EffectPreset> gravityPresets();
std::vector<EffectPreset> flarePresets();
std::vector<EffectPreset> cometPresets();
std::vector<EffectPreset> tidePresets();
std::vector<EffectPreset> polarisPresets();

} // namespace northlight::plugins::presetlists
