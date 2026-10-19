#include "MainWindow.h"
#include "UserSettings.h"
#include "audio/AudioDeviceLayer.h"
#include "ui/AuralLookAndFeel.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace aural
{

class AuralApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        lookAndFeel_ = std::make_unique<ui::AuralLookAndFeel>(ui::Theme::dark());
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());

        settings_ = std::make_unique<UserSettings>();
        audio_ = std::make_unique<audio::AudioDeviceLayer>(*settings_);
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *audio_);

        // Open the device after the window is up so a slow driver does not delay
        // the first paint.
        audio_->initialise();

        // Developer convenience: `Aural.exe --audio-setup` opens straight into the
        // Audio Setup window.
        if (commandLine.contains("--audio-setup"))
            mainWindow_->showAudioSetup();
    }

    void shutdown() override
    {
        mainWindow_.reset();
        audio_.reset();
        settings_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        lookAndFeel_.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<ui::AuralLookAndFeel> lookAndFeel_;
    std::unique_ptr<UserSettings> settings_;
    std::unique_ptr<audio::AudioDeviceLayer> audio_;
    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace aural

START_JUCE_APPLICATION(aural::AuralApplication)
