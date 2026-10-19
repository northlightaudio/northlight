#include "MainWindow.h"
#include "ProjectSession.h"
#include "UserSettings.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Command.h"
#include "plugins/BuiltIns.h"
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
        settings_ = std::make_unique<UserSettings>();
        // The design tokens: shipped beside the app, with the user's tokens.json
        // beside the settings file applied on top.
        ui::Theme::initialise(settings_->file().getSiblingFile("tokens.json"));
        lookAndFeel_ = std::make_unique<ui::AuralLookAndFeel>(ui::Theme::dark());
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());

        audio_ = std::make_unique<audio::AudioDeviceLayer>(*settings_);
        session_ =
            std::make_unique<ProjectSession>(*audio_, settings_->file().getSiblingFile("plugins.json"));
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *audio_, *session_, *settings_);

        // Open the device after the window is up so a slow driver does not delay
        // the first paint. The session is already attached to the callback.
        audio_->initialise();

        // Recordings interrupted by a crash are offered back as tracks.
        mainWindow_->offerRecovery();

        applyLaunchFlags(commandLine);
    }

    // Developer conveniences for unattended checks:
    //   --audio-setup        open the Audio Setup window
    //   --import <file>      import an audio file onto a new track (repeatable)
    //   --play               start the transport
    //   --record <n>         create n armed tracks on inputs 1..n and start recording
    //   --stop-after <s>     stop the transport after s seconds
    //   --loop <from> <to>   set and enable the loop region, in seconds
    //   --no-count-in        record without the count-in
    //   --midi-track         add a MIDI track (built-in synth) with a short clip
    //   --midi-track-with <type>  the same with the given instrument type instead
    //   --piano-roll         open the piano roll on the first MIDI clip
    void applyLaunchFlags(const juce::String& commandLine)
    {
        const auto tokens = juce::ArgumentList("Aural", commandLine).arguments;
        int stopAfterSeconds = 0;
        for (int i = 0; i < tokens.size(); ++i)
        {
            const auto& arg = tokens.getReference(i);
            if (arg == "--audio-setup")
                mainWindow_->showAudioSetup();
            else if (arg == "--open" && i + 1 < tokens.size())
                session_->open(tokens.getReference(++i).resolveAsFile());
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
            else if (arg == "--loop" && i + 2 < tokens.size())
            {
                const double from = tokens.getReference(++i).text.getDoubleValue();
                const double to = tokens.getReference(++i).text.getDoubleValue();
                const double rate = session_->project().sampleRate;
                session_->setRegion(static_cast<core::SamplePosition>(from * rate),
                                    static_cast<core::SamplePosition>(to * rate));
                session_->setLoopEnabled(true);
            }
            else if (arg == "--no-count-in")
                session_->setCountInBars(0);
            else if (arg == "--midi-track" || (arg == "--midi-track-with" && i + 1 < tokens.size()))
            {
                // A MIDI track with the built-in synth (or the given instrument type)
                // and a four-note clip.
                const auto trackId = session_->addMidiTrack();
                if (arg == "--midi-track-with")
                {
                    const auto type = tokens.getReference(++i).text.unquoted().toStdString();
                    if (const auto* track = session_->project().findTrack(trackId);
                        track != nullptr && !track->inserts.empty())
                        session_->execute(
                            std::make_unique<core::RemoveInsert>(trackId, track->inserts[0].id));
                    core::Insert instrument;
                    instrument.type = type;
                    instrument.params = plugins::defaultParameters(type);
                    session_->execute(std::make_unique<core::AddInsert>(trackId, instrument));
                }
                const auto beat = static_cast<core::SamplePosition>(
                    core::samplesPerBeat(session_->project().sampleRate,
                                         session_->project().tempoBpm,
                                         session_->project().timeSignature));
                core::MidiClip clip;
                clip.name = "Riff";
                clip.start = 0;
                clip.length = beat * 4;
                for (int n = 0; n < 4; ++n)
                    clip.notes.push_back({core::invalidId, 60 + n * 2, 100, beat * n, beat / 2});
                session_->execute(std::make_unique<core::AddMidiClip>(trackId, clip));
            }
            else if (arg == "--piano-roll")
            {
                // Opens the piano roll on the first MIDI clip.
                for (const auto& track : session_->project().tracks)
                    if (!track.midiClips.empty())
                    {
                        mainWindow_->openPianoRoll(track.id, track.midiClips.front().id);
                        break;
                    }
            }
            else if (arg == "--mixer")
                mainWindow_->showMixer();
            else if (arg == "--insert" && i + 1 < tokens.size())
            {
                // Adds a built-in insert to every track and opens the first one's editor.
                const auto type = tokens.getReference(++i).text.toStdString();
                core::Id first = core::invalidId, firstTrack = core::invalidId;
                for (const auto& track : session_->project().tracks)
                {
                    core::Insert insert;
                    insert.type = type;
                    insert.params = plugins::defaultParameters(type);
                    auto command = std::make_unique<core::AddInsert>(track.id, insert);
                    const auto* raw = command.get();
                    session_->execute(std::move(command));
                    if (first == core::invalidId)
                    {
                        first = raw->insertId();
                        firstTrack = track.id;
                    }
                }
                if (first != core::invalidId)
                    mainWindow_->openInsertEditor(firstTrack, first);
            }
            else if (arg == "--stop-after" && i + 1 < tokens.size())
            {
                stopAfterSeconds = tokens.getReference(++i).text.getIntValue();
                juce::Timer::callAfterDelay(stopAfterSeconds * 1000, [this] { session_->stop(); });
            }
            else if (arg == "--scan-plugins")
                session_->scanPlugins(true);
            else if (arg == "--plugins")
                mainWindow_->showPluginBrowser();
            else if (arg == "--marker" && i + 1 < tokens.size())
            {
                core::Marker marker;
                marker.position = static_cast<core::SamplePosition>(
                    tokens.getReference(++i).text.getDoubleValue() * session_->project().sampleRate);
                marker.name = "Marker " + std::to_string(session_->project().markers.size() + 1);
                session_->execute(std::make_unique<core::AddMarker>(marker));
            }
            else if (arg == "--recover")
                session_->recoverPartialRecordings(); // without the prompt, for unattended checks
            else if (arg == "--save" && i + 1 < tokens.size())
            {
                // Saves two seconds after a --stop-after given before it (so the
                // recording has been placed), otherwise half a second after launch.
                const auto file = tokens.getReference(++i).resolveAsFile();
                juce::Timer::callAfterDelay(stopAfterSeconds > 0 ? stopAfterSeconds * 1000 + 2000 : 500,
                                            [this, file] { session_->save(file); });
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
