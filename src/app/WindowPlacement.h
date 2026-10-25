#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

// Where the main window was last session (docs/design/screens/shell.md): kept as
// JUCE's window state string ("x y w h", "fs x y w h" when maximised, the bounds
// then being the ones it restores to), and checked against today's screens before
// it is used, so a window saved on a monitor that has gone opens centred instead.

namespace northlight::app
{

struct WindowPlacement
{
    juce::Rectangle<int> bounds; // the normal (not maximised) bounds
    bool maximised = false;
};

// Reads a state string from juce::ResizableWindow::getWindowStateAsString; nothing
// when it is not one or has no size.
inline std::optional<WindowPlacement> parsePlacement(const juce::String& state)
{
    juce::StringArray tokens;
    tokens.addTokens(state.trim(), " ", "");
    tokens.removeEmptyStrings();
    WindowPlacement placement;
    if (!tokens.isEmpty() && tokens[0] == "fs")
    {
        placement.maximised = true;
        tokens.remove(0);
    }
    if (tokens.size() != 4)
        return std::nullopt;
    for (const auto& t : tokens)
        if (!t.containsOnly("-0123456789"))
            return std::nullopt;
    placement.bounds = {
        tokens[0].getIntValue(), tokens[1].getIntValue(), tokens[2].getIntValue(), tokens[3].getIntValue()};
    if (placement.bounds.getWidth() <= 0 || placement.bounds.getHeight() <= 0)
        return std::nullopt;
    return placement;
}

// Whether a window at `bounds` can still be grabbed: its top strip (where the
// header bar is) overlaps one of the screens' work areas by at least
// `minimumGrab` (wide) by `strip` (tall).
inline bool reachable(juce::Rectangle<int> bounds,
                      const std::vector<juce::Rectangle<int>>& workAreas,
                      int strip = 32,
                      int minimumGrab = 120)
{
    const auto top = bounds.withHeight(std::min(strip, bounds.getHeight()));
    for (const auto& area : workAreas)
    {
        const auto overlap = top.getIntersection(area);
        if (overlap.getWidth() >= std::min(minimumGrab, top.getWidth()) &&
            overlap.getHeight() >= top.getHeight())
            return true;
    }
    return false;
}

} // namespace northlight::app
