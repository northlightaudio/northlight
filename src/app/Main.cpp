#include "MainWindow.h"
#include "ProjectSession.h"
#include "UserSettings.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Command.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "ui/AuralLookAndFeel.h"
#include "ui/PreferencesView.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <cmath>

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
        // The very first launch (no settings file yet) gets the device sheet.
        firstRun_ = !UserSettings().file().existsAsFile();
        settings_ = std::make_unique<UserSettings>();
        // The design tokens: shipped beside the app, with the user's tokens.json
        // beside the settings file applied on top.
        ui::Theme::initialise(settings_->file().getSiblingFile("tokens.json"));
        lookAndFeel_ = std::make_unique<ui::AuralLookAndFeel>(ui::Theme::dark());
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());

        audio_ = std::make_unique<audio::AudioDeviceLayer>(*settings_);
        session_ =
            std::make_unique<ProjectSession>(*audio_, settings_->file().getSiblingFile("plugins.json"));
        session_->setAutosave(settings_->file().getSiblingFile("Autosave"),
                              [this]
                              {
                                  const auto minutes =
                                      settings_->value(ui::PreferencesView::Keys::autosaveMinutes);
                                  return (minutes.empty() ? 2 : juce::String(minutes).getIntValue()) * 60;
                              });
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *audio_, *session_, *settings_);

        // Open the device after the window is up so a slow driver does not delay
        // the first paint. The session is already attached to the callback.
        audio_->initialise();

        // Recordings interrupted by a crash are offered back as tracks.
        mainWindow_->offerRecovery();

        applyLaunchFlags(commandLine);
        if (firstRun_ || commandLine.contains("--first-run"))
            mainWindow_->showFirstRun();
    }

    // Developer conveniences for unattended checks:
    //   --audio-setup        open the Audio Setup window
    //   --import <file>      import an audio file onto a new track (repeatable)
    //   --play               start the transport
    //   --record <n>         create n armed tracks on inputs 1..n and start recording
    //   --stop-after <s>     stop the transport after s seconds
    //   --loop <from> <to>   set and enable the loop region, in seconds
    //   --no-count-in        record without the count-in
    //   --layout <Mix|Edit>  apply a layout preset
    //   --panel <name>       show a panel: mixer, editor, pianoRoll, diagnostics, browser, inspector
    //   --preferences [page] open Preferences (general, audio, recording, editing, plugins, shortcuts,
    //   diagnostics)
    //   --benchmark-ui <s>   play and scroll for s seconds, print frame percentiles, quit
    //   --bus                add a bus
    //   --remove-bus         remove the last bus the way the mixer does
    //   --first-run          show the first-run sheet
    //   --midi-track         add a MIDI track (built-in synth) with a short clip
    //   --midi-track-with <type>  the same with the given instrument type instead
    //   --piano-roll         open the piano roll on the first MIDI clip
    //   --automation         add a volume lane to the first track and show it in the arrange view
    //   --master-gain <dB>   set the master fader (-120 = silent: an unattended check that
    //                        should not be heard; pre-fader displays still move)
    //   --strip <modules>    switch channel strip modules on for every track and show the
    //                        Editor panel: a comma list of gate, eq, compressor, deesser,
    //                        saturation, limiter, or all; "demo" also sets values that give
    //                        every display something to show
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
            else if (arg == "--master-gain" && i + 1 < tokens.size())
            {
                const float db = tokens.getReference(++i).text.getFloatValue();
                session_->execute(std::make_unique<core::SetMasterGain>(
                    db <= -120.0f ? 0.0f : std::pow(10.0f, db / 20.0f)));
            }
            else if (arg == "--automation")
            {
                // A volume lane on the first track (a slow swell), shown in the arrange view.
                if (!session_->project().tracks.empty())
                {
                    const auto trackId = session_->project().tracks.front().id;
                    const double rate = session_->project().sampleRate;
                    core::AutomationLane lane;
                    lane.target = core::AutomationTarget::gain;
                    const float values[] = {1.0f, 0.3f, 0.3f, 1.0f, 0.6f, 1.4f, 0.0f};
                    for (int p = 0; p < 7; ++p)
                        lane.points.push_back({static_cast<core::SamplePosition>(p * 2.5 * rate), values[p]});
                    session_->execute(std::make_unique<core::AddAutomationLane>(trackId, lane));
                    mainWindow_->showAutomation(trackId);
                }
            }
            else if (arg == "--strip" && i + 1 < tokens.size())
                applyStripFlag(tokens.getReference(++i).text.unquoted());
            else if (arg == "--bus")
                mainWindow_->addBus();
            else if (arg == "--remove-bus")
                mainWindow_->removeLastBus();
            else if (arg == "--mixer")
                mainWindow_->showMixer();
            else if (arg == "--layout" && i + 1 < tokens.size())
                mainWindow_->applyLayoutPreset(tokens.getReference(++i).text.unquoted());
            else if (arg == "--panel" && i + 1 < tokens.size())
                mainWindow_->showPanel(tokens.getReference(++i).text.unquoted());
            else if (arg == "--benchmark-ui" && i + 1 < tokens.size())
                mainWindow_->runBenchmark(tokens.getReference(++i).text.getIntValue());
            else if (arg == "--preferences")
                mainWindow_->showPreferences(i + 1 < tokens.size() && !tokens[i + 1].text.startsWith("--")
                                                 ? tokens.getReference(++i).text.unquoted()
                                                 : juce::String());
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

    void applyStripFlag(const juce::String& text)
    {
        namespace ps = plugins::strip;
        const auto names = juce::StringArray::fromTokens(text.toLowerCase(), ",", "");
        const bool all = names.contains("all") || (names.contains("demo") && names.size() == 1);
        const bool demo = names.contains("demo");
        const auto tracks = session_->project().tracks; // a copy: commands change the project
        for (const auto& track : tracks)
            for (const auto& module : track.strip)
            {
                const auto name = juce::String(module.type).fromLastOccurrenceOf(".", false, false);
                if (all || names.contains(name))
                    session_->execute(std::make_unique<core::SetInsertBypass>(track.id, module.id, false));
                if (!demo || !(all || names.contains(name)))
                    continue;
                const auto set = [&](int index, float value)
                {
                    session_->execute(
                        std::make_unique<core::SetInsertParameter>(track.id, module.id, index, value));
                };
                if (module.type == ps::types::eq)
                {
                    set(ps::eq::paramIndex(ps::eq::lowCut, ps::eq::on), 1.0f);
                    set(ps::eq::paramIndex(ps::eq::lowCut, ps::eq::frequency), 45.0f);
                    set(ps::eq::paramIndex(ps::eq::band1, ps::eq::gain), 3.0f);
                    set(ps::eq::paramIndex(ps::eq::band3, ps::eq::frequency), 900.0f);
                    set(ps::eq::paramIndex(ps::eq::band3, ps::eq::gain), -5.0f);
                    set(ps::eq::paramIndex(ps::eq::band3, ps::eq::q), 2.5f);
                    set(ps::eq::paramIndex(ps::eq::band4, ps::eq::gain), 2.5f);
                }
                else if (module.type == ps::types::compressor)
                {
                    set(ps::compressor::threshold, -26.0f);
                    set(ps::compressor::ratio, 4.0f);
                    set(ps::compressor::autoMakeup, 1.0f);
                }
                else if (module.type == ps::types::gate)
                    set(ps::gate::threshold, -45.0f);
                else if (module.type == ps::types::deesser)
                    set(ps::deesser::threshold, -42.0f);
                else if (module.type == ps::types::saturation)
                    set(ps::saturation::drive, 12.0f);
                else if (module.type == ps::types::limiter)
                    set(ps::limiter::input, 6.0f);
            }
        mainWindow_->showPanel("editor");
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
    bool firstRun_ = false;
    std::unique_ptr<UserSettings> settings_;
    std::unique_ptr<audio::AudioDeviceLayer> audio_;
    std::unique_ptr<ProjectSession> session_;
    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace aural

START_JUCE_APPLICATION(aural::AuralApplication)
