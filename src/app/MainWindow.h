#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace aural::audio
{
class AudioDeviceLayer;
}

namespace aural
{

class ProjectSession;

// Top-level document window. Phase 2: a toolbar with transport, import and the
// device summary, a placeholder track list, and the Audio Setup window.
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, audio::AudioDeviceLayer& audio, ProjectSession& session);
    ~MainWindow() override;

    void closeButtonPressed() override;

    // Opens the Audio Setup window (non-modal) or brings it to the front.
    void showAudioSetup();
    void closeAudioSetup();

    // If unfinished recordings are in the media folder, asks whether to recover them.
    void offerRecovery();
    // Shows the mixer panel.
    void showMixer();

private:
    class Content;
    class AudioSetupWindow;

    audio::AudioDeviceLayer& audio_;
    ProjectSession& session_;
    std::unique_ptr<AudioSetupWindow> audioSetupWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace aural
