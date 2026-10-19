#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace aural::audio
{
class AudioDeviceLayer;
}

namespace aural
{

// Top-level document window. Phase 1: a toolbar with the device summary and an
// "Audio Setup" button over a placeholder body. The arrange view, transport and
// mixer land here in Phases 2 and 4.
class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow(const juce::String& name, audio::AudioDeviceLayer& audio);
    ~MainWindow() override;

    void closeButtonPressed() override;

    // Opens the Audio Setup window (non-modal) or brings it to the front.
    void showAudioSetup();
    void closeAudioSetup();

private:
    class Content;
    class AudioSetupWindow;

    audio::AudioDeviceLayer& audio_;
    std::unique_ptr<AudioSetupWindow> audioSetupWindow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace aural
