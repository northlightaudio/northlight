#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>
#include <vector>

// The keyboard map (interaction.md 10.4 as implemented) as one table: every key a
// command answers, what the command is called and where it is listed. The main
// window dispatches its commands from it, the panels take their keys from it,
// the arrange view's keymap matches through it, the Preferences Shortcuts page
// lists it, and tooltips and menus print the keys it gives. A command's first
// listed key is the one tooltips and menus show.

namespace northlight::ui
{

enum class Command
{
    // The window's (MainWindow dispatches these).
    renameTrack, // F2 while the arrange view has the focus
    findPlugin,
    playStop,
    pause,
    record,
    armSelected,
    returnToStart,
    goToEnd,
    loop,
    punch,
    click,
    zoomIn,
    zoomOut,
    zoomToFit,
    zoomToSelection,
    snap,
    ripple,
    muteTrack,
    soloTrack,
    newProject,
    open,
    save,
    saveAs,
    importAudio,
    exportMix,
    undo,
    redo,
    audioSetup,
    preferences,
    plugins,
    addAudioTrack,
    addMidiTrack,
    addBus,
    // The panels' (F-keys show one; with Shift, detach or reattach it).
    showMixer,
    showEditor,
    showBrowser,
    showInspector,
    showPianoRoll,
    showDiagnostics,
    showInstruments,
    showAudioEditor,
    // The arrange view's (ArrangeKeymap acts on these).
    deleteSelection,
    addMarker,
    addRegion,
    previousMarker,
    nextMarker,
    duplicate,
    copy,
    cut,
    paste,
    pickTool, // 1 to 7, in the tools' order
    muteClips,
    cycleTake,
    takeLanes,
    nudgeFine, // Alt+Left / Right (Shift: a quarter step)
    selectAll,
    automationLanes,
    normalise,
    clearSelection,
    splitAtPlayhead,
    splitAtSelection,
    join,
    crossfade,
    snapInArrange,
    rippleInArrange,
    nudge,
};

// One key: a key code (or the character typed, for keys whose shifted form is
// what matters) and the exact modifiers.
struct Key
{
    int code = 0;
    int modifiers = 0;      // juce::ModifierKeys flags, mouse buttons ignored
    bool character = false; // match the text character rather than the key code
    bool listed = true;     // shown on the Shortcuts page, in tooltips and menus

    bool matches(const juce::KeyPress& press) const noexcept;
    // "Ctrl+Shift+S", "Space", "F2", ",".
    juce::String text() const;
    juce::KeyPress keyPress() const { return juce::KeyPress(code, juce::ModifierKeys(modifiers), 0); }
};

struct Shortcut
{
    Command command;
    const char* name; // what it does, for tests and tools
    std::vector<Key> keys;
};

class Shortcuts
{
public:
    // Every command, in dispatch order within its owner.
    static const std::vector<Shortcut>& all();
    static const Shortcut& of(Command command);
    static bool matches(Command command, const juce::KeyPress& press);
    // The index of the key that matched (pickTool: which tool), -1 for none.
    static int matchIndex(Command command, const juce::KeyPress& press);
    // The key tooltips and menus show ("" for a command with none listed).
    static juce::String keyText(Command command);
    // "Undo  Ctrl+Z": the tooltip form, which the look and feel draws as key caps.
    static juce::String withKey(const juce::String& text, Command command);
    // The commands the window dispatches matching `press`, in order.
    static std::vector<Command> windowCommandsFor(const juce::KeyPress& press);

    // The Shortcuts page: a row per action (several commands can share one, "Zoom
    // in / out"), keys as space-separated tokens joined by "/", "or" and "to".
    struct Row
    {
        juce::String action, keys, group;
    };
    static const std::vector<Row>& pageRows();
};

} // namespace northlight::ui
