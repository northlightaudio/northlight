#pragma once

#include "plugins/EffectPresets.h"

#include <vector>

// Each built-in's factory preset list (the instruments' are built by index in
// their own files, as Patina's is), built on first use by
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
std::vector<EffectPreset> auroraPresets();
std::vector<EffectPreset> nebulaPresets();
std::vector<EffectPreset> starbitPresets();
std::vector<EffectPreset> lumenPresets();
std::vector<EffectPreset> cirrusPresets();

} // namespace northlight::plugins::presetlists
