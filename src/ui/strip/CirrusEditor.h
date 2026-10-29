#pragma once

#include "ui/strip/ModuleCard.h"

#include <memory>

namespace northlight::ui::strip
{

// Cirrus's editor (screens/plugins.md 7.4h): the FM synth's own face, an evening
// sky over its algorithm, operators, the selected operator, tone, and pitch and
// LFO.
std::unique_ptr<ModuleCard>
createCirrusEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

} // namespace northlight::ui::strip
