#include "hosting/ClapEditor.h"

#if JUCE_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace northlight::hosting
{

ClapEditor::ClapEditor(hosting::HostedClapPlugin& plugin) : plugin_(plugin)
{
    setOpaque(true);
#if JUCE_WINDOWS
    addAndMakeVisible(host_);
#endif
    setSize(400, 300);
}

ClapEditor::~ClapEditor()
{
    stopTimer();
    plugin_.closeEditor();
#if JUCE_WINDOWS
    host_.setHWND(nullptr);
    if (child_ != nullptr)
        DestroyWindow(static_cast<HWND>(child_));
#endif
    child_ = nullptr;
}

void ClapEditor::parentHierarchyChanged()
{
    if (!opened_ && getPeer() != nullptr)
        open();
}

void ClapEditor::open()
{
#if JUCE_WINDOWS
    auto* peer = getPeer();
    if (peer == nullptr || opened_)
        return;
    auto* parent = static_cast<HWND>(peer->getNativeHandle());
    // A plain child window JUCE positions over this component; the plugin's GUI
    // becomes its child.
    child_ = CreateWindowExW(0,
                             L"STATIC",
                             L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                             0,
                             0,
                             std::max(1, getWidth()),
                             std::max(1, getHeight()),
                             parent,
                             nullptr,
                             GetModuleHandleW(nullptr),
                             nullptr);
    if (child_ == nullptr)
        return;
    host_.setHWND(child_);
    int w = 0, h = 0;
    const double scale = peer->getPlatformScaleFactor();
    if (plugin_.openEditor(child_, scale, w, h))
    {
        opened_ = true;
        setSize(w, h);
        if (events.resized)
            events.resized(w, h);
        if (events.resizableKnown)
            events.resizableKnown(plugin_.editorIsResizable());
        startTimerHz(20);
    }
#endif
}

void ClapEditor::resized()
{
#if JUCE_WINDOWS
    host_.setBounds(getLocalBounds());
#endif
    if (opened_)
        plugin_.editorResized(getWidth(), getHeight());
}

void ClapEditor::timerCallback()
{
    if (opened_ && !plugin_.isEditorOpen())
    {
        // Closed from the plugin's side (host.gui.closed): tell the window once.
        opened_ = false;
        stopTimer();
        if (events.closed)
            events.closed();
        return;
    }
    int w = 0, h = 0;
    if (plugin_.takeResizeRequest(w, h) && w > 0 && h > 0)
    {
        setSize(w, h);
        if (events.resized)
            events.resized(w, h);
    }
}

} // namespace northlight::hosting
