#include "ui/KeyboardMenu.h"

#if JUCE_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace northlight::ui
{

bool KeyboardMenu::showFor(juce::Component* focused, KeyboardMenu* fallback)
{
    for (auto* c = focused; c != nullptr; c = c->getParentComponent())
        if (auto* target = dynamic_cast<KeyboardMenu*>(c); target != nullptr && target->showKeyboardMenu())
            return true;
    return fallback != nullptr && fallback->showKeyboardMenu();
}

bool KeyboardMenu::menuKeyDown()
{
#if JUCE_WINDOWS
    return (GetKeyState(VK_APPS) & 0x8000) != 0;
#else
    return false;
#endif
}

} // namespace northlight::ui
