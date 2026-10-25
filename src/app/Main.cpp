#include "CrashReports.h"
#include "DemoSong.h"
#include "MainWindow.h"
#include "Migration.h"
#include "ProjectSession.h"
#include "UserSettings.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Command.h"
#include "core/Comping.h"
#include "core/ProjectJson.h"
#include "core/TempoMap.h"
#include "engine/OutputTap.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/FxTracks.h"
#include "plugins/StripPresets.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/OnScreen.h"
#include "ui/PianoRoll.h"
#include "ui/PreferencesView.h"
#include "ui/SplashScreen.h"
#include "ui/strip/ModuleCard.h"
#include "version/Version.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace northlight
{

class NorthlightApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    // The full version with its commit (version/Version.h): what crash reports and
    // bug reports need. The About box and splash show version::display().
    const juce::String getApplicationVersion() override { return juce::String(version::full()); }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        // --capture keeps its settings in a folder of its own, emptied each run, so a
        // capture always shows the default layout and the built-in plugins, and leaves
        // the user's settings alone.
        if (commandLine.contains("--capture"))
        {
            capture_ = true;
            UserSettings::useFolder("Northlight Capture");
            const auto folder = UserSettings().file().getParentDirectory();
            if (folder.getFileName() == "Northlight Capture")
                folder.deleteRecursively();
        }
        // The very first launch (no settings file yet) gets the device sheet.
        // Northlight was called Aural: bring its settings and folders across once.
        migration::fromAural();
        firstRun_ = !UserSettings().file().existsAsFile();
        settings_ = std::make_unique<UserSettings>();
        // A crash from here on leaves a report in the settings folder.
        crash::install(settings_->file().getSiblingFile("Crashes").getFullPathName().toWideCharPointer(),
                       getApplicationVersion().toStdString());
        // The design tokens: shipped beside the app, with the user's tokens.json
        // beside the settings file applied on top.
        ui::Theme::initialise(settings_->file().getSiblingFile("tokens.json"));
        lookAndFeel_ = std::make_unique<ui::NorthlightLookAndFeel>(ui::Theme::dark());
        juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel_.get());

        // The splash, once the theme is loaded; it reports each step below.
        // --offscreen: no window of any kind reaches the screen (no splash either).
        ui::offscreenMode() = commandLine.contains("--offscreen");
        juce::Component::SafePointer<ui::SplashScreen> splash;
        if (!commandLine.contains("--no-splash") && !ui::offscreenMode())
            splash = ui::SplashScreen::showStartup(ui::Theme::dark(),
                                                   juce::String::fromUTF8(version::display().c_str()));
        const auto step = [&splash](const juce::String& text, float progress)
        {
            if (splash != nullptr)
                splash->setStatus(text, progress);
        };

        step("Preparing the audio engine", 0.25f);
        audio_ = std::make_unique<audio::AudioDeviceLayer>(*settings_);
        if (capture_)
            audio_->callback().setOutputMuted(true); // a capture plays but is not heard
        step("Loading the plugin list", 0.45f);
        session_ =
            std::make_unique<ProjectSession>(*audio_, settings_->file().getSiblingFile("plugins.json"));
        session_->setAutosave(settings_->file().getSiblingFile("Autosave"),
                              [this]
                              {
                                  const auto minutes =
                                      settings_->value(ui::PreferencesView::Keys::autosaveMinutes);
                                  return (minutes.empty() ? 2 : juce::String(minutes).getIntValue()) * 60;
                              });
        step("Opening the main window", 0.65f);
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *audio_, *session_, *settings_);

        // Open the device after the window is up so a slow driver does not delay
        // the first paint. The session is already attached to the callback.
        step("Opening the audio device", 0.85f);
        audio_->initialise();
        if (splash != nullptr)
            splash->finish();

        // Recordings interrupted by a crash are offered back as tracks.
        mainWindow_->offerRecovery();

        applyLaunchFlags(commandLine);
        if ((firstRun_ && !capture_) || commandLine.contains("--first-run"))
            mainWindow_->showFirstRun();
    }

    // Developer conveniences for unattended checks:
    //   --audio-setup        open the Audio Setup window
    //   --about              open the About box (the splash screen, until clicked)
    //   --no-splash          start without the splash screen
    //   --offscreen          run with no window on screen at all (no splash, nothing that can
    //                        take the focus); windows still lay out and paint, so --capture
    //                        works while someone is using the machine
    //   --import <file>      import an audio file onto a new track (repeatable)
    //   --play               start the transport
    //   --record <n>         create n armed tracks on inputs 1..n and start recording
    //   --stop-after <s>     stop the transport after s seconds
    //   --quit-after <s>     quit the app after s seconds
    //   --buffer-size <n>    run the audio device at n samples for this run only (the saved
    //                        settings are not changed); prints the device it opened
    //   --loop <from> <to>   set and enable the loop region, in seconds
    //   --no-count-in        record without the count-in
    //   --layout <Mix|Edit>  apply a layout preset
    //   --demo-song <file>   write the demo song (Northern Drift) to the file and open it
    //   --time-load <file>   open the file, print how long each stage of the load took, quit
    //   --show-loading <file> [stage]  open the file as File > Open does, with the loading
    //                        sheet, and hold the load once it reaches the stage (reading,
    //                        media, plugins, engine; media if not given) for --capture
    //   --instrument-editor  open the first instrument's editor window
    //   --insert-editor [type]  open the editor window of the first insert (of that type, if
    //                        given) on the tracks, else on the master
    //   --arrange-tool <name>  pick the arrange view's tool: select, range, pencil, erase,
    //                        split, glue, mute
    //   --arrange-pointer <track> <bar> [shift|alt]  rest the pointer on a track row (0-based)
    //                        at a bar (fractions allowed), with Shift or Alt held if given,
    //                        once the window has settled (the split line, the glue preview)
    //   --mute-clips         mute the first audio clip and the first MIDI clip
    //   --clip-fades <in s> <out s> <in curve> <out curve>  give the first audio clip
    //                        fades of those lengths and curves (-1 slow to 1 fast)
    //   --show-menu <name>   open a menu as a click would: project, add-track, clip (the
    //                        first clip's)
    //   --editor-gap <n>     show the Editor panel's nth gap button (1-based), as the pointer in
    //                        that gap between two insert cards would
    //   --panel <name>       show a panel: mixer, editor, instruments, pianoRoll, diagnostics, browser,
    //   inspector
    //   --preferences [page] open Preferences (general, audio, recording, editing, plugins, shortcuts,
    //   diagnostics)
    //   --benchmark-strip-drag <s>  drag the first track's strip EQ for s seconds, print the
    //                        strip's display tick intervals, quit
    //   --benchmark-ui <s>   play and scroll for s seconds, print frame percentiles, quit
    //   --benchmark-hover <s>  hover the open editor windows' knobs (else the Editor
    //                        panel's), one a frame, for s seconds; print the repaint
    //                        times, quit
    //   --benchmark-panels <n>  open and close the inspector n times with the Editor
    //                        docked, print the slide's frame intervals, quit
    //   --insert <type>      add a built-in insert (northlight.glue, ...) to the first track that
    //                        makes audio
    //   --master-insert <type>  add a built-in insert to the master
    //   --corona-page <amp|cabinet>  the page Corona's cards open on (give it before --insert)
    //   --insert-param <name> <value>  set a parameter of the insert the last --insert or
    //                        --master-insert added, by its name or index (a stepped one also
    //                        by its choice's text: --insert-param Release Auto)
    //   --insert-param-at <s> <name> <value>  the same, s seconds after launch (a tail frozen
    //                        mid-play: --insert-param-at 4 Freeze 1)
    //   --bus                add a bus
    //   --remove-bus         remove the last bus the way the mixer does
    //   --first-run          show the first-run sheet
    //   --export             show the export sheet
    //   --midi-track         add a MIDI track (built-in synth) with a short clip
    //   --midi-track-with <type>  the same with the given instrument type instead
    //   --piano-roll         open the piano roll on the first MIDI clip
    //   --sampler <file> [demo]  add a Sampler instrument playing the file (a MIDI track with a
    //                        short clip) and open its editor; "demo" also sets a loop, a key
    //                        range and the filter so every display has something to show
    //   --import-midi <file> import a Standard MIDI File at the start (new instrument tracks)
    //   --export-midi <file> write every MIDI clip to a Standard MIDI File (after the flags
    //                        before it have run)
    //   --key <root> [minor] set the project key: root a pitch class 0 (C)..11 (B), major
    //                        unless "minor" follows
    //   --cc-lane <lane>     the same, showing a controller lane (bend, mod, expression,
    //                        sustain, pressure, cc<n>); a clip with no events in it gets a
    //                        demonstration curve first
    //   --audio-editor [pitch]  open the audio editor on the first audio clip (in pitch mode)
    //   --audio-editor-zoom <samples per pixel> [seconds]  zoom that editor, with the given
    //                        time into the clip at the left edge (default 0)
    //   --select-clip        select the first audio clip (the inspector shows it)
    //   --correct-pitch      once that clip's pitch is analysed, correct it to the key
    //   --mute-breaths       once that clip's pitch is analysed, mute its breaths
    //   --browser-files [folder]  show the Browser panel's Files tab (at the folder)
    //   --automation         automation lanes on the first track that makes sound, open in
    //                        the arrange view: volume (a swell), pan (collapsed), an empty
    //                        lane for the first strip module's first parameter, and a hidden
    //                        lane for its second
    //   --tempo-demo         a tempo track with a change, a ramp and a 3/4 bar, shown
    //   --freeze <n>         freeze track n (1-based) in the background
    //   --folder <n> <m>     put tracks n..m (1-based) in a new folder track
    //   --stretch <factor> [semitones]  stretch (and transpose) the first audio clip
    //   --takes-demo <files> a "Vocal" track with the files as takes in one take group, comped
    //                        from each in turn, its take lanes expanded
    //   --master-gain <dB>   set the master fader (-120 = silent: an unattended check that
    //                        should not be heard; pre-fader displays still move)
    //   --output-tap <folder>  record what the callback hands the driver to output.wav in the
    //                        folder (created if needed) until quit, with events.csv (clicks
    //                        found in it, late callbacks, overruns) and summary.txt. For
    //                        telling clicks Northlight makes from ones the driver or system
    //                        makes; the Diagnostics panel shows its counts while it runs
    //   --fx-track <reverb|delay|empty>  add an FX track with a send from every track
    //   --strip-preset <name> apply a factory channel strip preset to every track
    //   --strip <modules>    switch channel strip modules on for every track and show the
    //                        Editor panel: a comma list of gate, eq, compressor, deesser,
    //                        saturation, limiter, or all; "opto" swaps the compressor for
    //                        the opto compressor and switches it on; "demo" also sets
    //                        values that give every display something to show
    //   --window-size <w>x<h>  set the window's size
    //   --capture <folder> [s]  after s seconds (2 if not given), save the window and its
    //                        showing parts (mixer, channel-strip, instrument, piano-roll,
    //                        audio-editor; with --preferences, every Preferences page as
    //                        preferences-<page>; any other open window, such as an insert's
    //                        editor, as window-<name>) as PNGs in the folder, then quit. Uses its own
    //                        settings folder, so the layout is the default one, and sends
    //                        silence to the device, so playback is not heard
    //   --capture-scale <n>  the scale the captures are saved at (2 if not given)
    // --cc-lane: the first MIDI clip in the piano roll with the lane showing; a
    // clip with nothing in that lane gets a curve (a swell, or a bend up and back).
    void showControllerLane(const juce::String& key)
    {
        const auto lane = ui::PianoRoll::laneForKey(key);
        for (const auto& track : session_->project().tracks)
        {
            if (track.midiClips.empty())
                continue;
            const auto& clip = track.midiClips.front();
            const bool empty = std::none_of(clip.controllers.begin(),
                                            clip.controllers.end(),
                                            [&](const core::MidiControllerEvent& e)
                                            { return e.sameLane(lane.type, lane.number); });
            if (!lane.velocity && empty && clip.length > 0)
            {
                std::vector<core::MidiControllerEvent> curve;
                const int steps = 32;
                const int low = core::midiControllerMinimum(lane.type),
                          high = core::midiControllerMaximum(lane.type);
                for (int s = 0; s < steps; ++s)
                {
                    const double t = static_cast<double>(s) / steps;
                    const double shape = lane.type == core::MidiControllerType::pitchBend
                                             ? std::sin(t * 2.0 * 3.14159265358979) * 0.5 + 0.5
                                             : t;
                    curve.push_back(
                        {core::invalidId,
                         lane.type,
                         lane.number,
                         low + static_cast<int>(std::lround(shape * (high - low))),
                         static_cast<core::SamplePosition>(t * static_cast<double>(clip.length))});
                }
                session_->execute(std::make_unique<core::EditMidiControllers>(
                    track.id, clip.id, std::move(curve), std::vector<core::Id>{}, "Draw controller"));
            }
            mainWindow_->openPianoRoll(track.id, clip.id);
            mainWindow_->setPianoRollLane(key);
            return;
        }
    }

    // Runs `action` once the first audio clip has a vocal edit (its analysis is in),
    // checking twice a second up to `tries` times.
    void whenVocalReady(std::function<void()> action, int tries)
    {
        juce::Timer::callAfterDelay(500,
                                    [this, action, tries]
                                    {
                                        for (const auto& track : session_->project().tracks)
                                            if (!track.clips.empty() && track.clips.front().vocal.active())
                                            {
                                                action();
                                                return;
                                            }
                                        if (tries > 0)
                                            whenVocalReady(action, tries - 1);
                                    });
    }

    void applyLaunchFlags(const juce::String& commandLine)
    {
        const auto tokens = juce::ArgumentList("Northlight", commandLine).arguments;
        int stopAfterSeconds = 0;
        for (int i = 0; i < tokens.size(); ++i)
        {
            const auto& arg = tokens.getReference(i);
            if (arg == "--audio-setup")
                mainWindow_->showAudioSetup();
            else if (arg == "--about")
                mainWindow_->showAbout();
            else if (arg == "--open" && i + 1 < tokens.size())
                session_->open(tokens.getReference(++i).resolveAsFile());
            else if (arg == "--demo-song" && i + 1 < tokens.size())
            {
                // Writes the demo song to the file and opens it.
                const auto file = tokens.getReference(++i).resolveAsFile();
                file.getParentDirectory().createDirectory();
                const plugins::BuiltInParameterKeys builtInKeys;
                if (file.replaceWithText(
                        juce::String(core::projectToJson(makeDemoSong(), true, &builtInKeys))))
                    session_->open(file);
            }
            else if (arg == "--time-load" && i + 1 < tokens.size())
            {
                const auto file = tokens.getReference(++i).resolveAsFile();
                const auto error = session_->open(file);
                const auto& t = session_->lastLoadTimings();
                if (!error.empty())
                    std::printf("--time-load: %s\n", error.c_str());
                else
                    std::printf(
                        "load %s: read %.0f ms, media %.0f ms (%d files), plugins %.0f ms (%d inserts, "
                        "%d hosted), engine %.0f ms, total %.0f ms\n",
                        file.getFileName().toRawUTF8(),
                        t.readMs,
                        t.mediaMs,
                        t.mediaFiles,
                        t.pluginsMs,
                        t.inserts,
                        t.hostedPlugins,
                        t.engineMs,
                        t.totalMs());
                std::fflush(stdout);
                juce::JUCEApplication::getInstance()->quit();
                return;
            }
            else if (arg == "--show-loading" && i + 1 < tokens.size())
            {
                const auto file = tokens.getReference(++i).resolveAsFile();
                juce::String stage = "media";
                if (i + 1 < tokens.size() && !tokens.getReference(i + 1).text.startsWith("--"))
                    stage = tokens.getReference(++i).text;
                mainWindow_->openWithProgress(file, stage);
            }
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
                // An instrument track (Aurora, or the given instrument type) with a
                // four-note clip on its MIDI track.
                const auto type = arg == "--midi-track-with"
                                      ? tokens.getReference(++i).text.unquoted().toStdString()
                                      : std::string{};
                const auto trackId = session_->addMidiTrack(type);
                const auto beat = core::tempoMapOf(session_->project()).positionAt(1.0);
                core::MidiClip clip;
                clip.name = "Riff";
                clip.start = 0;
                clip.length = beat * 4;
                for (int n = 0; n < 4; ++n)
                    clip.notes.push_back({core::invalidId, 60 + n * 2, 100, beat * n, beat / 2});
                session_->execute(std::make_unique<core::AddMidiClip>(trackId, clip));
            }
            else if (arg == "--sampler" && i + 1 < tokens.size())
            {
                const auto file = tokens.getReference(++i).resolveAsFile();
                const auto midiId = session_->addSamplerTrack(file);
                const auto beat = static_cast<core::SamplePosition>(
                    core::samplesPerBeat(session_->project().sampleRate,
                                         session_->project().tempoBpm,
                                         session_->project().timeSignature));
                core::MidiClip clip;
                clip.name = "Sampler";
                clip.length = beat * 4;
                for (int n = 0; n < 4; ++n)
                    clip.notes.push_back({core::invalidId, 60 + n * 4, 100, beat * n, beat});
                session_->execute(std::make_unique<core::AddMidiClip>(midiId, clip));
                const bool demo = i + 1 < tokens.size() && tokens.getReference(i + 1).text == "demo";
                if (demo)
                    ++i;
                if (const auto* midi = session_->project().findTrack(midiId))
                    if (const auto* channel = session_->project().findTrack(midi->midiTarget))
                    {
                        const auto channelId = channel->id, instrumentId = channel->instrument.id;
                        if (demo)
                        {
                            namespace sp = plugins::sampler;
                            const std::pair<int, float> values[] = {{sp::loop, 1.0f},
                                                                    {sp::loopStart, 0.3f},
                                                                    {sp::loopEnd, 0.62f},
                                                                    {sp::loopFade, 60.0f},
                                                                    {sp::end, 0.85f},
                                                                    {sp::keyLow, 36.0f},
                                                                    {sp::keyHigh, 84.0f},
                                                                    {sp::cutoff, 2500.0f},
                                                                    {sp::resonance, 0.4f},
                                                                    {sp::filterEnv, 2.0f},
                                                                    {sp::release, 600.0f}};
                            for (const auto& [index, value] : values)
                                session_->execute(std::make_unique<core::SetInsertParameter>(
                                    channelId, instrumentId, index, value));
                        }
                        mainWindow_->openInsertEditor(channelId, instrumentId);
                    }
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
            else if (arg == "--import-midi" && i + 1 < tokens.size())
            {
                const auto file = tokens.getReference(++i).resolveAsFile();
                if (const auto error = session_->importMidiFile(file, 0); !error.empty())
                    mainWindow_->notify(juce::String::fromUTF8(error.c_str()), true);
            }
            else if (arg == "--export-midi" && i + 1 < tokens.size())
            {
                std::vector<std::pair<core::Id, core::Id>> clips;
                for (const auto& track : session_->project().tracks)
                    for (const auto& clip : track.midiClips)
                        clips.emplace_back(track.id, clip.id);
                const auto file = tokens.getReference(++i).resolveAsFile();
                if (const auto error = session_->exportMidiFile(file, clips); !error.empty())
                    mainWindow_->notify(juce::String::fromUTF8(error.c_str()), true);
            }
            else if (arg == "--key" && i + 1 < tokens.size())
            {
                core::MusicalKey key;
                key.root = std::clamp(tokens.getReference(++i).text.getIntValue(), 0, 11);
                if (i + 1 < tokens.size() && tokens.getReference(i + 1).text == "minor")
                {
                    key.scale = core::ScaleKind::minor;
                    ++i;
                }
                session_->execute(std::make_unique<core::SetProjectKey>(key));
            }
            else if (arg == "--cc-lane" && i + 1 < tokens.size())
                showControllerLane(tokens.getReference(++i).text.unquoted());
            else if (arg == "--audio-editor")
            {
                const bool pitch = i + 1 < tokens.size() && tokens.getReference(i + 1).text == "pitch";
                if (pitch)
                    ++i;
                for (const auto& track : session_->project().tracks)
                    if (!track.clips.empty())
                    {
                        mainWindow_->openAudioEditor(track.id, track.clips.front().id, pitch);
                        break;
                    }
            }
            else if (arg == "--audio-editor-zoom" && i + 1 < tokens.size())
            {
                const double samplesPerPixel = tokens.getReference(++i).text.getDoubleValue();
                double seconds = 0.0;
                if (i + 1 < tokens.size() && tokens.getReference(i + 1).text.containsOnly("0123456789."))
                    seconds = tokens.getReference(++i).text.getDoubleValue();
                mainWindow_->zoomAudioEditor(samplesPerPixel, seconds);
            }
            else if (arg == "--select-clip")
            {
                for (const auto& track : session_->project().tracks)
                    if (!track.clips.empty())
                    {
                        mainWindow_->selectClip(track.id, track.clips.front().id);
                        break;
                    }
            }
            else if (arg == "--correct-pitch" || arg == "--mute-breaths")
                whenVocalReady(arg == "--correct-pitch"
                                   ? [this] { mainWindow_->correctPitchInEditor(); }
                                   : std::function<void()>([this] { mainWindow_->muteBreathsInEditor(); }),
                               60);
            else if (arg == "--quit-after" && i + 1 < tokens.size())
            {
                const int seconds = tokens.getReference(++i).text.getIntValue();
                juce::Timer::callAfterDelay(std::max(0, seconds) * 1000,
                                            []
                                            { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
            }
            else if (arg == "--buffer-size" && i + 1 < tokens.size())
            {
                auto settings = audio_->settings();
                settings.bufferSize = tokens.getReference(++i).text.getIntValue();
                const auto error = audio_->applyForThisRun(settings);
                const auto s = audio_->status();
                std::printf("Audio: %s %s at %.0f Hz, %d samples%s%s\n",
                            s.deviceType.c_str(),
                            s.outputDevice.c_str(),
                            s.sampleRate,
                            s.bufferSize,
                            error ? ": " : "",
                            error ? error->c_str() : "");
                std::string inputs;
                for (const auto& name : audio_->inputChannelNames())
                    inputs += (inputs.empty() ? "" : ", ") + name;
                std::printf("Inputs: %s\n", inputs.c_str());
                std::fflush(stdout);
            }
            else if (arg == "--output-tap" && i + 1 < tokens.size())
            {
                const juce::File folder(tokens.getReference(++i).resolveAsFile());
                if (folder.createDirectory().wasOk() && outputTap_ == nullptr)
                {
                    outputTap_ = std::make_unique<engine::OutputTap>(folder.getFullPathName().toStdString());
                    audio_->callback().setOutputTap(outputTap_.get());
                    std::printf("Output tap recording to %s\n", folder.getFullPathName().toRawUTF8());
                }
            }
            else if (arg == "--master-gain" && i + 1 < tokens.size())
            {
                const float db = tokens.getReference(++i).text.getFloatValue();
                session_->execute(std::make_unique<core::SetMasterGain>(
                    db <= -120.0f ? 0.0f : std::pow(10.0f, db / 20.0f)));
            }
            else if (arg == "--takes-demo")
            {
                // A track whose take group holds the files that follow as takes (all
                // from zero), comped from each of them in turn, its lanes expanded.
                std::vector<core::AudioClip> passes;
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                const double rate = session_->project().sampleRate;
                while (i + 1 < tokens.size() && !tokens.getReference(i + 1).text.startsWith("--"))
                {
                    const auto file = tokens.getReference(++i).resolveAsFile();
                    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
                    if (reader == nullptr || reader->sampleRate <= 0.0)
                        continue;
                    core::AudioClip clip;
                    clip.mediaPath = file.getFullPathName().toStdString();
                    clip.length = static_cast<core::SamplePosition>(
                        static_cast<double>(reader->lengthInSamples) * rate / reader->sampleRate);
                    passes.push_back(std::move(clip));
                }
                if (!passes.empty())
                {
                    core::Track track;
                    track.name = "Vocal";
                    session_->execute(std::make_unique<core::AddTrack>(track));
                    const auto trackId = session_->project().tracks.back().id;
                    const auto length = passes.front().length;
                    session_->execute(std::make_unique<core::RecordTakes>(trackId, passes));
                    const auto& group = session_->project().tracks.back().takeGroups.front();
                    const auto groupId = group.id;
                    std::vector<core::Id> takes;
                    for (const auto& take : group.takes)
                        takes.push_back(take.id());
                    for (size_t k = 0; k + 1 < takes.size(); ++k)
                        session_->execute(std::make_unique<core::SelectTake>(
                            trackId,
                            groupId,
                            takes[k],
                            length * static_cast<core::SamplePosition>(k + 1) /
                                static_cast<core::SamplePosition>(takes.size() + 1),
                            length * static_cast<core::SamplePosition>(k + 2) /
                                static_cast<core::SamplePosition>(takes.size() + 1)));
                    session_->execute(std::make_unique<core::SetTakeLanesExpanded>(trackId, groupId, true));
                }
            }
            else if (arg == "--automation")
            {
                // Lanes on the first track in each of their states (adding a lane opens them).
                const auto& tracks = session_->project().tracks;
                const auto sounding = std::find_if(
                    tracks.begin(), tracks.end(), [](const core::Track& t) { return t.hasAudio(); });
                if (sounding != tracks.end())
                {
                    const auto& track = *sounding;
                    const auto trackId = track.id;
                    const double rate = session_->project().sampleRate;
                    const auto at = [rate](double seconds)
                    { return static_cast<core::SamplePosition>(seconds * rate); };
                    core::AutomationLane volume;
                    volume.target = core::AutomationTarget::gain;
                    const float values[] = {1.0f, 0.3f, 0.3f, 1.0f, 0.6f, 1.4f, 0.0f};
                    for (int p = 0; p < 7; ++p)
                        volume.points.push_back({at(p * 2.5), values[p]});
                    volume.view.height = 64;
                    core::AutomationLane pan;
                    pan.target = core::AutomationTarget::pan;
                    pan.points = {
                        {0, 0.0f}, {at(4.0), 0.0f}, {at(5.0), -0.6f}, {at(8.0), 0.6f}, {at(9.0), 0.0f}};
                    pan.view.collapsed = true;
                    std::vector<core::AutomationLane> lanes{volume, pan};
                    if (!track.strip.empty() && !track.strip.front().params.empty())
                    {
                        core::AutomationLane empty;
                        empty.target = core::AutomationTarget::insertParameter;
                        empty.insert = track.strip.front().id;
                        lanes.push_back(empty);
                        if (track.strip.front().params.size() > 1)
                        {
                            core::AutomationLane hidden = empty;
                            hidden.parameter = 1;
                            hidden.points = {{0, track.strip.front().params[1]}};
                            hidden.view.hidden = true;
                            lanes.push_back(hidden);
                        }
                    }
                    for (const auto& lane : lanes)
                        session_->execute(std::make_unique<core::AddAutomationLane>(trackId, lane));
                    mainWindow_->showAutomation(trackId);
                }
            }
            else if (arg == "--fx-track" && i + 1 < tokens.size())
            {
                // An FX track with the effect (reverb, delay, empty) and a send from every track.
                const auto wanted = tokens.getReference(++i).text.unquoted().toLowerCase();
                for (const auto& choice : plugins::fxTrackEffects())
                    if (juce::String(choice.name).toLowerCase() == wanted)
                    {
                        std::vector<core::Id> senders;
                        for (const auto& t : session_->project().tracks)
                            senders.push_back(t.id);
                        session_->executeGroup("Add FX track",
                                               plugins::addFxTrackCommands(
                                                   session_->project(), choice.type, choice.name, senders));
                    }
            }
            else if (arg == "--strip-preset" && i + 1 < tokens.size())
            {
                // A factory strip preset on every track, and the Editor panel.
                const auto name = tokens.getReference(++i).text.unquoted().toStdString();
                if (const auto* preset = plugins::strip::findFactoryPreset(name))
                {
                    const auto tracks = session_->project().tracks;
                    for (const auto& track : tracks)
                        session_->executeGroup(
                            "Strip preset: " + name,
                            plugins::strip::presetCommands(track, preset->modules, "Strip preset: " + name));
                }
                mainWindow_->showPanel("editor");
            }
            else if (arg == "--strip" && i + 1 < tokens.size())
                applyStripFlag(tokens.getReference(++i).text.unquoted());
            else if ((arg == "--insert" || arg == "--master-insert") && i + 1 < tokens.size())
            {
                // A built-in insert on the master, or on the first track that makes
                // audio (the one the Editor panel shows at launch).
                const auto type = tokens.getReference(++i).text.unquoted().toStdString();
                core::Id channel = arg == "--master-insert" ? core::masterId : core::invalidId;
                if (channel == core::invalidId)
                    for (const auto& t : session_->project().tracks)
                        if (t.hasAudio())
                        {
                            channel = t.id;
                            break;
                        }
                if (channel != core::invalidId)
                {
                    core::Insert insert;
                    insert.type = type;
                    insert.params = plugins::defaultParameters(type);
                    session_->execute(std::make_unique<core::AddInsert>(channel, insert));
                    if (const auto* c = session_->project().findChannel(channel);
                        c != nullptr && !c->inserts.empty())
                    {
                        lastInsertChannel_ = channel;
                        lastInsertId_ = c->inserts.back().id;
                    }
                }
            }
            else if ((arg == "--insert-param" && i + 2 < tokens.size()) ||
                     (arg == "--insert-param-at" && i + 3 < tokens.size()))
            {
                // A parameter of the insert the last --insert or --master-insert added,
                // by name (case ignored) or index; a stepped one also takes its choice's
                // text. The -at form sets it that many seconds after launch (a tail
                // frozen mid-play, for a capture).
                const double after =
                    arg == "--insert-param-at" ? tokens.getReference(++i).text.getDoubleValue() : 0.0;
                const auto name = tokens.getReference(++i).text.unquoted();
                const auto value = tokens.getReference(++i).text.unquoted();
                const auto set = [this, name, value, channel = lastInsertChannel_, id = lastInsertId_]
                {
                    const auto* c = session_->project().findChannel(channel);
                    const auto* insert = c != nullptr ? c->findInsert(id) : nullptr;
                    if (insert == nullptr)
                    {
                        std::fprintf(stderr, "--insert-param: no insert added yet\n");
                        return;
                    }
                    const auto& infos = plugins::parameterInfo(insert->type);
                    int index =
                        name.containsOnly("0123456789") && name.isNotEmpty() ? name.getIntValue() : -1;
                    if (index >= static_cast<int>(infos.size()))
                        index = -1;
                    for (size_t p = 0; p < infos.size(); ++p)
                        if (juce::String(infos[p].name).equalsIgnoreCase(name))
                            index = static_cast<int>(p);
                    if (index < 0)
                    {
                        std::fprintf(stderr,
                                     "--insert-param: %s has no parameter \"%s\"\n",
                                     insert->type.c_str(),
                                     name.toRawUTF8());
                        return;
                    }
                    const auto& info = infos[static_cast<size_t>(index)];
                    float v = value.getFloatValue();
                    for (size_t k = 0; k < info.choices.size(); ++k)
                        if (juce::String(info.choices[k]).equalsIgnoreCase(value))
                            v = static_cast<float>(k);
                    session_->execute(std::make_unique<core::SetInsertParameter>(
                        channel, id, index, std::clamp(v, info.minimum, info.maximum)));
                };
                if (after > 0.0)
                    juce::Timer::callAfterDelay(static_cast<int>(after * 1000.0), set);
                else
                    set();
            }
            else if (arg == "--corona-page" && i + 1 < tokens.size())
                ui::strip::setEmberStartPage(tokens.getReference(++i).text.unquoted() == "cabinet");
            else if (arg == "--bus")
                mainWindow_->addBus();
            else if (arg == "--remove-bus")
                mainWindow_->removeLastBus();
            else if (arg == "--mixer")
                mainWindow_->showMixer();
            else if (arg == "--freeze" && i + 1 < tokens.size())
            {
                const int n = tokens.getReference(++i).text.getIntValue() - 1;
                if (n >= 0 && n < static_cast<int>(session_->project().tracks.size()))
                    if (const auto error =
                            session_->freezeTrack(session_->project().tracks[static_cast<size_t>(n)].id);
                        !error.empty())
                        juce::Logger::writeToLog("--freeze: " + juce::String(error));
            }
            else if (arg == "--folder" && i + 2 < tokens.size())
            {
                const int from = tokens.getReference(++i).text.getIntValue() - 1;
                const int to = tokens.getReference(++i).text.getIntValue() - 1;
                std::vector<core::Id> members;
                for (int t = std::max(0, from);
                     t <= to && t < static_cast<int>(session_->project().tracks.size());
                     ++t)
                    members.push_back(session_->project().tracks[static_cast<size_t>(t)].id);
                session_->execute(std::make_unique<core::AddFolderTrack>("Folder", members));
            }
            else if (arg == "--tempo-demo")
            {
                core::TempoState state = core::TempoState::of(session_->project());
                state.tempoChanges = {{16.0, 140.0, false}, {32.0, 90.0, true}};
                state.meterChanges = {{10, {3, 4}}, {12, {4, 4}}};
                session_->execute(std::make_unique<core::SetTempoMap>(state));
                mainWindow_->showTempoTrack();
            }
            else if (arg == "--stretch" && i + 1 < tokens.size())
            {
                const double factor = tokens.getReference(++i).text.getDoubleValue();
                double semitones = 0.0;
                if (i + 1 < tokens.size() && !tokens.getReference(i + 1).text.startsWith("--"))
                    semitones = tokens.getReference(++i).text.getDoubleValue();
                for (const auto& track : session_->project().tracks)
                    if (!track.clips.empty() && factor > 0.0)
                    {
                        const auto& clip = track.clips.front();
                        session_->execute(std::make_unique<core::SetClipStretch>(
                            track.id,
                            clip.id,
                            clip.stretch * factor,
                            semitones,
                            static_cast<core::SamplePosition>(
                                std::llround(static_cast<double>(clip.length) * factor)),
                            false));
                        break;
                    }
            }
            else if (arg == "--layout" && i + 1 < tokens.size())
                mainWindow_->applyLayoutPreset(tokens.getReference(++i).text.unquoted());
            else if (arg == "--panel" && i + 1 < tokens.size())
                mainWindow_->showPanel(tokens.getReference(++i).text.unquoted());
            else if (arg == "--editor-gap" && i + 1 < tokens.size())
            {
                // After the window has settled and the cards are laid out.
                const int n = tokens.getReference(++i).text.getIntValue();
                juce::Component::SafePointer<MainWindow> window(mainWindow_.get());
                juce::Timer::callAfterDelay(1000,
                                            [window, n]
                                            {
                                                if (window != nullptr)
                                                    window->showEditorGap(n);
                                            });
            }
            else if (arg == "--arrange-tool" && i + 1 < tokens.size())
            {
                const auto name = tokens.getReference(++i).text;
                if (!mainWindow_->setArrangeTool(name))
                    std::fprintf(stderr, "--arrange-tool: no tool \"%s\"\n", name.toRawUTF8());
            }
            else if (arg == "--arrange-pointer" && i + 2 < tokens.size())
            {
                const int row = tokens.getReference(++i).text.getIntValue();
                const double bar = tokens.getReference(++i).text.getDoubleValue();
                juce::ModifierKeys mods;
                if (i + 1 < tokens.size() &&
                    (tokens.getReference(i + 1).text == "shift" || tokens.getReference(i + 1).text == "alt"))
                    mods = juce::ModifierKeys(tokens.getReference(++i).text == "shift"
                                                  ? juce::ModifierKeys::shiftModifier
                                                  : juce::ModifierKeys::altModifier);
                juce::Component::SafePointer<MainWindow> window(mainWindow_.get());
                juce::Timer::callAfterDelay(1500,
                                            [window, row, bar, mods]
                                            {
                                                if (window != nullptr)
                                                    window->arrangePointer(row, bar, mods);
                                            });
            }
            else if (arg == "--clip-fades" && i + 4 < tokens.size())
            {
                const double rate = session_->project().sampleRate;
                const auto in =
                    static_cast<core::SamplePosition>(tokens.getReference(++i).text.getDoubleValue() * rate);
                const auto out =
                    static_cast<core::SamplePosition>(tokens.getReference(++i).text.getDoubleValue() * rate);
                const float inCurve = tokens.getReference(++i).text.getFloatValue();
                const float outCurve = tokens.getReference(++i).text.getFloatValue();
                for (const auto& track : session_->project().tracks)
                    if (!track.clips.empty())
                    {
                        session_->execute(std::make_unique<core::SetClipFades>(
                            track.id, track.clips.front().id, in, out, inCurve, outCurve));
                        break;
                    }
            }
            else if (arg == "--mute-clips")
            {
                bool audio = false, midi = false;
                for (const auto& track : session_->project().tracks)
                {
                    if (!audio && !track.clips.empty())
                    {
                        session_->execute(std::make_unique<core::SetClipMute>(
                            track.id, track.clips.front().id, false, true));
                        audio = true;
                    }
                    if (!midi && !track.midiClips.empty())
                    {
                        session_->execute(std::make_unique<core::SetClipMute>(
                            track.id, track.midiClips.front().id, true, true));
                        midi = true;
                    }
                }
            }
            else if (arg == "--show-menu" && i + 1 < tokens.size())
            {
                // After the window has settled, so a capture can watch it open.
                const auto key = tokens.getReference(++i).text.unquoted();
                juce::Component::SafePointer<MainWindow> window(mainWindow_.get());
                juce::Timer::callAfterDelay(1500,
                                            [window, key]
                                            {
                                                if (window != nullptr)
                                                    window->showMenu(key);
                                            });
            }
            else if (arg == "--benchmark-strip-drag" && i + 1 < tokens.size())
                mainWindow_->runStripDragBenchmark(tokens.getReference(++i).text.getIntValue());
            else if (arg == "--benchmark-ui" && i + 1 < tokens.size())
                mainWindow_->runBenchmark(tokens.getReference(++i).text.getIntValue());
            else if (arg == "--benchmark-hover" && i + 1 < tokens.size())
                mainWindow_->runHoverBenchmark(tokens.getReference(++i).text.getIntValue());
            else if (arg == "--benchmark-panels" && i + 1 < tokens.size())
                mainWindow_->runPanelBenchmark(tokens.getReference(++i).text.getIntValue());
            else if (arg == "--export")
                mainWindow_->showExport();
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
                    if (!track.hasAudio())
                        continue; // a MIDI track has no audio to process
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
            else if (arg == "--insert-editor")
            {
                // With a type after it, the first insert of that type; else the first
                // insert. The tracks are searched first, then the master.
                std::string type;
                if (i + 1 < tokens.size() && !tokens[i + 1].text.startsWith("--"))
                    type = tokens.getReference(++i).text.unquoted().toStdString();
                const auto open = [&](const core::Channel& channel)
                {
                    for (const auto& insert : channel.inserts)
                        if (type.empty() || insert.type == type)
                        {
                            mainWindow_->openInsertEditor(channel.id, insert.id);
                            return true;
                        }
                    return false;
                };
                bool opened = false;
                for (const auto& track : session_->project().tracks)
                    if ((opened = open(track)))
                        break;
                if (!opened)
                    open(session_->project().master);
            }
            else if (arg == "--instrument-editor")
            {
                // Opens the first instrument's editor window.
                for (const auto& track : session_->project().tracks)
                    if (track.hasInstrument())
                    {
                        mainWindow_->openInsertEditor(track.id, track.instrument.id);
                        break;
                    }
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
            else if (arg == "--browser-files")
                mainWindow_->showFilesBrowser(i + 1 < tokens.size() && !tokens[i + 1].text.startsWith("--")
                                                  ? tokens.getReference(++i).resolveAsFile()
                                                  : juce::File());
            else if (arg == "--marker" && i + 1 < tokens.size())
            {
                core::Marker marker;
                marker.position = static_cast<core::SamplePosition>(
                    tokens.getReference(++i).text.getDoubleValue() * session_->project().sampleRate);
                marker.name = "Marker " + std::to_string(session_->project().markers.size() + 1);
                session_->execute(std::make_unique<core::AddMarker>(marker));
            }
            else if (arg == "--window-size" && i + 1 < tokens.size())
            {
                const auto size = tokens.getReference(++i).text.unquoted();
                const int width = size.upToFirstOccurrenceOf("x", false, true).getIntValue();
                const int height = size.fromFirstOccurrenceOf("x", false, true).getIntValue();
                if (width > 0 && height > 0)
                    mainWindow_->setSize(width, height);
            }
            else if (arg == "--capture-scale" && i + 1 < tokens.size())
                captureScale_ = juce::jlimit(1.0f, 4.0f, tokens.getReference(++i).text.getFloatValue());
            else if (arg == "--capture" && i + 1 < tokens.size())
            {
                const auto folder = tokens.getReference(++i).resolveAsFile();
                int seconds = 2;
                if (i + 1 < tokens.size() && tokens[i + 1].text.containsOnly("0123456789"))
                    seconds = tokens.getReference(++i).text.getIntValue();
                juce::Timer::callAfterDelay(seconds * 1000,
                                            [this, folder]
                                            {
                                                if (mainWindow_ != nullptr)
                                                    mainWindow_->capture(folder, captureScale_);
                                                quit();
                                            });
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
        if (names.contains("opto"))
            for (const auto& track : session_->project().tracks)
                for (const auto& module : track.strip)
                    if (module.type == ps::types::compressor)
                    {
                        session_->execute(std::make_unique<core::SetStripModuleType>(
                            track.id,
                            module.id,
                            ps::types::opto,
                            plugins::defaultParameters(ps::types::opto),
                            "Swap"));
                        break;
                    }
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
                else if (module.type == ps::types::opto)
                {
                    set(ps::opto::peakReduction, 60.0f);
                    set(ps::opto::gain, 4.0f);
                    set(ps::opto::colour, 45.0f);
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
        if (outputTap_ != nullptr)
        {
            // The device is stopped: no callback can reach the tap any more.
            audio_->callback().setOutputTap(nullptr);
            outputTap_->stop();
            outputTap_.reset();
        }
        session_.reset();
        audio_.reset();
        settings_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        lookAndFeel_.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<ui::NorthlightLookAndFeel> lookAndFeel_;
    bool firstRun_ = false;
    bool capture_ = false;      // --capture: its own settings folder, no first-run sheet
    float captureScale_ = 2.0f; // --capture-scale
    // --insert-param's target: the insert the last --insert or --master-insert added.
    core::Id lastInsertChannel_ = core::invalidId;
    core::Id lastInsertId_ = core::invalidId;
    std::unique_ptr<UserSettings> settings_;
    std::unique_ptr<audio::AudioDeviceLayer> audio_;
    std::unique_ptr<engine::OutputTap> outputTap_; // --output-tap
    std::unique_ptr<ProjectSession> session_;
    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace northlight

START_JUCE_APPLICATION(northlight::NorthlightApplication)
