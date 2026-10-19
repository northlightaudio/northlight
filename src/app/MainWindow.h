#pragma once

#include "core/Model.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace aural::audio
{
class AudioDeviceLayer;
}

namespace aural
{

class ProjectSession;
class UserSettings;

// Top-level document window. Phase 2: a toolbar with transport, import and the
// device summary, a placeholder track list, and the Audio Setup window.
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

    // If unfinished recordings are in the media folder, asks whether to recover them.
    void offerRecovery();
    // Shows the mixer panel.
    void showMixer();
    // Shows the mixer and opens an insert's editor.
    void openInsertEditor(core::Id channelId, core::Id insertId);
    // The plugin browser (scan results, blocklist, scan controls).
    void showPluginBrowser();
    void closePluginBrowser();
    // The piano roll on a MIDI clip (one window, retargeted).
    void openPianoRoll(core::Id trackId, core::Id clipId);
    void closePianoRoll();

private:
    class Content;
    class AudioSetupWindow;
    class PluginBrowserWindow;
    class PianoRollWindow;

    void refreshPluginBrowser();
    void refreshPianoRoll();
    void refreshPianoRollPlayhead();

    audio::AudioDeviceLayer& audio_;
    ProjectSession& session_;
    UserSettings& settings_;
    std::unique_ptr<AudioSetupWindow> audioSetupWindow_;
    std::unique_ptr<PluginBrowserWindow> pluginBrowserWindow_;
    std::unique_ptr<PianoRollWindow> pianoRollWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace aural
