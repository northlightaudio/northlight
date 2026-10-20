#pragma once

#include "ui/strip/ModuleCard.h"

#include <memory>

namespace aural::ui::strip
{

// Aurora's editor (07-plugins.md 7.5): the bundled synth's own face, a night
// sky with the aurora in it over glass panels for the oscillators, unison,
// filter, envelopes, LFO and output, each with a display to drag.
std::unique_ptr<ModuleCard>
createAuroraEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

} // namespace aural::ui::strip
