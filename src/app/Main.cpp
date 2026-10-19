#include "MainWindow.h"
#include "ProjectSession.h"
#include "UserSettings.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Command.h"
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
        session_ = std::make_unique<ProjectSession>(*audio_);
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *audio_, *session_);

        // Open the device after the window is up so a slow driver does not delay
        // the first paint. The session is already attached to the callback.
        audio_->initialise();

        // Recordings interrupted by a crash are repaired and brought back as tracks.
        session_->recoverPartialRecordings();

        applyLaunchFlags(commandLine);
    }

    // Developer conveniences for unattended checks:
    //   --audio-setup        open the Audio Setup window
    //   --import <file>      import an audio file onto a new track (repeatable)
    //   --play               start the transport
    //   --record <n>         create n armed tracks on inputs 1..n and start recording
    //   --stop-after <s>     stop the transport after s seconds
    void applyLaunchFlags(const juce::String& commandLine)
    {
        const auto tokens = juce::ArgumentList("Aural", commandLine).arguments;
        for (int i = 0; i < tokens.size(); ++i)
        {
            const auto& arg = tokens.getReference(i);
            if (arg == "--audio-setup")
                mainWindow_->showAudioSetup();
            else if (arg == "--import" && i + 1 < tokens.size())
                session_->importAudioFile(tokens.getReference(++i).resolveAsFile(), 0);
            else if (arg == "--play")
                session_->play();
            else if (arg == "--record" && i + 1 < tokens.size())
            {
                const int inputs = tokens.getReference(++i).text.getIntValue();
                for (int input = 0; input < inputs; ++input)
                {
                    core::Track track;
                    track.name = "Input " + std::to_string(input + 1);
                    track.inputChannel = input;
                    track.armed = true;
                    session_->execute(std::make_unique<core::AddTrack>(track));
                }
                // The device opens asynchronously from the UI's point of view; give it a
                // moment before recording.
                juce::Timer::callAfterDelay(1500, [this] { session_->startRecording(); });
            }
            else if (arg == "--stop-after" && i + 1 < tokens.size())
            {
                const int seconds = tokens.getReference(++i).text.getIntValue();
                juce::Timer::callAfterDelay(seconds * 1000, [this] { session_->stop(); });
            }
        }
    }

    void shutdown() override
    {
        mainWindow_.reset();
        audio_->shutdown(); // stop the device before the engine goes away
        session_.reset();
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
    std::unique_ptr<ProjectSession> session_;
    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace aural

START_JUCE_APPLICATION(aural::AuralApplication)
