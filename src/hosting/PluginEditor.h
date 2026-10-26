#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace northlight::hosting
{

// What a GUI that sizes and closes itself reports to the window holding it (a
// CLAP GUI: its requests arrive on a timer). Message thread.
struct EditorEvents
{
    std::function<void(int width, int height)> resized; // the plugin resized its GUI
    std::function<void(bool canResize)> resizableKnown; // known once the GUI is open
    std::function<void()> closed;                       // the plugin closed its own GUI
};

// A hosted plugin's editor, whatever its format (HostFactory::createEditor): the
// VST3's own editor (or JUCE's generic one), or a CLAP GUI embedded in a native
// child window. Null `component` when the plugin has no GUI of its own (the
// window then shows the generic parameter editor).
struct PluginEditor
{
    std::unique_ptr<juce::Component> component;
    bool resizable = false;
    // Set for a GUI that reports its own resizes and closing; owned by `component`.
    EditorEvents* events = nullptr;
};

} // namespace northlight::hosting
