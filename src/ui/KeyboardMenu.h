#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace northlight::ui
{

// A view whose right-click menu also opens from the keyboard: Shift+F10 or the
// Menu key (interaction.md 10.4). It opens the menu a right-click on the focused
// part would, at that part (the selected clip or note, the selected track).
class KeyboardMenu
{
public:
    virtual ~KeyboardMenu() = default;
    // False when there is nothing here to open a menu for.
    virtual bool showKeyboardMenu() = 0;

    // Opens the menu of `focused` or the nearest of its parents that has one;
    // when none does, `fallback`'s (the arrange view, which owns the focus by
    // default). False when no menu opened.
    static bool showFor(juce::Component* focused, KeyboardMenu* fallback);
    // Whether the Menu key is down now. Windows sends it as a key state change,
    // never as a key press, so the window asks on every key state change.
    static bool menuKeyDown();
};

} // namespace northlight::ui
