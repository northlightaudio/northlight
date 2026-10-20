#pragma once

#include "ui/strip/ModuleCard.h"

#include <memory>

namespace aural::ui::strip
{

// Pulsar's editor (07-plugins.md 7.4c): the drum synth's own face, a pulsing
// star whose beams flash on every hit, eight pads that light as they play,
// the selected pad's sound drawn large to drag, its knobs, and the master.
std::unique_ptr<ModuleCard>
createPulsarEditor(const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert);

} // namespace aural::ui::strip
