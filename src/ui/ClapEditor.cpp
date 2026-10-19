#include "ui/ClapEditor.h"

#if JUCE_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace aural::ui
{

ClapEditor::ClapEditor(hosting::HostedClapPlugin& plugin) : plugin_(plugin)
{
    setOpaque(true);
    addAndMakeVisible(host_);
    setSize(400, 300);
}

ClapEditor::~ClapEditor()
{
    stopTimer();
    plugin_.closeEditor();
    host_.setHWND(nullptr);
#if JUCE_WINDOWS
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
        if (onPluginResized)
            onPluginResized(w, h);
        startTimerHz(20);
    }
#endif
}

void ClapEditor::resized()
{
    host_.setBounds(getLocalBounds());
    if (opened_)
        plugin_.editorResized(getWidth(), getHeight());
}

void ClapEditor::timerCallback()
{
    int w = 0, h = 0;
    if (plugin_.takeResizeRequest(w, h) && w > 0 && h > 0)
    {
        setSize(w, h);
        if (onPluginResized)
            onPluginResized(w, h);
    }
}

} // namespace aural::ui
