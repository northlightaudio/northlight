#pragma once

#include "ui/strip/ModuleCard.h"

#include <memory>

namespace northlight::ui::strip
{

// Cirrus's editor (screens/plugins.md 7.4j): the FM synth's own face, the
// sidebands of the selected operator over a table of all four, with its routing,
// pitch and LFO, and tone.
std::unique_ptr<ModuleCard>
createCirrusEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

} // namespace northlight::ui::strip
