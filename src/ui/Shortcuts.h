#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

// The keyboard map (interaction.md 10.4 as implemented) as one registry: every
// action a key can run, with a stable id, the name the Shortcuts page shows, the
// section it is listed under, the context it works in (the whole window, the
// arrange view, the piano roll or the audio editor) and its default keys. The
// user may rebind an action on the Preferences Shortcuts page; the bindings are
// kept in the user settings and loaded here, so everything that reads a key reads
// the current one: the main window dispatches from it, the panels take their keys
// from it, the arrange view, piano roll and audio editor match through it, and the
// Shortcuts page, tooltips and menus print it. A command's first listed key is the
// one tooltips and menus show. Message thread only.

namespace northlight::ui
{

enum class Command
{
    // The window's (MainWindow dispatches these).
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
    closePluginWindows,
    addAudioTrack,
    addMidiTrack,
    addBus,
    // The panels' (with Shift, the key detaches or reattaches the panel).
    showMixer,
    showEditor,
    showBrowser,
    showInspector,
    showPianoRoll,
    showDiagnostics,
    showInstruments,
    showAudioEditor,
    // The arrange view's (ArrangeKeymap acts on these; the window on renameTrack).
    renameTrack, // F2 while the arrange view has the focus
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
    selectTrack, // Up / Down (Shift extends)
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
    nudge,
    // The piano roll's (while it has the focus).
    pianoRollDelete,
    pianoRollSelectTool,
    pianoRollDrawTool,
    pianoRollEraseTool,
    pianoRollSplitTool,
    pianoRollGlueTool,
    pianoRollMuteTool,
    pianoRollCopy,
    pianoRollCut,
    pianoRollPaste,
    pianoRollMuteNotes,
    pianoRollSplitAtPlayhead,
    pianoRollJoin,
    pianoRollSelectAll,
    pianoRollDuplicate,
    pianoRollClearSelection,
    pianoRollSnap,
    pianoRollQuantise,
    pianoRollQuantiseEnds,
    pianoRollFinerGrid,
    pianoRollCoarserGrid,
    pianoRollTransposeUp,
    pianoRollTransposeDown,
    pianoRollNudge,         // Left / Right (Shift: fine)
    pianoRollStepSelection, // Ctrl+Left / Right (Shift extends)
    pianoRollLength,        // Alt+Left / Right (Shift: fine)
    pianoRollNudgePitch,    // Up / Down (Ctrl: an octave)
                            // The audio editor's (in pitch mode, while it has the focus).
    audioEditorTool,        // 1 to 5, in the tools' order
    audioEditorNudgePitch,
    audioEditorStepSelection, // Left / Right (Shift extends)
    audioEditorMute,
    audioEditorSelectAll,
    audioEditorClearSelection,
};

// Where a command's keys work. A view's keys are tried before the window's while
// it has the focus, so a key bound both in a view and globally is a conflict.
enum class Context
{
    global,
    arrange,
    pianoRoll,
    audioEditor,
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
    // The same key as `other`, however each is written ('r' typed is the R key).
    bool sameAs(const Key& other) const noexcept;
    // "Ctrl+Shift+S", "Space", "F2", ",", "Num0".
    juce::String text() const;
    // The press a keyboard sends for this key (what tests post).
    juce::KeyPress keyPress() const;
    // The key a press records on the Shortcuts page: its code and modifiers.
    static Key fromPress(const juce::KeyPress& press);
    // False for presses the Shortcuts page will not record (Tab moves the focus).
    static bool bindable(const juce::KeyPress& press);
};

struct Shortcut
{
    Command command;
    const char* id;      // stable, for the settings file ("playStop")
    const char* name;    // what it does, as the Shortcuts page lists it
    const char* section; // the Shortcuts page's group
    Context context = Context::global;
    std::vector<Key> defaults;
    // A family of keys that work as a set (the tools' 1 to 7, the arrows): listed,
    // not rebindable.
    bool fixed = false;
    std::vector<Key> keys; // the current keys: the defaults or the user's binding
};

class Shortcuts
{
public:
    // Every command, in dispatch order within its context.
    static const std::vector<Shortcut>& all();
    static const Shortcut& of(Command command);
    static const Shortcut* find(const juce::String& id);
    static bool matches(Command command, const juce::KeyPress& press);
    // The index of the key that matched (pickTool: which tool), -1 for none.
    static int matchIndex(Command command, const juce::KeyPress& press);
    // The key tooltips and menus show ("" for a command with none listed).
    static juce::String keyText(Command command);
    // "Undo  Ctrl+Z": the tooltip form, which the look and feel draws as key caps.
    static juce::String withKey(const juce::String& text, Command command);
    // Gives a control the tooltip `text` with the command's key, and remembers both,
    // so retag() can print the key again after the user rebinds it.
    static void tag(juce::Component& control, const juce::String& text, Command command);
    // Rewrites the tagged tooltips under `root` with the current keys.
    static void retag(juce::Component& root);
    // The commands of a context that `press` runs, in dispatch order.
    static std::vector<Command> commandsFor(const juce::KeyPress& press, Context context);
    // The window's commands for `press`: the global context's.
    static std::vector<Command> windowCommandsFor(const juce::KeyPress& press);
    // The main window's handler: offers `press` to each global command bound to it
    // until `perform` takes one. True when one did.
    template <typename Perform> static bool dispatch(const juce::KeyPress& press, Perform&& perform)
    {
        for (const auto command : windowCommandsFor(press))
            if (perform(command))
                return true;
        return false;
    }

    // ----- rebinding ------------------------------------------------------------------
    // Whether two contexts see the same key press (either is global, or both the same).
    static bool overlaps(Context a, Context b) noexcept;
    // The other commands `key` would collide with if `command` took it.
    static std::vector<Command> conflicts(Command command, const Key& key);
    // Binds `command` to `key` alone, replacing its keys, after taking the key away
    // from the commands in `takeFrom`. A fixed command ignores it.
    static void bind(Command command, const Key& key, const std::vector<Command>& takeFrom = {});
    static void reset(Command command);
    static void resetAll();
    static bool isDefault(Command command);
    static bool allDefault();
    // The user's bindings as settings text, one "id=mods.code,mods.code" per changed
    // command, space-separated ("id=" is unbound), and back. Unknown ids and bad
    // keys are skipped; a command the text does not name takes its defaults.
    static std::string save();
    static void load(const std::string& text);
    // Bumped by every change to the bindings.
    static int generation() noexcept;

    // The Shortcuts page's sections, in order.
    static const std::vector<const char*>& sections();
};

} // namespace northlight::ui
