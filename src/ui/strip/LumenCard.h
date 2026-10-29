#pragma once

#include "ui/strip/ModuleCard.h"

#include <memory>

namespace northlight::ui::strip
{

// Lumen's card (screens/plugins.md 7.4h): the electric piano's face, the front of
// a 1970s suitcase piano drawn from rendered parts (assets/lumen): the harp window
// with its tines, the nameplate and service trims on the tolex case, and the
// preamp plate's knobs, vibrato lever and lamps.
std::unique_ptr<ModuleCard>
createLumenCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

} // namespace northlight::ui::strip
