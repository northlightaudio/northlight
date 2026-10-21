#pragma once

#include "ui/strip/ModuleCard.h"

#include <memory>

namespace aural::ui::strip
{

// The Sampler's editor (07-plugins.md 7.4f): the sample's waveform with its
// start, end and loop points to drag, the key range and root on a keyboard,
// and the pitch, playback, envelope and filter controls. Dropping an audio
// file on it (from Explorer or the browser's Files tab) loads it.
std::unique_ptr<ModuleCard>
createSamplerEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

} // namespace aural::ui::strip
