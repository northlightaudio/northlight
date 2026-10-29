#pragma once

#include "ui/strip/ModuleCard.h"

#include <memory>

namespace northlight::ui::strip
{

// Lumen's editor (screens/plugins.md 7.4h): the electric piano's own face, a dark
// room lit from below by a lamp, with glass panels for the tine and its pickup,
// the hammer, the preamp's tone, the tremolo, the phaser and the chorus.
std::unique_ptr<ModuleCard>
createLumenEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

} // namespace northlight::ui::strip
