#pragma once

#include "ui/strip/ModuleCard.h"

#include <memory>

namespace northlight::ui::strip
{

// Starbit's editor (screens/plugins.md 7.4e): the chiptune synth's own face, pixel
// art from the wordmark to the displays, with a wave you draw cell by cell.
std::unique_ptr<ModuleCard>
createStarbitEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

} // namespace northlight::ui::strip
