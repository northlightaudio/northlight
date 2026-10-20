#pragma once

#include "core/Model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace aural::audio
{
class AudioDeviceLayer;
}

namespace aural
{
class ProjectSession;
class UserSettings;

// The main window (docs/design/03-shell.md): the header bar with the transport,
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

    // Opens the Audio Setup window (non-modal) or brings it to the front.
    void showAudioSetup();
    void closeAudioSetup();
    // Opens Preferences on a page ("general", "audio", ...).
    void showPreferences(const juce::String& page = {});
    void closePreferences();

    // If unfinished recordings are in the media folder, offers to recover them (a toast).
    void offerRecovery();
    // The first-run sheet (08-setup.md 8.3): device choice, then a plugin scan offer.
    void showFirstRun();
    // --benchmark-ui: plays and scrolls for `seconds`, prints frame percentiles, quits.
    void runBenchmark(int seconds);
    // --bus / --remove-bus: adds a bus; removes the last one through the mixer's
    // command path (the sheet when channels route to it), for unattended checks.
    void addBus();
    void removeLastBus();
    // Shows the mixer panel.
    void showMixer();
    // Shows the mixer and opens an insert's editor.
    void openInsertEditor(core::Id channelId, core::Id insertId);
    // The plugin browser: the Browser panel in its own window.
    void showPluginBrowser();
    // The piano roll panel on a MIDI clip.
    void openPianoRoll(core::Id trackId, core::Id clipId);
    // A layout preset ("Mix", "Edit") for unattended captures and the menu.
    void applyLayoutPreset(const juce::String& name);
    // Shows a panel by its key (mixer, editor, pianoRoll, diagnostics, browser, inspector).
    void showPanel(const juce::String& key);
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

} // namespace aural
