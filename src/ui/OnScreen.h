#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// --offscreen: Northlight runs with no window on screen at all, for unattended
// checks (--capture) while someone is using the machine. Windows are still made,
// laid out, timed and painted, but none gets a native window, so nothing can
// appear over what they are doing, take the focus or switch their virtual
// desktop. Every top-level window is constructed off the desktop then (JUCE's
// addToDesktop argument): one made on it takes the foreground as it is created,
// hidden or not, and setUsingNativeTitleBar recreates it and brings it to the
// front. The checks that would normally ask whether a component is on screen ask
// onScreen() instead.

namespace northlight::ui
{

inline bool& offscreenMode() noexcept
{
    static bool on = false;
    return on;
}

// Whether a component counts as on screen: showing, or, off-screen, visible
// itself and in every parent up to its (never shown) window.
inline bool onScreen(const juce::Component& component)
{
    if (component.isShowing())
        return true;
    if (!offscreenMode())
        return false;
    for (const auto* c = &component; c != nullptr; c = c->getParentComponent())
        if (!c->isVisible())
            return false;
    return true;
}

// Shows a top-level window: normally as setVisible(true) does; off-screen it is
// taken off the desktop first, so it is "visible" with no native window behind it.
inline void showWindow(juce::Component& window)
{
    if (offscreenMode() && window.isOnDesktop())
        window.removeFromDesktop();
    window.setVisible(true);
}

// Brings a window to the front and focuses it; off-screen nothing does.
inline void bringToFront(juce::Component& window)
{
    if (!offscreenMode())
        window.toFront(true);
}

} // namespace northlight::ui
