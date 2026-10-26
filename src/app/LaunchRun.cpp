#include "app/LaunchRun.h"

#include "app/DemoSong.h"
#include "app/MainWindow.h"
#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Command.h"
#include "core/Comping.h"
#include "core/ProjectJson.h"
#include "core/TempoMap.h"
#include "engine/Gain.h"
#include "engine/OutputTap.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"
#include "plugins/FxTracks.h"
#include "plugins/Sampler.h"
#include "plugins/StripPresets.h"
#include "ui/PianoRoll.h"
#include "ui/strip/ModuleCard.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace northlight
{

namespace
{
using Args = LaunchArguments;
using Flag = LaunchFlag<LaunchRun>;

void quitApp()
{
    juce::JUCEApplication::getInstance()->quit();
}

// The first track with an audio clip, and that clip (both null for none).
template <typename F> void onFirstAudioClip(ProjectSession& session, F&& f)
{
    for (const auto& track : session.project().tracks)
        if (!track.clips.empty())
        {
            f(track, track.clips.front());
            return;
        }
}

void timeLoad(LaunchRun& r, Args& a)
{
    const auto file = a.file();
    const auto error = r.session.open(file);
    const auto& t = r.session.lastLoadTimings();
    if (!error.empty())
        std::printf("--time-load: %s\n", error.c_str());
    else
        std::printf("load %s: read %.0f ms, media %.0f ms (%d files), plugins %.0f ms (%d inserts, "
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
    quitApp();
    r.stopped = true;
}

void record(LaunchRun& r, Args& a)
{
    const int inputs = a.integer();
    for (int input = 0; input < inputs; ++input)
    {
        core::Track track;
        track.name = "Input " + std::to_string(input + 1);
        track.inputChannel = input;
        track.armed = true;
        r.session.execute(std::make_unique<core::AddTrack>(track));
    }
    // The device opens asynchronously from the UI's point of view; give it a
    // moment before recording.
    r.schedule.after(LaunchSchedule::settledMs, [&r] { r.session.startRecording(); });
}

// An instrument track (Aurora, or the given instrument type) with a four-note
// clip on its MIDI track.
void midiTrack(LaunchRun& r, const std::string& type)
{
    const auto trackId = r.session.addMidiTrack(type);
    const auto beat = core::tempoMapOf(r.session.project()).positionAt(1.0);
    core::MidiClip clip;
    clip.name = "Riff";
    clip.start = 0;
    clip.length = beat * 4;
    for (int n = 0; n < 4; ++n)
        clip.notes.push_back({core::invalidId, 60 + n * 2, 100, beat * n, beat / 2});
    r.session.execute(std::make_unique<core::AddMidiClip>(trackId, clip));
}

void sampler(LaunchRun& r, Args& a)
{
    const auto file = a.file();
    const auto midiId = r.session.addSamplerTrack(file);
    const auto beat = core::tempoMapOf(r.session.project()).positionAt(1.0); // a quarter note
    core::MidiClip clip;
    clip.name = "Sampler";
    clip.length = beat * 4;
    for (int n = 0; n < 4; ++n)
        clip.notes.push_back({core::invalidId, 60 + n * 4, 100, beat * n, beat});
    r.session.execute(std::make_unique<core::AddMidiClip>(midiId, clip));
    const bool demo = a.word("demo");
    const auto* midi = r.session.project().findTrack(midiId);
    const auto* channel = midi != nullptr ? r.session.project().findTrack(midi->midiTarget) : nullptr;
    if (channel == nullptr)
        return;
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
            r.session.execute(
                std::make_unique<core::SetInsertParameter>(channelId, instrumentId, index, value));
    }
    r.window.openInsertEditor(channelId, instrumentId);
}

void bufferSize(LaunchRun& r, Args& a)
{
    auto settings = r.audio.settings();
    settings.bufferSize = a.integer();
    const auto error = r.audio.applyForThisRun(settings);
    const auto s = r.audio.status();
    std::printf("Audio: %s %s at %.0f Hz, %d samples%s%s\n",
                s.deviceType.c_str(),
                s.outputDevice.c_str(),
                s.sampleRate,
                s.bufferSize,
                error ? ": " : "",
                error ? error->c_str() : "");
    std::string inputs;
    for (const auto& name : r.audio.inputChannelNames())
        inputs += (inputs.empty() ? "" : ", ") + name;
    std::printf("Inputs: %s\n", inputs.c_str());
    std::fflush(stdout);
}

void startOutputTap(LaunchRun& r, Args& a)
{
    const juce::File folder(a.file());
    if (folder.createDirectory().wasOk() && r.outputTap == nullptr)
    {
        r.outputTap = std::make_unique<engine::OutputTap>(folder.getFullPathName().toStdString());
        r.audio.callback().setOutputTap(r.outputTap.get());
        std::printf("Output tap recording to %s\n", folder.getFullPathName().toRawUTF8());
    }
}

// A track whose take group holds the files that follow as takes (all from zero),
// comped from each of them in turn, its lanes expanded.
void takesDemo(LaunchRun& r, Args& a)
{
    std::vector<core::AudioClip> passes;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const double rate = r.session.project().sampleRate;
    while (const auto* arg = a.value())
    {
        const auto file = arg->resolveAsFile();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            continue;
        core::AudioClip clip;
        clip.mediaPath = file.getFullPathName().toStdString();
        clip.length = static_cast<core::SamplePosition>(static_cast<double>(reader->lengthInSamples) * rate /
                                                        reader->sampleRate);
        passes.push_back(std::move(clip));
    }
    if (passes.empty())
        return;
    core::Track track;
    track.name = "Vocal";
    r.session.execute(std::make_unique<core::AddTrack>(track));
    const auto trackId = r.session.project().tracks.back().id;
    const auto length = passes.front().length;
    r.session.execute(std::make_unique<core::RecordTakes>(trackId, passes));
    const auto& group = r.session.project().tracks.back().takeGroups.front();
    const auto groupId = group.id;
    std::vector<core::Id> takes;
    for (const auto& take : group.takes)
        takes.push_back(take.id());
    for (size_t k = 0; k + 1 < takes.size(); ++k)
        r.session.execute(
            std::make_unique<core::SelectTake>(trackId,
                                               groupId,
                                               takes[k],
                                               length * static_cast<core::SamplePosition>(k + 1) /
                                                   static_cast<core::SamplePosition>(takes.size() + 1),
                                               length * static_cast<core::SamplePosition>(k + 2) /
                                                   static_cast<core::SamplePosition>(takes.size() + 1)));
    r.session.execute(std::make_unique<core::SetTakeLanesExpanded>(trackId, groupId, true));
}

// Lanes on the first track that makes sound, in each of their states (adding a
// lane opens them).
void automation(LaunchRun& r, Args&)
{
    const auto& tracks = r.session.project().tracks;
    const auto sounding =
        std::find_if(tracks.begin(), tracks.end(), [](const core::Track& t) { return t.hasAudio(); });
    if (sounding == tracks.end())
        return;
    const auto& track = *sounding;
    const auto trackId = track.id;
    const double rate = r.session.project().sampleRate;
    const auto at = [rate](double seconds) { return static_cast<core::SamplePosition>(seconds * rate); };
    core::AutomationLane volume;
    volume.target = core::AutomationTarget::gain;
    const float values[] = {1.0f, 0.3f, 0.3f, 1.0f, 0.6f, 1.4f, 0.0f};
    for (int p = 0; p < 7; ++p)
        volume.points.push_back({at(p * 2.5), values[p]});
    volume.view.height = 64;
    core::AutomationLane pan;
    pan.target = core::AutomationTarget::pan;
    pan.points = {{0, 0.0f}, {at(4.0), 0.0f}, {at(5.0), -0.6f}, {at(8.0), 0.6f}, {at(9.0), 0.0f}};
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
        r.session.execute(std::make_unique<core::AddAutomationLane>(trackId, lane));
    r.window.showAutomation(trackId);
}

// A built-in insert on the master, or on the first track that makes audio (the
// one the Editor panel shows at launch).
void insert(LaunchRun& r, Args& a, bool onMaster)
{
    const auto type = a.text().unquoted().toStdString();
    core::Id channel = onMaster ? core::masterId : core::invalidId;
    if (channel == core::invalidId)
        for (const auto& t : r.session.project().tracks)
            if (t.hasAudio())
            {
                channel = t.id;
                break;
            }
    if (channel == core::invalidId)
        return;
    core::Insert added;
    added.type = type;
    added.params = plugins::defaultParameters(type);
    r.session.execute(std::make_unique<core::AddInsert>(channel, added));
    if (const auto* c = r.session.project().findChannel(channel); c != nullptr && !c->inserts.empty())
    {
        r.lastInsertChannel = channel;
        r.lastInsertId = c->inserts.back().id;
    }
}

// The mixer's less common states at once, for a capture (--mixer-states).
void mixerStates(LaunchRun& r, Args&)
{
    std::vector<core::Id> tracks;
    for (const auto& t : r.session.project().tracks)
        if (t.hasAudio())
            tracks.push_back(t.id);
    if (!tracks.empty())
        r.session.execute(std::make_unique<core::SetSoloSafe>(tracks.front(), true));
    if (tracks.size() > 1)
        r.session.execute(std::make_unique<core::SetTrackSolo>(tracks[1], true));
    if (tracks.size() > 2)
        r.session.execute(std::make_unique<core::SetHardwareOutput>(tracks[2], 1));
    if (tracks.size() > 3)
        r.session.execute(std::make_unique<core::SetMixerHidden>(tracks.back(), true));
    for (const auto& t : r.session.project().tracks)
        if (!t.sends.empty())
        {
            r.session.execute(std::make_unique<core::SetSendMute>(t.id, t.sends.front().bus, true));
            r.session.execute(std::make_unique<core::SetSendPan>(t.id, t.sends.front().bus, -0.35f));
            break;
        }
}

// A parameter of the insert the last --insert or --master-insert added, by name
// (case ignored) or index; a stepped one also takes its choice's text. With a
// delay, that many seconds after launch (a tail frozen mid-play, for a capture).
void insertParameter(LaunchRun& r, Args& a, double after)
{
    const auto name = a.text().unquoted();
    const auto value = a.text().unquoted();
    const auto set = [&r, name, value, channel = r.lastInsertChannel, id = r.lastInsertId]
    {
        const auto* c = r.session.project().findChannel(channel);
        const auto* target = c != nullptr ? c->findInsert(id) : nullptr;
        if (target == nullptr)
        {
            std::fprintf(stderr, "--insert-param: no insert added yet\n");
            return;
        }
        const auto& infos = plugins::parameterInfo(target->type);
        int index = name.containsOnly("0123456789") && name.isNotEmpty() ? name.getIntValue() : -1;
        if (index >= static_cast<int>(infos.size()))
            index = -1;
        for (size_t p = 0; p < infos.size(); ++p)
            if (juce::String(infos[p].name).equalsIgnoreCase(name))
                index = static_cast<int>(p);
        if (index < 0)
        {
            std::fprintf(stderr,
                         "--insert-param: %s has no parameter \"%s\"\n",
                         target->type.c_str(),
                         name.toRawUTF8());
            return;
        }
        const auto& info = infos[static_cast<size_t>(index)];
        float v = value.getFloatValue();
        for (size_t k = 0; k < info.choices.size(); ++k)
            if (juce::String(info.choices[k]).equalsIgnoreCase(value))
                v = static_cast<float>(k);
        r.session.execute(std::make_unique<core::SetInsertParameter>(
            channel, id, index, std::clamp(v, info.minimum, info.maximum)));
    };
    if (after > 0.0)
        r.schedule.afterSeconds(after, set);
    else
        set();
}

void stretch(LaunchRun& r, Args& a)
{
    const double factor = a.number();
    double semitones = 0.0;
    if (const auto* arg = a.value())
        semitones = arg->text.getDoubleValue();
    if (factor <= 0.0)
        return;
    onFirstAudioClip(
        r.session,
        [&](const core::Track& track, const core::AudioClip& clip)
        {
            r.session.execute(std::make_unique<core::SetClipStretch>(
                track.id,
                clip.id,
                clip.stretch * factor,
                semitones,
                static_cast<core::SamplePosition>(std::llround(static_cast<double>(clip.length) * factor)),
                false));
        });
}

void insertEditor(LaunchRun& r, Args& a)
{
    // With a type after it, the first insert of that type; else the first insert.
    // The tracks are searched first, then the master.
    std::string type;
    if (const auto* arg = a.value())
        type = arg->text.unquoted().toStdString();
    const auto open = [&](const core::Channel& channel)
    {
        for (const auto& i : channel.inserts)
            if (type.empty() || i.type == type)
            {
                r.window.openInsertEditor(channel.id, i.id);
                return true;
            }
        return false;
    };
    for (const auto& track : r.session.project().tracks)
        if (open(track))
            return;
    open(r.session.project().master);
}

void capture(LaunchRun& r, Args& a)
{
    const auto folder = a.file();
    int seconds = 2;
    if (const auto* arg = a.value([](const juce::String& t) { return t.containsOnly("0123456789"); }))
        seconds = arg->text.getIntValue();
    r.schedule.after(seconds * 1000,
                     [&r, folder]
                     {
                         r.window.capture(folder, r.captureScale);
                         quitApp();
                     });
}

void save(LaunchRun& r, Args& a)
{
    // Saves two seconds after a --stop-after given before it (so the recording has
    // been placed), otherwise once the launch's own messages are through.
    const auto file = a.file();
    r.schedule.after(r.stopAfterSeconds > 0 ? r.stopAfterSeconds * 1000 + 2000 : LaunchSchedule::launchedMs,
                     [&r, file] { r.session.save(file); });
}

// After the window has settled (laid out, the device open), for a capture to see.
void whenSettled(LaunchRun& r, std::function<void()> action)
{
    r.schedule.after(LaunchSchedule::settledMs, std::move(action));
}
} // namespace

LaunchRun::LaunchRun(ProjectSession& s, MainWindow& w, audio::AudioDeviceLayer& a)
    : session(s), window(w), audio(a)
{
}

LaunchRun::~LaunchRun()
{
    cancel();
}

std::unique_ptr<engine::OutputTap> LaunchRun::takeOutputTap()
{
    return std::move(outputTap);
}

void LaunchRun::apply(const juce::String& commandLine)
{
    runLaunchFlags(flags(), *this, commandLine, stopped);
}

// The flags, in the order the help lists them. Each entry: the name, its
// arguments as the help shows them, how many must follow, the help, the action.
const std::vector<LaunchFlag<LaunchRun>>& LaunchRun::flags()
{
    static const std::vector<Flag> table{
        // ----- start-up (read before the window exists)
        {"--help",
         "",
         0,
         "print these flags and quit",
         [](LaunchRun& r, Args&)
         {
             std::printf("%s", help().toRawUTF8());
             std::fflush(stdout);
             quitApp();
             r.stopped = true;
         }},
        {"--no-splash", "", 0, "start without the splash screen"},
        {"--capture-plugins",
         "",
         0,
         "with --capture, start from a copy of the user's scanned plugins, so --insert can add a VST3 or "
         "CLAP (vst3:<id>, clap:<id>)"},
        {"--offscreen",
         "",
         0,
         "run with no window on screen at all (no splash, nothing that can take the focus); windows still "
         "lay "
         "out and paint, so --capture works while someone is using the machine"},
        {"--first-run", "", 0, "show the first-run sheet"},
        // ----- windows
        {"--audio-setup",
         "",
         0,
         "open the Audio Setup window",
         [](LaunchRun& r, Args&) { r.window.showAudioSetup(); }},
        {"--about",
         "",
         0,
         "open the About box (the splash screen, until clicked)",
         [](LaunchRun& r, Args&) { r.window.showAbout(); }},
        {"--preferences",
         "[page]",
         0,
         "open Preferences (general, audio, recording, editing, plugins, shortcuts, diagnostics)",
         [](LaunchRun& r, Args& a)
         {
             const auto* page = a.value();
             r.window.showPreferences(page != nullptr ? page->text.unquoted() : juce::String());
         }},
        {"--plugins",
         "",
         0,
         "show the plugin browser",
         [](LaunchRun& r, Args&) { r.window.showPluginBrowser(); }},
        {"--mixer", "", 0, "show the mixer", [](LaunchRun& r, Args&) { r.window.showMixer(); }},
        {"--panel",
         "<name>",
         1,
         "show a panel: mixer, editor, instruments, pianoRoll, diagnostics, browser, inspector",
         [](LaunchRun& r, Args& a) { r.window.showPanel(a.text().unquoted()); }},
        {"--layout",
         "<Mix|Edit>",
         1,
         "apply a layout preset",
         [](LaunchRun& r, Args& a) { r.window.applyLayoutPreset(a.text().unquoted()); }},
        {"--browser-files",
         "[folder]",
         0,
         "show the Browser panel's Files tab (at the folder)",
         [](LaunchRun& r, Args& a)
         {
             const auto* folder = a.value();
             r.window.showFilesBrowser(folder != nullptr ? folder->resolveAsFile() : juce::File());
         }},
        {"--export", "", 0, "show the export sheet", [](LaunchRun& r, Args&) { r.window.showExport(); }},
        {"--window-size",
         "<w>x<h>",
         1,
         "set the window's size",
         [](LaunchRun& r, Args& a)
         {
             const auto size = a.text().unquoted();
             const int width = size.upToFirstOccurrenceOf("x", false, true).getIntValue();
             const int height = size.fromFirstOccurrenceOf("x", false, true).getIntValue();
             if (width > 0 && height > 0)
                 r.window.setSize(width, height);
         }},
        // ----- projects
        {"--open",
         "<file>",
         1,
         "open a project file",
         [](LaunchRun& r, Args& a) { r.session.open(a.file()); }},
        {"--demo-song",
         "<file>",
         1,
         "write the demo song (Northern Drift) to the file and open it",
         [](LaunchRun& r, Args& a)
         {
             const auto file = a.file();
             file.getParentDirectory().createDirectory();
             const plugins::BuiltInParameterKeys builtInKeys;
             if (file.replaceWithText(juce::String(core::projectToJson(makeDemoSong(), true, &builtInKeys))))
                 r.session.open(file);
         }},
        {"--time-load",
         "<file>",
         1,
         "open the file, print how long each stage of the load took, quit",
         timeLoad},
        {"--show-loading",
         "<file> [stage]",
         1,
         "open the file as File > Open does, with the loading sheet, and hold the load once it reaches the "
         "stage "
         "(reading, media, plugins, engine; media if not given) for --capture",
         [](LaunchRun& r, Args& a)
         {
             const auto file = a.file();
             const auto* stage = a.value();
             r.window.openWithProgress(file, stage != nullptr ? stage->text : juce::String("media"));
         }},
        {"--save",
         "<file>",
         1,
         "save the project to the file: half a second after launch, or two seconds after a --stop-after "
         "given "
         "before it",
         save},
        {"--recover",
         "",
         0,
         "recover partial recordings without asking (unattended checks)",
         [](LaunchRun& r, Args&) { r.session.recoverPartialRecordings(); }},
        {"--import",
         "<file>",
         1,
         "import an audio file onto a new track (repeatable)",
         [](LaunchRun& r, Args& a) { r.session.importAudioFile(a.file(), 0); }},
        {"--import-midi",
         "<file>",
         1,
         "import a Standard MIDI File at the start (new instrument tracks)",
         [](LaunchRun& r, Args& a)
         {
             if (const auto error = r.session.importMidiFile(a.file(), 0); !error.empty())
                 r.window.notify(juce::String::fromUTF8(error.c_str()), true);
         }},
        {"--export-midi",
         "<file>",
         1,
         "write every MIDI clip to a Standard MIDI File (after the flags before it have run)",
         [](LaunchRun& r, Args& a)
         {
             std::vector<std::pair<core::Id, core::Id>> clips;
             for (const auto& track : r.session.project().tracks)
                 for (const auto& clip : track.midiClips)
                     clips.emplace_back(track.id, clip.id);
             if (const auto error = r.session.exportMidiFile(a.file(), clips); !error.empty())
                 r.window.notify(juce::String::fromUTF8(error.c_str()), true);
         }},
        // ----- transport and timing
        {"--play", "", 0, "start the transport", [](LaunchRun& r, Args&) { r.session.play(); }},
        {"--record", "<n>", 1, "create n armed tracks on inputs 1..n and start recording", record},
        {"--stop-after",
         "<s>",
         1,
         "stop the transport after s seconds",
         [](LaunchRun& r, Args& a)
         {
             r.stopAfterSeconds = a.integer();
             r.schedule.after(r.stopAfterSeconds * 1000, [&r] { r.session.stop(); });
         }},
        {"--quit-after",
         "<s>",
         1,
         "quit the app after s seconds",
         [](LaunchRun& r, Args& a)
         {
             r.schedule.after(std::max(0, a.integer()) * 1000,
                              [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
         }},
        {"--loop",
         "<from> <to>",
         2,
         "set and enable the loop region, in seconds",
         [](LaunchRun& r, Args& a)
         {
             const double from = a.number();
             const double to = a.number();
             const double rate = r.session.project().sampleRate;
             r.session.setRegion(static_cast<core::SamplePosition>(from * rate),
                                 static_cast<core::SamplePosition>(to * rate));
             r.session.setLoopEnabled(true);
         }},
        {"--no-count-in",
         "",
         0,
         "record without the count-in",
         [](LaunchRun& r, Args&) { r.session.setCountInBars(0); }},
        {"--marker",
         "<s>",
         1,
         "add a marker at s seconds",
         [](LaunchRun& r, Args& a)
         {
             core::Marker marker;
             marker.position = static_cast<core::SamplePosition>(a.number() * r.session.project().sampleRate);
             marker.name = "Marker " + std::to_string(r.session.project().markers.size() + 1);
             r.session.execute(std::make_unique<core::AddMarker>(marker));
         }},
        {"--master-gain",
         "<dB>",
         1,
         "set the master fader (-120 = silent: an unattended check that should not be heard; pre-fader "
         "displays "
         "still move)",
         [](LaunchRun& r, Args& a)
         {
             const auto db = static_cast<float>(a.number());
             r.session.execute(std::make_unique<core::SetMasterGain>(
                 engine::dbToGain(db, static_cast<float>(engine::processorFloorDb))));
         }},
        // ----- the audio device
        {"--buffer-size",
         "<n>",
         1,
         "run the audio device at n samples for this run only (the saved settings are not changed); prints "
         "the "
         "device it opened",
         bufferSize},
        {"--output-tap",
         "<folder>",
         1,
         "record what the callback hands the driver to output.wav in the folder (created if needed) until "
         "quit, "
         "with events.csv (clicks found in it, late callbacks, overruns) and summary.txt. For telling clicks "
         "Northlight makes from ones the driver or system makes; the Diagnostics panel shows its counts "
         "while it "
         "runs",
         startOutputTap},
        {"--scan-plugins",
         "",
         0,
         "rescan every plugin",
         [](LaunchRun& r, Args&) { r.session.scanPlugins(true); }},
        // ----- tracks and clips
        {"--bus", "", 0, "add a bus", [](LaunchRun& r, Args&) { r.window.addBus(); }},
        {"--remove-bus",
         "",
         0,
         "remove the last bus the way the mixer does",
         [](LaunchRun& r, Args&) { r.window.removeLastBus(); }},
        {"--midi-track",
         "",
         0,
         "add a MIDI track (built-in synth) with a short clip",
         [](LaunchRun& r, Args&) { midiTrack(r, {}); }},
        {"--midi-track-with",
         "<type>",
         1,
         "the same with the given instrument type instead",
         [](LaunchRun& r, Args& a) { midiTrack(r, a.text().unquoted().toStdString()); }},
        {"--sampler",
         "<file> [demo]",
         1,
         "add a Sampler instrument playing the file (a MIDI track with a short clip) and open its editor; "
         "\"demo\" "
         "also sets a loop, a key range and the filter so every display has something to show",
         sampler},
        {"--fx-track",
         "<reverb|delay|empty>",
         1,
         "add an FX track with a send from every track",
         [](LaunchRun& r, Args& a)
         {
             const auto wanted = a.text().unquoted().toLowerCase();
             for (const auto& choice : plugins::fxTrackEffects())
                 if (juce::String(choice.name).toLowerCase() == wanted)
                 {
                     std::vector<core::Id> senders;
                     for (const auto& t : r.session.project().tracks)
                         senders.push_back(t.id);
                     r.session.executeGroup(
                         "Add FX track",
                         plugins::addFxTrackCommands(r.session.project(), choice.type, choice.name, senders));
                 }
         }},
        {"--folder",
         "<n> <m>",
         2,
         "put tracks n..m (1-based) in a new folder track",
         [](LaunchRun& r, Args& a)
         {
             const int from = a.integer() - 1;
             const int to = a.integer() - 1;
             std::vector<core::Id> members;
             const auto& tracks = r.session.project().tracks;
             for (int t = std::max(0, from); t <= to && t < static_cast<int>(tracks.size()); ++t)
                 members.push_back(tracks[static_cast<size_t>(t)].id);
             r.session.execute(std::make_unique<core::AddFolderTrack>("Folder", members));
         }},
        {"--freeze",
         "<n>",
         1,
         "freeze track n (1-based) in the background",
         [](LaunchRun& r, Args& a)
         {
             const int n = a.integer() - 1;
             const auto& tracks = r.session.project().tracks;
             if (n >= 0 && n < static_cast<int>(tracks.size()))
                 if (const auto error = r.session.freezeTrack(tracks[static_cast<size_t>(n)].id);
                     !error.empty())
                     juce::Logger::writeToLog("--freeze: " + juce::String(error));
         }},
        {"--takes-demo",
         "<files>",
         0,
         "a \"Vocal\" track with the files as takes in one take group, comped from each in turn, its take "
         "lanes "
         "expanded",
         takesDemo},
        {"--tempo-demo",
         "",
         0,
         "a tempo track with a change, a ramp and a 3/4 bar, shown",
         [](LaunchRun& r, Args&)
         {
             core::TempoState state = core::TempoState::of(r.session.project());
             state.tempoChanges = {{16.0, 140.0, false}, {32.0, 90.0, true}};
             state.meterChanges = {{10, {3, 4}}, {12, {4, 4}}};
             r.session.execute(std::make_unique<core::SetTempoMap>(state));
             r.window.showTempoTrack();
         }},
        {"--key",
         "<root> [minor]",
         1,
         "set the project key: root a pitch class 0 (C)..11 (B), major unless \"minor\" follows",
         [](LaunchRun& r, Args& a)
         {
             core::MusicalKey key;
             key.root = std::clamp(a.integer(), 0, 11);
             if (a.word("minor"))
                 key.scale = core::ScaleKind::minor;
             r.session.execute(std::make_unique<core::SetProjectKey>(key));
         }},
        {"--automation",
         "",
         0,
         "automation lanes on the first track that makes sound, open in the arrange view: volume (a swell), "
         "pan "
         "(collapsed), an empty lane for the first strip module's first parameter, and a hidden lane for its "
         "second",
         automation},
        {"--select-clip",
         "",
         0,
         "select the first audio clip (the inspector shows it)",
         [](LaunchRun& r, Args&)
         {
             onFirstAudioClip(r.session,
                              [&](const core::Track& track, const core::AudioClip& clip)
                              { r.window.selectClip(track.id, clip.id); });
         }},
        {"--mute-clips",
         "",
         0,
         "mute the first audio clip and the first MIDI clip",
         [](LaunchRun& r, Args&)
         {
             bool audio = false, midi = false;
             for (const auto& track : r.session.project().tracks)
             {
                 if (!audio && !track.clips.empty())
                 {
                     r.session.execute(
                         std::make_unique<core::SetClipMute>(track.id, track.clips.front().id, false, true));
                     audio = true;
                 }
                 if (!midi && !track.midiClips.empty())
                 {
                     r.session.execute(std::make_unique<core::SetClipMute>(
                         track.id, track.midiClips.front().id, true, true));
                     midi = true;
                 }
             }
         }},
        {"--clip-fades",
         "<in s> <out s> <in curve> <out curve>",
         4,
         "give the first audio clip fades of those lengths and curves (-1 slow to 1 fast)",
         [](LaunchRun& r, Args& a)
         {
             const double rate = r.session.project().sampleRate;
             const auto in = static_cast<core::SamplePosition>(a.number() * rate);
             const auto out = static_cast<core::SamplePosition>(a.number() * rate);
             const auto inCurve = static_cast<float>(a.number());
             const auto outCurve = static_cast<float>(a.number());
             onFirstAudioClip(r.session,
                              [&](const core::Track& track, const core::AudioClip& clip)
                              {
                                  r.session.execute(std::make_unique<core::SetClipFades>(
                                      track.id, clip.id, in, out, inCurve, outCurve));
                              });
         }},
        {"--stretch", "<factor> [semitones]", 1, "stretch (and transpose) the first audio clip", stretch},
        // ----- inserts and the strip
        {"--insert",
         "<type>",
         1,
         "add a built-in insert (northlight.glue, ...) to the first track that makes audio",
         [](LaunchRun& r, Args& a) { insert(r, a, false); }},
        {"--master-insert",
         "<type>",
         1,
         "add a built-in insert to the master",
         [](LaunchRun& r, Args& a) { insert(r, a, true); }},
        {"--corona-page",
         "<amp|cabinet>",
         1,
         "the page Corona's cards open on (give it before --insert)",
         [](LaunchRun&, Args& a) { ui::strip::setEmberStartPage(a.text().unquoted() == "cabinet"); }},
        {"--insert-param",
         "<name> <value>",
         2,
         "set a parameter of the insert the last --insert or --master-insert added, by its name or index (a "
         "stepped one also by its choice's text: --insert-param Release Auto)",
         [](LaunchRun& r, Args& a) { insertParameter(r, a, 0.0); }},
        {"--insert-param-at",
         "<s> <name> <value>",
         3,
         "the same, s seconds after launch (a tail frozen mid-play: --insert-param-at 4 Freeze 1)",
         [](LaunchRun& r, Args& a)
         {
             const double after = a.number();
             insertParameter(r, a, after);
         }},
        {"--insert-editor",
         "[type]",
         0,
         "open the editor window of the first insert (of that type, if given) on the tracks, else on the "
         "master",
         insertEditor},
        {"--instrument-editor",
         "",
         0,
         "open the first instrument's editor window",
         [](LaunchRun& r, Args&)
         {
             for (const auto& track : r.session.project().tracks)
                 if (track.hasInstrument())
                 {
                     r.window.openInsertEditor(track.id, track.instrument.id);
                     return;
                 }
         }},
        {"--mixer-wide",
         "",
         0,
         "show the mixer's strips wide (the dB scale, the send pan column)",
         [](LaunchRun& r, Args&) { r.window.setMixerWide(true); }},
        {"--dock-height",
         "<px>",
         1,
         "make the dock this tall (a tall mixer shows its insert and send slots)",
         [](LaunchRun& r, Args& a) { r.window.setDockHeight(a.integer()); }},
        {"--mixer-states",
         "",
         0,
         "put the mixer in its less common states: the first track solo safe and the second soloed, the "
         "first "
         "send muted and panned, the third track on outputs 3/4 and the last one hidden",
         mixerStates},
        {"--strip-preset",
         "<name>",
         1,
         "apply a factory channel strip preset to every track",
         [](LaunchRun& r, Args& a)
         {
             const auto name = a.text().unquoted().toStdString();
             if (const auto* preset = plugins::strip::findFactoryPreset(name))
             {
                 const auto tracks = r.session.project().tracks;
                 for (const auto& track : tracks)
                     r.session.executeGroup(
                         "Strip preset: " + name,
                         plugins::strip::presetCommands(track, preset->modules, "Strip preset: " + name));
             }
             r.window.showPanel("editor");
         }},
        {"--strip",
         "<modules>",
         1,
         "switch channel strip modules on for every track and show the Editor panel: a comma list of gate, "
         "eq, "
         "compressor, deesser, saturation, limiter, or all; \"opto\" swaps the compressor for the opto "
         "compressor "
         "and switches it on; \"demo\" also sets values that give every display something to show",
         [](LaunchRun& r, Args& a) { r.applyStripFlag(a.text().unquoted()); }},
        {"--editor-gap",
         "<n>",
         1,
         "show the Editor panel's nth gap button (1-based), as the pointer in that gap between two insert "
         "cards "
         "would, once the window has settled",
         [](LaunchRun& r, Args& a)
         {
             const int n = a.integer();
             whenSettled(r, [&r, n] { r.window.showEditorGap(n); });
         }},
        // ----- editors
        {"--piano-roll",
         "",
         0,
         "open the piano roll on the first MIDI clip",
         [](LaunchRun& r, Args&)
         {
             for (const auto& track : r.session.project().tracks)
                 if (!track.midiClips.empty())
                 {
                     r.window.openPianoRoll(track.id, track.midiClips.front().id);
                     return;
                 }
         }},
        {"--cc-lane",
         "<lane>",
         1,
         "the piano roll showing a controller lane (bend, mod, expression, sustain, pressure, cc<n>); a clip "
         "with "
         "no events in it gets a demonstration curve first",
         [](LaunchRun& r, Args& a) { r.showControllerLane(a.text().unquoted()); }},
        {"--audio-editor",
         "[pitch]",
         0,
         "open the audio editor on the first audio clip (in pitch mode)",
         [](LaunchRun& r, Args& a)
         {
             const bool pitch = a.word("pitch");
             onFirstAudioClip(r.session,
                              [&](const core::Track& track, const core::AudioClip& clip)
                              { r.window.openAudioEditor(track.id, clip.id, pitch); });
         }},
        {"--audio-editor-zoom",
         "<samples per pixel> [seconds]",
         1,
         "zoom that editor, with the given time into the clip at the left edge (default 0)",
         [](LaunchRun& r, Args& a)
         {
             const double samplesPerPixel = a.number();
             double seconds = 0.0;
             if (const auto* arg =
                     a.value([](const juce::String& t) { return t.containsOnly("0123456789."); }))
                 seconds = arg->text.getDoubleValue();
             r.window.zoomAudioEditor(samplesPerPixel, seconds);
         }},
        {"--correct-pitch",
         "",
         0,
         "once that clip's pitch is analysed, correct it to the key",
         [](LaunchRun& r, Args&) { r.whenVocalReady([&r] { r.window.correctPitchInEditor(); }, 60); }},
        {"--mute-breaths",
         "",
         0,
         "once that clip's pitch is analysed, mute its breaths",
         [](LaunchRun& r, Args&) { r.whenVocalReady([&r] { r.window.muteBreathsInEditor(); }, 60); }},
        {"--arrange-tool",
         "<name>",
         1,
         "pick the arrange view's tool: select, range, pencil, erase, split, glue, mute",
         [](LaunchRun& r, Args& a)
         {
             const auto name = a.text();
             if (!r.window.setArrangeTool(name))
                 std::fprintf(stderr, "--arrange-tool: no tool \"%s\"\n", name.toRawUTF8());
         }},
        {"--arrange-pointer",
         "<track> <bar> [shift|alt]",
         2,
         "rest the pointer on a track row (0-based) at a bar (fractions allowed), with Shift or Alt held if "
         "given, "
         "once the window has settled (the split line, the glue preview)",
         [](LaunchRun& r, Args& a)
         {
             const int row = a.integer();
             const double bar = a.number();
             juce::ModifierKeys mods;
             if (a.word("shift"))
                 mods = juce::ModifierKeys(juce::ModifierKeys::shiftModifier);
             else if (a.word("alt"))
                 mods = juce::ModifierKeys(juce::ModifierKeys::altModifier);
             whenSettled(r, [&r, row, bar, mods] { r.window.arrangePointer(row, bar, mods); });
         }},
        {"--show-menu",
         "<name>",
         1,
         "open a menu as a click would, once the window has settled: project, add-track, clip (the first "
         "clip's)",
         [](LaunchRun& r, Args& a)
         {
             const auto key = a.text().unquoted();
             whenSettled(r, [&r, key] { r.window.showMenu(key); });
         }},
        // ----- benchmarks
        {"--benchmark-strip-drag",
         "<s>",
         1,
         "drag the first track's strip EQ for s seconds, print the strip's display tick intervals, quit",
         [](LaunchRun& r, Args& a) { r.window.runStripDragBenchmark(a.integer()); }},
        {"--benchmark-ui",
         "<s>",
         1,
         "play and scroll for s seconds, print frame percentiles, quit",
         [](LaunchRun& r, Args& a) { r.window.runBenchmark(a.integer()); }},
        {"--benchmark-hover",
         "<s>",
         1,
         "hover the open editor windows' knobs (else the Editor panel's), one a frame, for s seconds; print "
         "the "
         "repaint times, quit",
         [](LaunchRun& r, Args& a) { r.window.runHoverBenchmark(a.integer()); }},
        {"--benchmark-panels",
         "<n>",
         1,
         "open and close the inspector n times with the Editor docked, print the slide's frame intervals, "
         "quit",
         [](LaunchRun& r, Args& a) { r.window.runPanelBenchmark(a.integer()); }},
        // ----- captures
        {"--capture-scale",
         "<n>",
         1,
         "the scale the captures are saved at (2 if not given)",
         [](LaunchRun& r, Args& a)
         { r.captureScale = juce::jlimit(1.0f, 4.0f, static_cast<float>(a.number())); }},
        {"--capture",
         "<folder> [s]",
         1,
         "after s seconds (2 if not given), save the window and its showing parts (mixer, channel-strip, "
         "instrument, piano-roll, audio-editor; with --preferences, every Preferences page as "
         "preferences-<page>; "
         "any other open window, such as an insert's editor, as window-<name>) as PNGs in the folder, then "
         "quit. "
         "Uses its own settings folder, so the layout is the default one, and sends silence to the device, "
         "so "
         "playback is not heard",
         capture},
    };
    return table;
}

// Runs `action` once the first audio clip has a vocal edit (its analysis is in),
// checking twice a second up to `tries` times.
void LaunchRun::whenVocalReady(std::function<void()> action, int tries)
{
    schedule.after(500,
                   [this, action, tries]
                   {
                       for (const auto& track : session.project().tracks)
                           if (!track.clips.empty() && track.clips.front().vocal.active())
                           {
                               action();
                               return;
                           }
                       if (tries > 0)
                           whenVocalReady(action, tries - 1);
                   });
}

// --cc-lane: the first MIDI clip in the piano roll with the lane showing; a clip
// with nothing in that lane gets a curve (a swell, or a bend up and back).
void LaunchRun::showControllerLane(const juce::String& key)
{
    const auto lane = ui::PianoRoll::laneForKey(key);
    for (const auto& track : session.project().tracks)
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
                curve.push_back({core::invalidId,
                                 lane.type,
                                 lane.number,
                                 low + static_cast<int>(std::lround(shape * (high - low))),
                                 static_cast<core::SamplePosition>(t * static_cast<double>(clip.length))});
            }
            session.execute(std::make_unique<core::EditMidiControllers>(
                track.id, clip.id, std::move(curve), std::vector<core::Id>{}, "Draw controller"));
        }
        window.openPianoRoll(track.id, clip.id);
        window.setPianoRollLane(key);
        return;
    }
}

void LaunchRun::applyStripFlag(const juce::String& text)
{
    namespace ps = plugins::strip;
    const auto names = juce::StringArray::fromTokens(text.toLowerCase(), ",", "");
    const bool all = names.contains("all") || (names.contains("demo") && names.size() == 1);
    const bool demo = names.contains("demo");
    if (names.contains("opto"))
        for (const auto& track : session.project().tracks)
            for (const auto& module : track.strip)
                if (module.type == ps::types::compressor)
                {
                    session.execute(std::make_unique<core::SetStripModuleType>(
                        track.id,
                        module.id,
                        ps::types::opto,
                        plugins::defaultParameters(ps::types::opto),
                        "Swap"));
                    break;
                }
    const auto tracks = session.project().tracks; // a copy: commands change the project
    for (const auto& track : tracks)
        for (const auto& module : track.strip)
        {
            const auto name = juce::String(module.type).fromLastOccurrenceOf(".", false, false);
            if (all || names.contains(name))
                session.execute(std::make_unique<core::SetInsertBypass>(track.id, module.id, false));
            if (!demo || !(all || names.contains(name)))
                continue;
            const auto set = [&](int index, float value)
            {
                session.execute(
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
    window.showPanel("editor");
}

} // namespace northlight
