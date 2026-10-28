#pragma once

#include "core/Model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace northlight::audio
{
class AudioDeviceLayer;
}
namespace northlight::ui
{
class ArrangeView;
}

namespace northlight
{
class ProjectSession;
class UserSettings;

// The main window (docs/design/screens/shell.md): the header bar with the transport,
// the arrange view, the dock and side panel hosts with their panels (mixer,
// editor, piano roll, diagnostics; browser, inspector), the status bar and the
// toasts. Secondary windows exist only for plugin editors, Audio Setup and
// floating panels.
class MainWindow final : public juce::DocumentWindow
{
public:
    // With ui::offscreenMode() (--offscreen) the window never gets a native
    // window: it lays out, ticks and paints for --capture, unseen.
    MainWindow(const juce::String& name,
               audio::AudioDeviceLayer& audio,
               ProjectSession& session,
               UserSettings& settings);
    ~MainWindow() override;

    void closeButtonPressed() override;
    // The custom chrome: Windows asks what is under the mouse, and the header bar
    // answers (caption, a window button, or a control).
    WindowControlKind findControlAtPoint(juce::Point<float> point) const override;

    // Opens the Audio Setup window (non-modal) or brings it to the front.
    void showAudioSetup();
    // The About box: the splash screen, centred over the window.
    void showAbout();
    void closeAudioSetup();
    // Opens Preferences on a page ("general", "audio", ...).
    void showPreferences(const juce::String& page = {});
    void closePreferences();

    // If unfinished recordings are in the media folder, offers to recover them (a toast).
    void offerRecovery();
    // The first-run sheet (screens/setup.md 8.3): device choice, then a plugin scan offer.
    void showFirstRun();
    // The export sheet (Ctrl+E).
    void showExport();
    // --benchmark-ui: plays and scrolls for `seconds`, prints frame percentiles, quits.
    void runBenchmark(int seconds);
    // --benchmark-panels: opens and closes the inspector `cycles` times, prints the
    // slide's frame intervals, quits.
    void runPanelBenchmark(int cycles);
    // --bus / --remove-bus: adds a bus; removes the last one through the mixer's
    // command path (the sheet when channels route to it), for unattended checks.
    void addBus();
    void removeLastBus();
    // Shows the mixer panel.
    void showMixer();
    // --mixer-wide: the mixer's wide strips. --dock-height: the dock's height.
    void setMixerWide(bool wide);
    void setDockHeight(int height);
    void showTempoTrack();
    // Shows the mixer and opens an insert's editor.
    void openInsertEditor(core::Id channelId, core::Id insertId);
    // The plugin browser: the Browser panel in its own window.
    void showPluginBrowser();
    // The Browser panel on its Files tab (at a folder, when one is given).
    void showFilesBrowser(const juce::File& folder = {});
    // The piano roll panel on a MIDI clip.
    void openPianoRoll(core::Id trackId, core::Id clipId);
    // The piano roll's lane by key (ui::PianoRoll::laneForKey: "velocity", "bend", "mod", "cc74").
    void setPianoRollLane(const juce::String& key);
    // The piano roll's view for captures (launch flag --piano-roll-view): a comma
    // list of ghosts, named, follow, select (all notes), mute (the selection),
    // lane=<key> (another lane), rows=<px>, grid=<label> ("1/8T"), fit.
    void configurePianoRoll(const juce::String& options);
    // The audio editor on an audio clip, in pitch mode when asked.
    void openAudioEditor(core::Id trackId, core::Id clipId, bool pitch = false);
    void zoomAudioEditor(double samplesPerPixel, double secondsIntoClip);
    // Selects an audio clip as a click on it would, so the inspector shows it.
    void selectClip(core::Id trackId, core::Id clipId);
    // Launch-flag conveniences for the audio editor's pitch tools.
    void correctPitchInEditor();
    void muteBreathsInEditor();
    // --capture: saves the window and every part of it tagged "capture:<name>" that is
    // showing (the mixer, the channel strip, the instrument's editor, the piano roll,
    // the audio editor) as <name>.png in the folder, at `scale` times their size, and
    // every other open window (an insert's editor) as window-<its name>.png.
    // Returns the file names written.
    juce::StringArray capture(const juce::File& folder, float scale);
    // A layout preset ("Mix", "Edit") for unattended captures and the menu.
    void applyLayoutPreset(const juce::String& name);
    // Shows a panel by its key (mixer, editor, pianoRoll, diagnostics, browser, inspector).
    void showPanel(const juce::String& key);
    // Stores a setting and applies it as Preferences would (--contrast, --reduce-motion).
    void setPreference(const std::string& key, const std::string& value);
    // Opens a menu as a click would ("project", "add-track", "clip": the first
    // clip's), for checking how menus appear (--show-menu).
    void showMenu(const juce::String& key);
    // The arrange view's tool by name ("select", "split", ...; --arrange-tool) and
    // the pointer resting on a track row at a bar (--arrange-pointer), for captures.
    bool setArrangeTool(const juce::String& name);
    void arrangePointer(int row, double bar, juce::ModifierKeys mods);
    // The arrange view itself, for launch flags that set up its state (track and
    // time selections, captures of its menus). Null before the content exists.
    ui::ArrangeView* arrangeView();
    // Shows the Editor panel's nth gap button (1-based) as the pointer in its gap
    // would, for captures (--editor-gap).
    void showEditorGap(int n);
    // The strip drag benchmark (--benchmark-strip-drag).
    void runStripDragBenchmark(int seconds);
    // The hover benchmark (--benchmark-hover).
    void runHoverBenchmark(int seconds);
    // Shows a track's automation over its lane in the arrange view.
    void showAutomation(core::Id trackId);
    // Opens a project as File > Open does: the loading sheet shows its progress.
    // With `holdAt` (reading, media, plugins, engine: --show-loading) the load
    // stops once it reaches that stage, so a capture shows the sheet.
    void openWithProgress(const juce::File& file, const juce::String& holdAt = {});
    // A project passed by Explorer (the command line, or another launch forwarding
    // its file): the window comes forward and the file opens after the unsaved
    // changes question.
    void openFromShell(const juce::File& file);
    // A toast or a status message, for the application's own notices.
    void notify(const juce::String& message, bool warning = false);

private:
    class Content;
    class AudioSetupWindow;
    class PreferencesWindow;

    // The content, owned by the window (setContentOwned); typed so the facade reaches it directly.
    Content* content_ = nullptr;
    std::unique_ptr<juce::TimedCallback> offscreenPainter_; // --offscreen only

    // The window's size, position and maximised state, kept in the user's settings
    // when it closes and restored at launch if it would still be on a screen. Not
    // with --offscreen, whose window is never shown.
    void restorePlacement();
    void savePlacement();
    static constexpr const char* placementKey = "window.placement";

    audio::AudioDeviceLayer& audio_;
    ProjectSession& session_;
    UserSettings& settings_;
    std::unique_ptr<AudioSetupWindow> audioSetupWindow_;
    std::unique_ptr<PreferencesWindow> preferencesWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace northlight
