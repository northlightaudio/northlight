#pragma once

#include "core/Model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace northlight::audio
{
class AudioDeviceLayer;
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
    // The audio editor on an audio clip, in pitch mode when asked.
    void openAudioEditor(core::Id trackId, core::Id clipId, bool pitch = false);
    // Selects an audio clip as a click on it would, so the inspector shows it.
    void selectClip(core::Id trackId, core::Id clipId);
    // Launch-flag conveniences for the audio editor's pitch tools.
    void correctPitchInEditor();
    void muteBreathsInEditor();
    // --capture: saves the window and every part of it tagged "capture:<name>" that is
    // showing (the mixer, the channel strip, the instrument's editor, the piano roll,
    // the audio editor) as <name>.png in the folder, at `scale` times their size.
    // Returns the file names written.
    juce::StringArray capture(const juce::File& folder, float scale);
    // A layout preset ("Mix", "Edit") for unattended captures and the menu.
    void applyLayoutPreset(const juce::String& name);
    // Shows a panel by its key (mixer, editor, pianoRoll, diagnostics, browser, inspector).
    void showPanel(const juce::String& key);
    // Opens a menu as a click would ("project", "add-track"), for checking how menus
    // appear (--show-menu).
    void showMenu(const juce::String& key);
    // The strip drag benchmark (--benchmark-strip-drag).
    void runStripDragBenchmark(int seconds);
    // Shows a track's automation over its lane in the arrange view.
    void showAutomation(core::Id trackId);
    // A toast or a status message, for the application's own notices.
    void notify(const juce::String& message, bool warning = false);

private:
    class Content;
    class AudioSetupWindow;
    class PreferencesWindow;

    audio::AudioDeviceLayer& audio_;
    ProjectSession& session_;
    UserSettings& settings_;
    std::unique_ptr<AudioSetupWindow> audioSetupWindow_;
    std::unique_ptr<PreferencesWindow> preferencesWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace northlight
