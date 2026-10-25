#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>

namespace northlight::ui
{

// The track colour menu, as everywhere offers it (the header stripe's swatch
// popup, the track menu's and the mixer strip's Colour): each palette colour by
// name with its swatch, the current one ticked, then Default. The items take ids
// from `firstId` up and carry their own action, so a caller's result callback can
// ignore them; choosing one calls `choose` with its colour (0: the default).
// Returns the next free id.
int addColourItems(juce::PopupMenu& menu,
                   const Theme& theme,
                   juce::Colour current,
                   int firstId,
                   std::function<void(std::uint32_t argb)> choose);

} // namespace northlight::ui
