#pragma once

#include "ui/strip/ModuleCard.h"

#include <memory>

namespace northlight::ui::strip
{

// Nebula's editor (screens/plugins.md 7.4d): the pad synth's own face, drifting gas
// clouds and dust behind its spectrum, cloud, envelope, motion and space.
std::unique_ptr<ModuleCard>
createNebulaEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

} // namespace northlight::ui::strip
