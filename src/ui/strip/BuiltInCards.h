#pragma once

#include "ui/strip/ModuleCard.h"

#include <memory>

// The built-in processors' inline editors (07-plugins.md 7.1): each is a module
// card (the channel strip's machinery: live feedback, displays to drag, knobs
// bound to the parameters) embedded as the body of the insert's card in the
// Editor panel.

namespace aural::ui::strip
{

// The editor for a built-in type, or null for a type without one.
std::unique_ptr<ModuleCard>
createBuiltInCard(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

} // namespace aural::ui::strip
