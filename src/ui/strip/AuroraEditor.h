#pragma once

#include "ui/strip/ModuleCard.h"

#include <memory>

namespace northlight::ui::strip
{

// Aurora's editor (screens/plugins.md 7.4b): the bundled synth's own face, a night
// sky with the aurora in it over glass panels for the oscillators, unison,
// filter, envelopes, LFO and output, each with a display to drag.
std::unique_ptr<ModuleCard>
createAuroraEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

} // namespace northlight::ui::strip
