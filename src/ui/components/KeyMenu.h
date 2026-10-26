#pragma once

#include "core/Model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

// The project key's menu (screens/pitch-editing.md "Correct to key", screens/piano-roll.md
// toolbar): a submenu per scale with its twelve roots, the current key ticked, "No key",
// and optionally "Detect from this vocal". The piano roll's key button, the audio
// editor's and the Correct sheet share it, so they read and set the key the same way.

namespace northlight::ui::keymenu
{

constexpr int detectItem = 1;
constexpr int noneItem = 2;

juce::PopupMenu build(core::MusicalKey current, bool offerDetect, bool offerNone = true);
// The key a menu result picks: nullopt for no choice (0) or the detect item.
std::optional<core::MusicalKey> keyFor(int choice);
// "C minor", or "No key".
juce::String name(core::MusicalKey key);

} // namespace northlight::ui::keymenu
