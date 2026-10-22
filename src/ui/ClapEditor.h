#pragma once

#include "hosting/ClapPlugin.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace northlight::ui
{

// Hosts a CLAP plugin's own GUI inside a JUCE component. The plugin wants a native
// parent window: this component owns a plain child HWND of the JUCE window that
// JUCE keeps positioned over the component's bounds, and hands that to the plugin,
// so the GUI sits exactly where the component is (under the window's bar). The
// plugin's resize requests are picked up on a timer and reported through
// onPluginResized so the window can follow.
//
// Only Windows is supported so far: elsewhere HostedClapPlugin reports no GUI (it
// asks for the win32 window API), so this component is never opened.
class ClapEditor final : public juce::Component, private juce::Timer
{
public:
    explicit ClapEditor(hosting::HostedClapPlugin& plugin);
    ~ClapEditor() override;

    std::function<void(int width, int height)> onPluginResized;
    // The plugin's GUI reported whether it can be resized (known once it is open).
    std::function<void(bool)> onResizableKnown;
    // The plugin closed its own window: the host window should go too.
    std::function<void()> onPluginClosed;
    bool isResizable() const noexcept { return plugin_.editorIsResizable(); }

    void parentHierarchyChanged() override;
    void resized() override;

private:
    void timerCallback() override;
    void open();

    hosting::HostedClapPlugin& plugin_;
#if JUCE_WINDOWS
    juce::HWNDComponent host_;
#endif
    void* child_ = nullptr; // the HWND the plugin's GUI is parented to
    bool opened_ = false;
};

} // namespace northlight::ui
