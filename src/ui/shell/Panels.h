#pragma once

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace northlight::ui
{

// The panel model of docs/design/screens/shell.md: a fixed shell with detachable
// panels. Every secondary view is a panel one of three hosts owns at a time (the
// dock at the bottom, the side panel at the right, or a floating window); the
// user moves a panel between hosts by button, by dragging a tab out or a window
// header in, or with Shift+F-key, and never loses one (closing a floating panel
// returns it to its default host, hidden). The PanelManager owns the layout and
// its persistence (layout.json beside the settings) and two presets.

enum class PanelId
{
    mixer,
    editor,
    pianoRoll,
    audioEditor,
    diagnostics,
    browser,
    inspector,
    instruments,
};

enum class HostKind
{
    dock,
    side,
    floating,
};

struct PanelInfo
{
    PanelId id;
    juce::String title;
    Icon icon;
    HostKind defaultHost;
    int minWidth = 240, minHeight = 200;
    bool canDock = true, canSide = false;
    juce::KeyPress shortcut; // F2..F9 by default: show; with Shift: detach or reattach
    // The panel's smallest height when it depends on what it shows (else minHeight).
    std::function<int()> minimumHeight = {};
};

const char* panelKey(PanelId id); // "mixer", "editor"...

// A tabbed host (the dock or the side panel): a 32 px header row of tabs with
// the current panel's detach and close buttons, and the current panel below.
class PanelHost final : public juce::Component
{
public:
    PanelHost(const Theme& theme, HostKind kind);

    HostKind kind() const noexcept { return kind_; }
    void addPanel(const PanelInfo& info, juce::Component& panel, int index = -1);
    void removePanel(PanelId id);
    bool hasPanel(PanelId id) const noexcept;
    bool empty() const noexcept { return tabs_.empty(); }
    void showPanel(PanelId id);
    PanelId currentPanel() const noexcept { return current_; }
    std::vector<PanelId> panels() const;
    // Highlights the whole host as a drop target.
    void setDropHighlight(bool on);
    // The index a drop at this x (in the tab row) would insert at.
    int insertIndexForX(int x) const noexcept;

    std::function<void(PanelId)> onDetach, onClose, onShown;
    std::function<void(PanelId, juce::Point<int> screenPosition)> onTabDraggedOut;

    int headerHeight() const noexcept { return theme_.sizes.controlLarge; }
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;

private:
    struct Tab
    {
        PanelInfo info;
        juce::Component* panel = nullptr;
        juce::Rectangle<int> bounds;
    };
    int tabAt(juce::Point<int> p) const noexcept;
    void layoutTabs();

    const Theme& theme_;
    HostKind kind_;
    std::vector<Tab> tabs_;
    PanelId current_ = PanelId::mixer;
    bool hasCurrent_ = false;
    bool dropHighlight_ = false;
    int hoverTab_ = -1;
    int pressedTab_ = -1;
    juce::Point<int> pressPosition_;
    bool dragStarted_ = false;
    IconButton detach_, close_;
};

// A floating window holding one panel: a header row under the native title bar
// with the icon and title, Attach (with a menu when both hosts can take it), Pin
// and Close; dragging the header over a host attaches it there.
class FloatingPanelWindow final : public juce::DocumentWindow
{
public:
    FloatingPanelWindow(const Theme& theme, const PanelInfo& info, juce::Component& panel);
    ~FloatingPanelWindow() override;

    const PanelInfo& info() const noexcept { return info_; }
    juce::Component& panel() noexcept { return *panel_; }
    void setPinned(bool pinned);
    bool pinned() const noexcept { return pin_.getToggleState(); }

    std::function<void(PanelId, HostKind)> onAttach;
    std::function<void(PanelId)> onCloseRequested;
    std::function<void(PanelId, juce::Point<int> screenPosition, bool released)> onHeaderDrag;

    void closeButtonPressed() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    class Header;
    const Theme& theme_;
    PanelInfo info_;
    juce::Component* panel_;
    Header* header_ = nullptr;
    void* layoutListener_ = nullptr;
    IconButton pin_;
};

class PanelManager final : private juce::Timer
{
public:
    PanelManager(const Theme& theme, PanelHost& dock, PanelHost& side);
    ~PanelManager() override;

    void registerPanel(const PanelInfo& info, juce::Component& panel);
    // Where the floating windows centre and what they are children of.
    void setMainWindow(juce::Component* mainWindow) { mainWindow_ = mainWindow; }

    void show(PanelId id);
    void hide(PanelId id);
    void toggle(PanelId id);
    bool isVisible(PanelId id) const;
    HostKind hostOf(PanelId id) const;
    void moveTo(PanelId id, HostKind host, int index = -1);
    // Shift+Fn: docked -> floating; floating -> its default host.
    void detachOrReattach(PanelId id);
    bool handleKey(const juce::KeyPress& key);
    // A panel's key after the user rebinds it (an invalid key: none).
    void setShortcut(PanelId id, const juce::KeyPress& key);
    const PanelInfo* info(PanelId id) const;

    // Host sizes, animated over motion.panelMs when they open or close; the owner
    // lays the hosts out from these and is told through onLayoutChanged.
    int dockHeight() const noexcept { return static_cast<int>(dockHeightShown_); }
    int sideWidth() const noexcept { return static_cast<int>(sideWidthShown_); }
    // While a host slides, it moves over the content at its full size rather than
    // squeezing it: the rest of the window is laid out around the smaller of where
    // the slide started and where it ends (so it changes once, not every frame),
    // and the host keeps its open size, partly off screen.
    bool sliding() const noexcept { return isTimerRunning(); }
    int dockLayoutHeight() const noexcept
    {
        return sliding() ? static_cast<int>(std::min(dockFrom_, static_cast<double>(dockRequested_)))
                         : dockHeight();
    }
    int sideLayoutWidth() const noexcept
    {
        return sliding() ? static_cast<int>(std::min(sideFrom_, static_cast<double>(sideRequested_)))
                         : sideWidth();
    }
    int dockOpenHeight() const noexcept { return dockSize_; }
    int sideOpenWidth() const noexcept { return sideSize_; }
    bool dockOpen() const noexcept { return !dock_.empty() && dockRequested_ > 0; }
    bool sideOpen() const noexcept { return !side_.empty() && sideRequested_ > 0; }
    void setDockHeight(int height);
    // The dock's smallest height: its tabs and the tallest minimum of the panels
    // shown in it. enforceDockMinimum grows the dock to it when it is smaller.
    int dockMinimum() const;
    void enforceDockMinimum();
    void setSideWidth(int width);
    std::function<void()> onLayoutChanged;
    std::function<void(PanelId, bool visible)> onPanelVisibilityChanged;

    // Persistence: layout.json beside the settings file.
    juce::var toJson() const;
    void fromJson(const juce::var& layout);
    void saveTo(const juce::File& file) const;
    bool loadFrom(const juce::File& file);
    void resetLayout();
    // "Mix" (mixer floating, dock and side closed) and "Edit" (the defaults).
    void applyPreset(const juce::String& name);
    void setWindowDefaults(int mainWidth, int mainHeight);

    static constexpr int defaultDockHeight = 480;
    static constexpr int defaultSideWidth = 300;

private:
    struct Entry
    {
        PanelInfo info;
        juce::Component* panel = nullptr;
        HostKind host = HostKind::dock;
        bool visible = false;
        std::unique_ptr<FloatingPanelWindow> window;
        juce::Rectangle<int> floatingBounds; // remembered per panel
        bool pinned = false;
    };
    Entry* find(PanelId id);
    const Entry* find(PanelId id) const;
    PanelHost& hostComponent(HostKind kind);
    void placeInHost(Entry& entry, int index);
    void removeFromHost(Entry& entry);
    void openFloating(Entry& entry);
    void closeFloating(Entry& entry, bool keepVisible);
    void refreshHosts();
    void animate();
    void timerCallback() override;
    void headerDragged(PanelId id, juce::Point<int> screenPosition, bool released);
    PanelHost* hostAtScreenPosition(juce::Point<int> screenPosition);

    const Theme& theme_;
    PanelHost& dock_;
    PanelHost& side_;
    std::vector<std::unique_ptr<Entry>> entries_;
    juce::Component* mainWindow_ = nullptr;
    int dockRequested_ = defaultDockHeight, sideRequested_ = defaultSideWidth;
    int dockSize_ = defaultDockHeight, sideSize_ = defaultSideWidth; // the sizes when open
    double dockHeightShown_ = 0.0, sideWidthShown_ = 0.0;
    double animationStart_ = 0.0;
    double dockFrom_ = 0.0, sideFrom_ = 0.0;
    PanelHost* highlighted_ = nullptr;
    bool defaultsDockOpen_ = true, defaultsSideOpen_ = true;
};

} // namespace northlight::ui
