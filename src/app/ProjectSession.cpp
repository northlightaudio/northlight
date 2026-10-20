#include "ProjectSession.h"

#include "audio/AudioDeviceLayer.h"
#include "core/ProjectJson.h"
#include "engine/WavFile.h"
#include "hosting/ClapPlugin.h"
#include "plugins/Aurora.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <vector>

namespace aural
{

namespace
{
constexpr int garbageTimerMilliseconds = 250;
}

ProjectSession::ProjectSession(audio::AudioDeviceLayer& audio, juce::File registryFile)
    : audio_(audio), loader_(disk_), registryFile_(std::move(registryFile)), scanner_(registry_),
      inserts_(registry_), engine_(loader_, inserts_)
{
    if (registryFile_.existsAsFile())
        if (auto loaded = hosting::PluginRegistry::fromJson(registryFile_.loadFileAsString().toStdString()))
            registry_ = std::move(*loaded);
    scanner_.onProgress = [this] { notify(Change::structure); };
    scanner_.onFinished = [this]
    {
        saveRegistry();
        inserts_.clearFailures(); // a plugin that failed before may load now
        publish();
    };
    audio_.callback().setEngine(&engine_);
    audio_.midi().setEngine(&engine_);
    audio_.addChangeListener(this);
    matchDeviceSampleRate();
    publish();
    startTimer(garbageTimerMilliseconds);
    hostedAutomation_.startTimerHz(50);
}

ProjectSession::~ProjectSession()
{
    stopTimer();
    hostedAutomation_.stopTimer();
    removeOwnAutosave(); // a normal close: the user saved or chose not to
    scanner_.cancel();
    audio_.removeChangeListener(this);
    // The device must not call into a dead engine: detach, then let the layer stop
    // the device (it shuts down after this object in the application).
    audio_.callback().setEngine(nullptr);
    audio_.midi().setEngine(nullptr);
}

// ----- editing --------------------------------------------------------------------------

namespace
{
// Every new track and bus gets the channel strip (its modules off).
void giveChannelStrip(core::Command& command)
{
    if (auto* addTrack = dynamic_cast<core::AddTrack*>(&command))
        addTrack->setDefaultStrip(plugins::strip::defaultStrip());
    else if (auto* addBus = dynamic_cast<core::AddBus*>(&command))
        addBus->setDefaultStrip(plugins::strip::defaultStrip());
}
} // namespace

void ProjectSession::notify(Change change)
{
    // structure < values < parameters: keep the widest.
    if (static_cast<int>(change) < static_cast<int>(pending_))
        pending_ = change;
    sendChangeMessage();
}

ProjectSession::Change ProjectSession::takeChange() noexcept
{
    const auto change = pending_;
    pending_ = Change::parameters;
    return change;
}

void ProjectSession::execute(std::unique_ptr<core::Command> command)
{
    giveChannelStrip(*command);
    if (recordAutomation(*command))
        return; // written into automation, not into the static value
    // A removal copies the model insert into the command for undo: pull the live
    // plugin state into the model first, so undo brings back what the user had,
    // not what the last save held.
    if (dynamic_cast<core::RemoveInsert*>(command.get()) != nullptr ||
        dynamic_cast<core::RemoveTrack*>(command.get()) != nullptr ||
        dynamic_cast<core::RemoveBus*>(command.get()) != nullptr)
        captureInsertStates();
    const bool live = command->isLive();
    const bool parametersOnly = dynamic_cast<core::SetInsertParameter*>(command.get()) != nullptr;
    std::vector<core::Command::ParameterTouch> touches;
    command->touchedParameters(touches);
    mirrorOwnedParameters(touches);
    undo_.execute(project_, std::move(command));
    modified_ = true;
    edited();
    if (live)
    {
        // A gain, pan or parameter: the running graph takes the new value
        // (ramped) without a recompile.
        // A parameter goes to its processor alone; anything else re-pushes
        // every channel's values.
        if (!parametersOnly)
            engine_.applyLiveValues(project_);
        engine_.pushInsertParameters(project_, touches);
        notify(parametersOnly ? Change::parameters : Change::values);
    }
    else
        publish();
}

void ProjectSession::beginGesture(const std::string& name)
{
    if (gestureOpen_)
        undo_.endGroup();
    undo_.beginGroup(name);
    gestureOpen_ = true;
}

void ProjectSession::endGesture()
{
    if (!gestureOpen_)
        return;
    undo_.endGroup();
    gestureOpen_ = false;
    finishPasses(true); // touch passes end with the gesture
}

void ProjectSession::executeGroup(const std::string& name,
                                  std::vector<std::unique_ptr<core::Command>> commands)
{
    if (commands.empty())
        return;
    undo_.beginGroup(name);
    std::vector<core::Command::ParameterTouch> touches;
    for (auto& command : commands)
    {
        giveChannelStrip(*command);
        std::vector<core::Command::ParameterTouch> own;
        command->touchedParameters(own);
        mirrorOwnedParameters(own);
        touches.insert(touches.end(), own.begin(), own.end());
        undo_.execute(project_, std::move(command));
    }
    undo_.endGroup();
    modified_ = true;
    edited();
    publish();
    engine_.pushInsertParameters(project_, touches);
}

std::string ProjectSession::normaliseClip(core::Id trackId, core::Id clipId, float targetDb)
{
    const auto* track = project_.findTrack(trackId);
    const auto* clip = track != nullptr ? track->findClip(clipId) : nullptr;
    if (clip == nullptr)
        return "no such clip";
    std::string error;
    const auto source = loader_.load(clip->mediaPath, project_.sampleRate, error);
    if (source == nullptr)
        return error.empty() ? "could not read " + clip->mediaPath : error;
    float peak = 0.0f;
    std::vector<float> chunk(8192);
    const auto end = std::min<std::int64_t>(clip->sourceOffset + clip->length, source->lengthSamples());
    for (int ch = 0; ch < source->numChannels(); ++ch)
        for (std::int64_t at = clip->sourceOffset; at < end; at += static_cast<std::int64_t>(chunk.size()))
        {
            const auto n =
                static_cast<int>(std::min<std::int64_t>(static_cast<std::int64_t>(chunk.size()), end - at));
            source->read(ch, at, chunk.data(), n);
            for (int i = 0; i < n; ++i)
                peak = std::max(peak, std::abs(chunk[static_cast<size_t>(i)]));
        }
    if (peak <= 0.0f)
        return "the clip is silent";
    const float target = std::pow(10.0f, targetDb / 20.0f);
    execute(std::make_unique<core::SetClipGain>(trackId, clipId, target / peak));
    return {};
}

void ProjectSession::undo()
{
    if (undo_.undo(project_))
    {
        modified_ = true;
        edited();
        publish();
        // Parameters the undone command restored reach plugins that own theirs.
        std::vector<core::Command::ParameterTouch> touches;
        if (const auto* undone = undo_.redoTop())
            undone->touchedParameters(touches);
        engine_.pushInsertParameters(project_, touches);
    }
}

void ProjectSession::redo()
{
    if (undo_.redo(project_))
    {
        modified_ = true;
        edited();
        publish();
        std::vector<core::Command::ParameterTouch> touches;
        if (const auto* redone = undo_.undoTop())
            redone->touchedParameters(touches);
        engine_.pushInsertParameters(project_, touches);
    }
}

void ProjectSession::mirrorOwnedParameters(const std::vector<core::Command::ParameterTouch>& touches)
{
    // A processor that owns its parameters (a hosted plugin) is the truth for
    // their values; before a command sets one, the model's list is brought up to
    // date from the instance so the command records the real previous value and
    // undo restores it.
    for (const auto& touch : touches)
    {
        const auto instance = engine_.insertInstance(touch.insertId);
        if (instance == nullptr || !instance->ownsParameters())
            continue;
        core::Insert* insert = nullptr;
        for (auto& t : project_.tracks)
            if (auto* i = t.findInsert(touch.insertId))
                insert = i;
        for (auto& b : project_.buses)
            if (auto* i = b.findInsert(touch.insertId))
                insert = i;
        if (insert == nullptr)
            continue;
        // Up to the touched parameter (a plugin can have thousands; the list
        // only needs to reach what commands record and restore).
        const auto count =
            std::min(instance->parameters().size(), static_cast<size_t>(std::max(0, touch.parameter)) + 1);
        if (insert->params.size() < count)
            insert->params.resize(count, 0.0f);
        for (size_t k = 0; k < count; ++k)
            insert->params[k] = instance->parameter(static_cast<int>(k));
    }
}

core::Id ProjectSession::addMidiTrack()
{
    int count = 0;
    for (const auto& t : project_.tracks)
        if (t.isMidi())
            ++count;
    core::Track track;
    track.kind = core::TrackKind::midi;
    track.name = "MIDI " + std::to_string(count + 1);
    track.armed = true;
    track.instrument.type = plugins::aurora::type; // the bundled synth
    track.instrument.params = plugins::defaultParameters(track.instrument.type);
    auto command = std::make_unique<core::AddTrack>(track);
    const auto* raw = command.get();
    execute(std::move(command));
    return raw->trackId();
}

std::string ProjectSession::importAudioFile(const juce::File& file, core::SamplePosition at)
{
    std::string error;
    const auto info = loader_.probe(file.getFullPathName().toStdString(), error);
    if (!info.has_value())
        return error;

    core::Track track;
    track.name = file.getFileNameWithoutExtension().toStdString();
    core::AudioClip clip;
    clip.name = track.name;
    clip.mediaPath = file.getFullPathName().toStdString();
    clip.start = std::max<core::SamplePosition>(0, at);
    clip.length = info->lengthAt(project_.sampleRate);
    track.clips.push_back(clip);

    execute(std::make_unique<core::AddTrack>(track));

    const auto& errors = engine_.mediaErrors();
    if (const auto it = errors.find(clip.mediaPath); it != errors.end())
        return it->second;
    return {};
}

// ----- files ------------------------------------------------------------------------------

void ProjectSession::newProject()
{
    removeOwnAutosave();
    hostedSent_.clear();
    untitledStem_.clear();
    engine_.resetCaches(); // another project's ids must not reuse these instances
    project_ = core::Project{};
    undo_.clear();
    projectFile_ = juce::File();
    modified_ = false;
    matchDeviceSampleRate();
    publish();
}

std::string ProjectSession::open(const juce::File& file)
{
    removeOwnAutosave();
    hostedSent_.clear();
    untitledStem_.clear();
    engine_.resetCaches();
    std::string error;
    const auto loaded = core::projectFromJson(file.loadFileAsString().toStdString(), &error);
    if (!loaded.has_value())
        return error.empty() ? "could not read project" : error;
    project_ = *loaded;
    plugins::strip::ensureChannelStrip(project_); // projects saved before the strip existed
    replaceLegacySynth();
    project_.adoptInstruments([this](const std::string& type) { return isInstrumentType(type); });
    undo_.clear();
    projectFile_ = file;
    modified_ = false;
    publish();
    return {};
}

std::string ProjectSession::save(const juce::File& file)
{
    captureInsertStates();
    const auto text = core::projectToJson(project_);
    if (!file.replaceWithText(juce::String(text)))
        return "could not write " + file.getFullPathName().toStdString();
    projectFile_ = file;
    modified_ = false;
    removeOwnAutosave();
    notify(Change::structure);
    return {};
}

// ----- templates ---------------------------------------------------------------------------

juce::File ProjectSession::templatesFolder() const
{
    if (templatesFolder_ != juce::File())
        return templatesFolder_;
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Aural")
        .getChildFile("Templates");
}

std::vector<juce::File> ProjectSession::templates() const
{
    std::vector<juce::File> found;
    for (const auto& f : templatesFolder().findChildFiles(juce::File::findFiles, false, "*.aural"))
        found.push_back(f);
    std::sort(found.begin(),
              found.end(),
              [](const juce::File& a, const juce::File& b)
              { return a.getFileName().compareNatural(b.getFileName()) < 0; });
    return found;
}

std::string ProjectSession::saveAsTemplate(const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty())
        return "a template needs a name";
    captureInsertStates();
    auto layout = project_;
    layout.name = clean.toStdString();
    for (auto& t : layout.tracks)
    {
        t.clips.clear();
        t.midiClips.clear();
        t.armed = false;
        for (auto& lane : t.automation)
            lane.points.clear();
        std::erase_if(t.automation, [](const core::AutomationLane& l) { return l.points.empty(); });
    }
    for (auto& b : layout.buses)
        b.automation.clear();
    layout.markers.clear();
    const auto folder = templatesFolder();
    if (!folder.createDirectory().wasOk())
        return "could not create " + folder.getFullPathName().toStdString();
    const auto file = folder.getChildFile(clean + ".aural");
    if (!file.replaceWithText(juce::String(core::projectToJson(layout))))
        return "could not write " + file.getFullPathName().toStdString();
    return {};
}

std::string ProjectSession::newFromTemplate(const juce::File& file)
{
    std::string error;
    const auto loaded = core::projectFromJson(file.loadFileAsString().toStdString(), &error);
    if (!loaded.has_value())
        return error.empty() ? "could not read the template" : error;
    removeOwnAutosave();
    untitledStem_.clear();
    engine_.resetCaches();
    project_ = *loaded;
    project_.name = "Untitled";
    hostedSent_.clear();
    plugins::strip::ensureChannelStrip(project_);
    replaceLegacySynth();
    project_.adoptInstruments([this](const std::string& type) { return isInstrumentType(type); });
    undo_.clear();
    projectFile_ = juce::File();
    modified_ = false;
    matchDeviceSampleRate();
    publish();
    return {};
}

// ----- media management ------------------------------------------------------------------

namespace
{
bool isMediaFile(const juce::File& f)
{
    if (f.getFileName().endsWithIgnoreCase(".partial.wav") ||
        f.getFileName().endsWithIgnoreCase(".discarded.wav"))
        return false; // unfinished recordings belong to recovery
    return f.hasFileExtension("wav;aif;aiff;flac;mp3;ogg");
}
} // namespace

ProjectSession::MediaReport ProjectSession::mediaReport() const
{
    MediaReport report;
    const auto folder = mediaFolder();
    std::vector<juce::File> used;
    for (const auto& t : project_.tracks)
        for (const auto& c : t.clips)
        {
            const juce::File f(juce::String::fromUTF8(c.mediaPath.c_str()));
            if (std::find(used.begin(), used.end(), f) != used.end())
                continue;
            used.push_back(f);
            if (!f.existsAsFile())
                report.missing.push_back(f);
            else if (projectFile_ == juce::File() || !f.isAChildOf(folder))
                report.outside.push_back(f);
        }
    if (projectFile_ != juce::File() && folder.isDirectory())
        for (const auto& f : folder.findChildFiles(juce::File::findFiles, false))
            if (isMediaFile(f) && std::find(used.begin(), used.end(), f) == used.end())
                report.unused.push_back(f);
    return report;
}

std::string ProjectSession::consolidateMedia(int& copied)
{
    copied = 0;
    if (projectFile_ == juce::File())
        return "save the project first: media is gathered next to the project file";
    const auto folder = mediaFolder();
    if (!folder.createDirectory().wasOk())
        return "could not create " + folder.getFullPathName().toStdString();
    std::vector<std::pair<std::string, std::string>> renames;
    for (const auto& source : mediaReport().outside)
    {
        auto target = folder.getChildFile(source.getFileName());
        if (target.existsAsFile())
            target = folder.getNonexistentChildFile(
                source.getFileNameWithoutExtension(), source.getFileExtension(), false);
        if (!source.copyFileTo(target))
            return "could not copy " + source.getFullPathName().toStdString();
        renames.emplace_back(source.getFullPathName().toStdString(), target.getFullPathName().toStdString());
        ++copied;
    }
    if (!renames.empty())
        execute(std::make_unique<core::SetMediaPaths>(std::move(renames)));
    return {};
}

int ProjectSession::removeUnusedMedia()
{
    int moved = 0;
    for (const auto& f : mediaReport().unused)
    {
        if (!f.moveToTrash())
            continue;
        ++moved;
        // Its peak cache goes with it.
        for (const auto& sidecar :
             f.getParentDirectory().findChildFiles(juce::File::findFiles, false, f.getFileName() + ".*"))
            sidecar.moveToTrash();
    }
    return moved;
}

// ----- autosave ---------------------------------------------------------------------------

namespace
{
juce::File sidecarOf(const juce::File& autosave)
{
    return autosave.withFileExtension("json");
}
} // namespace

void ProjectSession::setAutosave(juce::File folder, std::function<int()> intervalSeconds)
{
    autosaveFolder_ = std::move(folder);
    autosaveInterval_ = std::move(intervalSeconds);
}

juce::File ProjectSession::currentAutosaveFile() const
{
    if (autosaveFolder_ == juce::File())
        return {};
    juce::String stem;
    if (projectFile_ != juce::File())
        stem = projectFile_.getFileNameWithoutExtension() + "-" +
               juce::String::toHexString(projectFile_.getFullPathName().toLowerCase().hashCode64());
    else
        stem = "Untitled-" + untitledStem_;
    return autosaveFolder_.getChildFile(juce::File::createLegalFileName(stem) + ".aural");
}

void ProjectSession::edited()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    lastEditMs_ = now;
    if (dirtySinceMs_ <= 0.0)
        dirtySinceMs_ = now;
}

bool ProjectSession::autosaveNow()
{
    if (autosaveFolder_ == juce::File() || !modified_)
        return false;
    if (untitledStem_.isEmpty())
        untitledStem_ = juce::Uuid().toString().substring(0, 8);
    const auto target = currentAutosaveFile();
    if (!autosaveFolder_.createDirectory().wasOk())
        return false;
    captureInsertStates();
    // Written beside the target and moved over it, so a crash mid-write never
    // leaves a torn copy where the last good one was.
    const auto temp = target.getSiblingFile(target.getFileName() + ".tmp");
    if (!temp.replaceWithText(juce::String(core::projectToJson(project_))) || !temp.moveFileTo(target))
    {
        temp.deleteFile();
        return false;
    }
    auto meta = std::make_unique<juce::DynamicObject>();
    meta->setProperty("project", projectFile_.getFullPathName());
    meta->setProperty("name", juce::String::fromUTF8(project_.name.c_str()));
    meta->setProperty("time", juce::Time::currentTimeMillis());
    sidecarOf(target).replaceWithText(juce::JSON::toString(juce::var(meta.release())));
    // A project saved under a new name since the last autosave: the old copy goes.
    if (autosaveWritten_ != juce::File() && autosaveWritten_ != target)
    {
        autosaveWritten_.deleteFile();
        sidecarOf(autosaveWritten_).deleteFile();
    }
    autosaveWritten_ = target;
    dirtySinceMs_ = 0.0;
    return true;
}

void ProjectSession::removeOwnAutosave()
{
    if (autosaveWritten_ != juce::File())
    {
        autosaveWritten_.deleteFile();
        sidecarOf(autosaveWritten_).deleteFile();
    }
    autosaveWritten_ = juce::File();
    dirtySinceMs_ = 0.0;
}

void ProjectSession::autosaveIfDue()
{
    const int interval = autosaveInterval_ ? autosaveInterval_() : 0;
    if (interval <= 0 || dirtySinceMs_ <= 0.0 || gestureOpen_)
        return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    // Due once the interval has passed since the first unsaved change, and the
    // edits have paused for a second (not in the middle of a burst of commands).
    if (now - dirtySinceMs_ >= interval * 1000.0 && now - lastEditMs_ >= 1000.0)
        autosaveNow();
}

std::vector<ProjectSession::Autosave> ProjectSession::leftoverAutosaves() const
{
    std::vector<Autosave> found;
    if (!autosaveFolder_.isDirectory())
        return found;
    for (const auto& file : autosaveFolder_.findChildFiles(juce::File::findFiles, false, "*.aural"))
    {
        if (file == autosaveWritten_)
            continue;
        Autosave a;
        a.file = file;
        a.time = file.getLastModificationTime();
        a.name = file.getFileNameWithoutExtension().toStdString();
        const auto meta = juce::JSON::parse(sidecarOf(file).loadFileAsString());
        if (const auto* object = meta.getDynamicObject())
        {
            const auto path = object->getProperty("project").toString();
            if (path.isNotEmpty())
                a.project = juce::File(path);
            const auto name = object->getProperty("name").toString();
            if (name.isNotEmpty())
                a.name = name.toStdString();
            if (const auto ms = static_cast<juce::int64>(object->getProperty("time")); ms > 0)
                a.time = juce::Time(ms);
        }
        if (a.project != juce::File())
            a.name = a.project.getFileNameWithoutExtension().toStdString();
        found.push_back(std::move(a));
    }
    std::sort(
        found.begin(), found.end(), [](const Autosave& x, const Autosave& y) { return x.time > y.time; });
    return found;
}

std::string ProjectSession::restoreAutosave(const Autosave& autosave)
{
    std::string error;
    const auto loaded = core::projectFromJson(autosave.file.loadFileAsString().toStdString(), &error);
    if (!loaded.has_value())
        return error.empty() ? "could not read the autosave" : error;
    removeOwnAutosave();
    untitledStem_.clear();
    hostedSent_.clear();
    engine_.resetCaches();
    project_ = *loaded;
    plugins::strip::ensureChannelStrip(project_);
    replaceLegacySynth();
    project_.adoptInstruments([this](const std::string& type) { return isInstrumentType(type); });
    undo_.clear();
    projectFile_ = autosave.project;
    modified_ = true; // not saved until the user saves it
    discardAutosave(autosave);
    edited();
    publish();
    return {};
}

void ProjectSession::discardAutosave(const Autosave& autosave)
{
    autosave.file.deleteFile();
    sidecarOf(autosave.file).deleteFile();
}

// ----- transport ---------------------------------------------------------------------------

void ProjectSession::play()
{
    if (!engine_.isPlaying())
        engine_.resetIntegratedLoudness();
    engine_.play();
}

void ProjectSession::stop()
{
    finishAutomationPasses();
    engine_.stop();
}

void ProjectSession::togglePlay()
{
    if (engine_.isPlaying() || engine_.isRecording())
        stop();
    else
        engine_.play();
}

void ProjectSession::returnToStart()
{
    engine_.locate(0);
}

void ProjectSession::locate(core::SamplePosition position)
{
    engine_.locate(position);
    disk_.poke(); // streamed clips rebuild their windows at the new position sooner
}

std::string ProjectSession::sampleRateWarning() const
{
    const double deviceRate = engine_.deviceSampleRate();
    if (deviceRate <= 0.0 || deviceRate == project_.sampleRate)
        return {};
    return "The device runs at " + std::to_string(static_cast<int>(deviceRate)) +
           " Hz but the project is at " + std::to_string(static_cast<int>(project_.sampleRate)) +
           " Hz; playback will be pitched.";
}

// ----- metronome, loop, punch ------------------------------------------------------------------

void ProjectSession::setMetronome(bool enabled)
{
    metronome_ = enabled;
    engine_.setMetronome(enabled, metronomeGain_);
    notify(Change::structure);
}

void ProjectSession::setMetronomeLevel(float gain)
{
    metronomeGain_ = std::clamp(gain, 0.0f, 1.0f);
    engine_.setMetronome(metronome_, metronomeGain_);
}

void ProjectSession::setPluginSearchPaths(std::vector<std::string> folders)
{
    scanner_.setExtraSearchPaths(std::move(folders));
}

void ProjectSession::setCountInBars(int bars)
{
    countInBars_ = std::clamp(bars, 0, 4);
    notify(Change::structure);
}

void ProjectSession::setRegion(core::SamplePosition start, core::SamplePosition end)
{
    regionStart_ = std::max<core::SamplePosition>(0, std::min(start, end));
    regionEnd_ = std::max(start, end);
    engine_.setLoop(regionStart_, regionEnd_, loop_);
    engine_.setPunch(regionStart_, regionEnd_, punch_);
    notify(Change::structure);
}

void ProjectSession::setLoopEnabled(bool enabled)
{
    loop_ = enabled;
    engine_.setLoop(regionStart_, regionEnd_, loop_);
    notify(Change::structure);
}

void ProjectSession::setPunchEnabled(bool enabled)
{
    punch_ = enabled;
    engine_.setPunch(regionStart_, regionEnd_, punch_);
    notify(Change::structure);
}

// ----- recording ---------------------------------------------------------------------------

juce::File ProjectSession::mediaFolder() const
{
    if (projectFile_.existsAsFile() || projectFile_.getFullPathName().isNotEmpty())
        return projectFile_.getSiblingFile(projectFile_.getFileNameWithoutExtension() + " Media");
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Aural")
        .getChildFile("Recordings");
}

core::SamplePosition ProjectSession::recordingLatencySamples() const
{
    // What the player hears lags the timeline by the device round trip plus the
    // plugin delay compensation on the master, so a take recorded against it
    // lands that much later than it was played.
    const auto graph = static_cast<core::SamplePosition>(engine_.outputLatencySamples());
    if (const auto calibration = audio_.currentCalibration())
        return calibration->roundTripSamples + graph;
    const auto s = audio_.status();
    return s.inputLatencySamples + s.outputLatencySamples + graph;
}

core::SamplePosition ProjectSession::midiRecordingLatencySamples() const
{
    // A MIDI note arrives as it is played, but the player plays to what they
    // hear, which lags by the output path (device output and the master's delay
    // compensation); the input side of the round trip does not apply.
    const auto graph = static_cast<core::SamplePosition>(engine_.outputLatencySamples());
    const auto s = audio_.status();
    return s.outputLatencySamples + graph;
}

namespace
{
// Beside every recording's first file sits "<base>.recording.json" with what the
// writer cannot know and recovery needs: the track, where capture started, the loop
// start for later takes and the latency the clip should be shifted back by.
constexpr const char* sidecarSuffix = ".recording.json";
constexpr const char* partialSuffix = ".partial.wav";

struct RecordingName
{
    juce::String base; // "<track>-<stamp>-<n>" without any take suffix
    int takeIndex = 0; // 0 for "<base>.partial.wav", n-1 for "<base>-take<n>.partial.wav"
};

RecordingName parseRecordingName(const juce::String& fileName)
{
    RecordingName name;
    name.base = fileName.endsWith(partialSuffix)
                    ? fileName.dropLastCharacters(static_cast<int>(std::strlen(partialSuffix)))
                    : fileName;
    const auto dash = name.base.lastIndexOf("-take");
    if (dash >= 0)
    {
        const auto digits = name.base.substring(dash + 5);
        if (digits.isNotEmpty() && digits.containsOnly("0123456789"))
        {
            name.takeIndex = std::max(0, digits.getIntValue() - 1);
            name.base = name.base.substring(0, dash);
        }
    }
    return name;
}

// A base name no existing file in the folder starts with (as "<base>.*" or
// "<base>-take*"), so a new recording never lands on an old one.
juce::String uniqueRecordingBase(const juce::File& folder, const juce::String& wanted)
{
    for (int attempt = 0;; ++attempt)
    {
        const auto candidate = attempt == 0 ? wanted : wanted + "-" + juce::String(attempt + 1);
        if (folder.findChildFiles(juce::File::findFiles, false, candidate + ".*").isEmpty() &&
            folder.findChildFiles(juce::File::findFiles, false, candidate + "-take*").isEmpty())
            return candidate;
    }
}

struct RecordingMeta
{
    core::Id trackId = core::invalidId;
    std::string trackName;
    core::SamplePosition startPosition = 0;
    core::SamplePosition loopStart = -1; // where takes after the first start; -1 = no loop
    double sampleRate = 0.0;
    core::SamplePosition latencySamples = 0;
};

bool writeSidecar(const juce::File& file, const RecordingMeta& meta)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("format", "aural.recording/1");
    object->setProperty("trackId", static_cast<juce::int64>(meta.trackId));
    object->setProperty("trackName", juce::String(meta.trackName));
    object->setProperty("startPosition", static_cast<juce::int64>(meta.startPosition));
    object->setProperty("loopStart", static_cast<juce::int64>(meta.loopStart));
    object->setProperty("sampleRate", meta.sampleRate);
    object->setProperty("latencySamples", static_cast<juce::int64>(meta.latencySamples));
    return file.replaceWithText(juce::JSON::toString(juce::var(object)));
}

std::optional<RecordingMeta> readSidecar(const juce::File& file)
{
    if (!file.existsAsFile())
        return std::nullopt;
    const auto v = juce::JSON::parse(file);
    if (!v.isObject())
        return std::nullopt;
    RecordingMeta meta;
    meta.trackId = static_cast<core::Id>(static_cast<juce::int64>(v.getProperty("trackId", -1)));
    meta.trackName = v.getProperty("trackName", "").toString().toStdString();
    meta.startPosition = static_cast<juce::int64>(v.getProperty("startPosition", 0));
    meta.loopStart = static_cast<juce::int64>(v.getProperty("loopStart", -1));
    meta.sampleRate = static_cast<double>(v.getProperty("sampleRate", 0.0));
    meta.latencySamples = static_cast<juce::int64>(v.getProperty("latencySamples", 0));
    return meta;
}

// Deletes sidecars whose recording files are all gone.
void removeOrphanSidecars(const juce::File& folder)
{
    for (const auto& sidecar :
         folder.findChildFiles(juce::File::findFiles, false, juce::String("*") + sidecarSuffix))
    {
        const auto base =
            sidecar.getFileName().dropLastCharacters(static_cast<int>(std::strlen(sidecarSuffix)));
        if (folder.findChildFiles(juce::File::findFiles, false, base + partialSuffix).isEmpty() &&
            folder.findChildFiles(juce::File::findFiles, false, base + "-take*" + partialSuffix).isEmpty())
            sidecar.deleteFile();
    }
}
} // namespace

std::string ProjectSession::startRecording()
{
    if (engine_.isRecording())
        return {};
    if (!engine_.isRunning())
        return "no audio device is running";
    // The session captures at the device rate and the files are stamped with the
    // project rate: they must agree, or every take would play at the wrong speed.
    const double deviceRate = engine_.deviceSampleRate();
    if (deviceRate > 0.0 && deviceRate != project_.sampleRate)
        return "the device runs at " + std::to_string(static_cast<int>(deviceRate)) +
               " Hz but the project is at " + std::to_string(static_cast<int>(project_.sampleRate)) +
               " Hz; set the device to the project rate before recording";
    const auto folder = mediaFolder();
    if (!folder.createDirectory().wasOk())
        return "could not create " + folder.getFullPathName().toStdString();

    const double barSamples =
        core::samplesPerBeat(project_.sampleRate, project_.tempoBpm, project_.timeSignature) *
        std::max(1, project_.timeSignature.numerator);
    const auto countIn = static_cast<core::SamplePosition>(std::llround(barSamples * countInBars_));
    const bool punching = punch_ && regionEnd_ > regionStart_;
    const bool looping = loop_ && regionEnd_ > regionStart_;

    // Punching in: roll from one bar before the region so the take has a run-up.
    // (Only once the session is known to start; the locate is harmless otherwise.)
    const auto rollFrom =
        punching
            ? std::max<core::SamplePosition>(0, regionStart_ - static_cast<core::SamplePosition>(barSamples))
            : engine_.position();
    // Capture starts at the punch-in point, or wraps to the loop start first when
    // playback is at or past the loop end.
    core::SamplePosition captureStart = punching ? std::max(regionStart_, rollFrom) : rollFrom;
    if (looping && captureStart >= regionEnd_)
        captureStart = regionStart_;

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    auto session = std::make_unique<engine::RecordSession>(project_.sampleRate);
    int index = 0;
    for (const auto& t : project_.tracks)
    {
        if (t.armed && t.isMidi())
        {
            session->addMidiTrack(t.id);
            continue;
        }
        if (!t.armed || t.inputChannel < 0)
            continue;
        const auto safeName = juce::File::createLegalFileName(juce::String(t.name)).trim();
        const auto base = uniqueRecordingBase(folder,
                                              (safeName.isEmpty() ? juce::String("Track") : safeName) + "-" +
                                                  stamp + "-" + juce::String(++index));
        const auto file = folder.getChildFile(base + partialSuffix);
        RecordingMeta meta;
        meta.trackId = t.id;
        meta.trackName = t.name;
        meta.startPosition = captureStart;
        meta.loopStart = looping ? regionStart_ : -1;
        meta.sampleRate = project_.sampleRate;
        meta.latencySamples = recordingLatencySamples();
        if (!writeSidecar(folder.getChildFile(base + sidecarSuffix), meta))
            return "could not write to " + folder.getFullPathName().toStdString();
        session->addInput(t.id, t.inputChannel, file.getFullPathName().toStdString());
    }
    if (session->inputs().empty() && session->midiTracks().empty())
        return "arm a track and give it an input first";

    if (punching)
        engine_.locate(rollFrom);

    std::string error;
    if (!engine_.startRecording(std::move(session), error, countIn))
    {
        removeOrphanSidecars(folder);
        return error;
    }
    recordingMessage_.clear();
    notify(Change::structure);
    return {};
}

void ProjectSession::finishRecording(engine::AudioEngine::FinishedRecording finished)
{
    const auto latency = recordingLatencySamples();
    std::uint64_t dropped = 0; // per input the count is cumulative over takes: take the largest
    std::int64_t lost = 0;
    int takes = 0;
    int failures = 0;

    undo_.beginGroup("Record");
    for (const auto& r : finished.results)
    {
        dropped = std::max(dropped, r.dropped);
        lost += r.samplesLost;
        juce::File partial{juce::String(r.path)};
        if (!r.ok)
        {
            ++failures;
            // What did reach the disk is still good audio: repair the header (it may
            // be stale) and keep it like any other take.
            if (r.samplesWritten <= 0 || !engine::WavWriter::repair(r.path))
            {
                partial.deleteFile();
                continue;
            }
        }
        else if (r.samplesWritten <= 0)
        {
            partial.deleteFile(); // an empty take (stopped at a loop wrap, or punched out at once)
            continue;
        }
        juce::File final = partial.getSiblingFile(partial.getFileName().replace(partialSuffix, ".wav"))
                               .getNonexistentSibling(false);
        if (!partial.moveFileTo(final))
            final = partial;

        // Each loop pass is its own take, placed at the loop start; later takes sit
        // on top of earlier ones on the same track.
        const auto placement = engine::placeRecording(r.startPosition, r.samplesWritten, latency);
        core::AudioClip clip;
        clip.name = final.getFileNameWithoutExtension().toStdString();
        clip.mediaPath = final.getFullPathName().toStdString();
        clip.start = placement.start;
        clip.sourceOffset = placement.sourceOffset;
        clip.length = placement.length;
        if (clip.length > 0 && project_.findTrack(r.trackId) != nullptr)
        {
            undo_.execute(project_, std::make_unique<core::AddClip>(r.trackId, clip));
            takes = std::max(takes, r.takeIndex + 1);
        }
    }
    // MIDI takes: the captured events become clips, placed like the audio (the
    // player heard the instrument late by the output path, and played to that).
    int midiClips = 0;
    for (auto& take : engine::buildMidiTakes(
             finished.session->capturedMidi(), finished.session->takeSpans(), midiRecordingLatencySamples()))
        if (project_.findTrack(take.trackId) != nullptr)
        {
            take.clip.name = "Take " + std::to_string(++midiClips);
            undo_.execute(project_, std::make_unique<core::AddMidiClip>(take.trackId, take.clip));
        }
    undo_.endGroup();
    modified_ = true;
    removeOrphanSidecars(mediaFolder());

    if (failures > 0)
        recordingMessage_ = "Recording failed on " + std::to_string(failures) +
                            " file(s): " + std::to_string(lost) +
                            " samples never reached the disk; what did is placed";
    else if (dropped > 0)
        recordingMessage_ =
            "Recording has " + std::to_string(dropped) + " samples of silence from disk overruns";
    else if (finished.session->inputs().empty())
        recordingMessage_ = midiClips > 0 ? "Recorded " + std::to_string(midiClips) + " MIDI clip(s)"
                                          : std::string("Recorded no MIDI: nothing was played");
    else
        recordingMessage_ = "Recorded " + std::to_string(finished.session->inputs().size()) + " track(s)" +
                            (takes > 1 ? ", " + std::to_string(takes) + " takes" : std::string{}) +
                            ", placed " + std::to_string(latency) + " samples early for latency";
    publish();
}

void ProjectSession::updateLiveRecording(engine::LiveRecording& into)
{
    // The files closed since the last frame: place the clips now rather than at
    // the next 250 ms poll, so the live takes hand over to them without a gap.
    if (engine_.hasRecordingToCollect())
        if (auto finished = engine_.collectRecording())
            finishRecording(std::move(*finished));
    auto* session = engine_.activeRecording();
    if (session == nullptr)
    {
        into.takes.clear();
        into.midi.clear();
        return;
    }
    into.latency = recordingLatencySamples();
    engine_.updateLiveTakes(into.takes);
    into.midi.clear();
    if (session->midiTracks().empty() || session->startPosition() < 0)
        return; // nothing captured yet (a count-in)
    const auto spans = session->takeSpans();
    const auto midiLatency = midiRecordingLatencySamples();
    into.midi = engine::buildMidiTakes(session->capturedMidi(), spans, midiLatency);
    // The current take shows as a clip even before a note is played.
    const int current = static_cast<int>(spans.size()) - 1;
    for (const auto trackId : session->midiTracks())
    {
        bool shown = false;
        for (const auto& take : into.midi)
            shown = shown || (take.trackId == trackId && take.takeIndex == current);
        if (shown)
            continue;
        engine::MidiTake take;
        take.trackId = trackId;
        take.takeIndex = current;
        take.clip.start = std::max<core::SamplePosition>(0, spans.back().start - midiLatency);
        take.clip.length =
            std::max<core::SamplePosition>(1, spans.back().end - midiLatency - take.clip.start);
        into.midi.push_back(std::move(take));
    }
}

int ProjectSession::countPartialRecordings() const
{
    const auto folder = mediaFolder();
    return folder.isDirectory() ? folder.getNumberOfChildFiles(juce::File::findFiles, "*.partial.wav") : 0;
}

int ProjectSession::discardPartialRecordings()
{
    const auto folder = mediaFolder();
    int discarded = 0;
    if (!folder.isDirectory())
        return 0;
    for (const auto& partial : folder.findChildFiles(juce::File::findFiles, false, "*.partial.wav"))
        if (partial.moveFileTo(
                partial.getSiblingFile(partial.getFileName().replace(partialSuffix, ".discarded.wav"))))
            ++discarded;
    removeOrphanSidecars(folder);
    return discarded;
}

bool ProjectSession::isTrackMonitoring(const core::Track& track) const
{
    engine::TrackNode node;
    node.isMidi = track.isMidi();
    node.inputChannel = track.inputChannel;
    node.armed = track.armed;
    node.monitor = track.monitor;
    return node.isMidi ? engine::Renderer::monitorsMidi(node, engine_.isPlaying(), engine_.isRecording())
                       : engine::Renderer::monitorsInput(node, engine_.isPlaying(), engine_.isRecording());
}

int ProjectSession::recoverPartialRecordings()
{
    const auto folder = mediaFolder();
    if (!folder.isDirectory())
        return 0;
    int recovered = 0;
    std::map<juce::String, core::Id> newTracks; // recording base -> the track made for its takes
    undo_.beginGroup("Recover recordings");
    for (const auto& partial : folder.findChildFiles(juce::File::findFiles, false, "*.partial.wav"))
    {
        const auto path = partial.getFullPathName().toStdString();
        if (!engine::WavWriter::repair(path))
        {
            // Not even a header (killed right after the file was opened): nothing to
            // keep. Anything else unreadable is set aside rather than offered forever.
            if (partial.getSize() < 44)
                partial.deleteFile();
            else
                partial.moveFileTo(
                    partial.getSiblingFile(partial.getFileName().replace(partialSuffix, ".discarded.wav")));
            continue;
        }
        const auto info = engine::readWavInfo(path);
        if (!info.has_value() || info->lengthSamples <= 0)
        {
            partial.deleteFile();
            continue;
        }
        const auto recoveredFile =
            partial.getSiblingFile(partial.getFileName().replace(partialSuffix, ".recovered.wav"))
                .getNonexistentSibling(false);
        if (!partial.moveFileTo(recoveredFile))
            continue;

        // With its sidecar the take goes back where it was captured, on its own
        // track if that still exists; without one it lands at the start of a new
        // track, which is at least audible.
        const auto name = parseRecordingName(partial.getFileName());
        const auto meta = readSidecar(folder.getChildFile(name.base + sidecarSuffix));
        core::AudioClip clip;
        clip.name = recoveredFile.getFileNameWithoutExtension().toStdString();
        clip.mediaPath = recoveredFile.getFullPathName().toStdString();
        clip.length = info->lengthSamples;
        if (meta.has_value())
        {
            const auto captureStart =
                name.takeIndex > 0 && meta->loopStart >= 0 ? meta->loopStart : meta->startPosition;
            const auto placement =
                engine::placeRecording(captureStart, info->lengthSamples, meta->latencySamples);
            clip.start = placement.start;
            clip.sourceOffset = placement.sourceOffset;
            clip.length = placement.length;
        }
        if (clip.length <= 0)
            continue;
        // The original track if it still exists, else one new track per recording
        // (all its takes together), made on the first take.
        core::Id target = meta.has_value() && project_.findTrack(meta->trackId) != nullptr ? meta->trackId
                                                                                           : core::invalidId;
        if (target == core::invalidId)
            if (const auto made = newTracks.find(name.base); made != newTracks.end())
                target = made->second;
        if (target != core::invalidId)
        {
            undo_.execute(project_, std::make_unique<core::AddClip>(target, clip));
        }
        else
        {
            core::Track track;
            track.name = "Recovered: " + (meta.has_value() && !meta->trackName.empty()
                                              ? meta->trackName
                                              : recoveredFile.getFileNameWithoutExtension().toStdString());
            track.clips.push_back(clip);
            auto add = std::make_unique<core::AddTrack>(track);
            const auto* raw = add.get();
            undo_.execute(project_, std::move(add));
            newTracks[name.base] = raw->trackId();
        }
        ++recovered;
    }
    undo_.endGroup();
    removeOrphanSidecars(folder);
    if (recovered > 0)
    {
        modified_ = true;
        recordingMessage_ = "Recovered " + std::to_string(recovered) + " unfinished recording(s)";
        publish();
    }
    return recovered;
}

// ----- bounce --------------------------------------------------------------------------------

std::string ProjectSession::startBounce(BounceJob::Options options)
{
    if (isBouncing())
        return "a bounce is already running";
    if (project_.tracks.empty())
        return "nothing to bounce";
    captureInsertStates(); // the offline engines restore the live plugin states
    stop();
    bounce_ = std::make_unique<BounceJob>(project_, inserts_, std::move(options));
    bounce_->onFinished = [this](const BounceJob::Result& result)
    {
        if (!result.error.empty())
            bounceMessage_ = "Bounce failed: " + result.error;
        else if (result.cancelled)
            bounceMessage_ = "Bounce cancelled";
        else
        {
            const double seconds = static_cast<double>(result.length) / project_.sampleRate;
            const int minutes = static_cast<int>(seconds) / 60;
            const int rest = static_cast<int>(seconds) % 60;
            char lufs[32];
            std::snprintf(lufs, sizeof(lufs), "%.1f", static_cast<double>(result.integratedLufs));
            bounceMessage_ =
                "Bounced " + std::to_string(result.files.size()) + " file(s), " + std::to_string(minutes) +
                ":" + (rest < 10 ? "0" : "") + std::to_string(rest) +
                (result.files.size() == 1 ? std::string(", ") + lufs + " LUFS integrated" : std::string{});
        }
        notify(Change::structure);
    };
    bounceMessage_ = "Bouncing...";
    bounce_->start();
    notify(Change::structure);
    return {};
}

// ----- plugins -------------------------------------------------------------------------------

void ProjectSession::scanPlugins(bool all)
{
    scanner_.start(all);
    notify(Change::structure);
}

void ProjectSession::clearPluginBlocklist()
{
    registry_.clearBlocklist();
    saveRegistry();
    notify(Change::structure);
}

void ProjectSession::retryBlockedPlugin(const std::string& path)
{
    registry_.unblock(path);
    saveRegistry();
    if (!scanner_.isScanning())
        scanner_.start(false);
    notify(Change::structure);
}

void ProjectSession::saveRegistry()
{
    if (registryFile_.getFullPathName().isEmpty())
        return;
    registryFile_.getParentDirectory().createDirectory();
    registryFile_.replaceWithText(juce::String(registry_.toJson()));
}

// Projects from before Aurora played the first built-in synth, which is gone:
// Aurora takes its place (the same slot and id, its own default sound), and
// lanes on the old synth's parameters go with it.
void ProjectSession::replaceLegacySynth()
{
    const auto replace = [](core::Channel& channel, core::Insert& insert)
    {
        const auto id = insert.id;
        insert.type = plugins::aurora::type;
        insert.params = plugins::defaultParameters(insert.type);
        insert.state.clear();
        std::erase_if(
            channel.automation,
            [id](const core::AutomationLane& lane)
            { return lane.target == core::AutomationTarget::insertParameter && lane.insert == id; });
    };
    for (auto& t : project_.tracks)
    {
        if (t.instrument.type == plugins::types::legacySynth)
            replace(t, t.instrument);
        for (auto& i : t.inserts)
            if (i.type == plugins::types::legacySynth)
                replace(t, i);
    }
}

bool ProjectSession::isInstrumentType(const std::string& type) const
{
    if (plugins::BuiltInFactory::isInstrument(type))
        return true;
    const auto* info = registry_.findByType(type);
    return info != nullptr && info->isInstrument;
}

void ProjectSession::captureInsertStates()
{
    const auto capture = [this](core::Channel& channel)
    {
        if (channel.hasInstrument())
            if (const auto live = engine_.insertInstance(channel.instrument.id))
                if (const auto state = live->state(); !state.empty())
                    channel.instrument.state = state;
        for (auto& insert : channel.inserts)
            if (const auto live = engine_.insertInstance(insert.id))
                if (const auto state = live->state(); !state.empty())
                    insert.state = state;
    };
    for (auto& t : project_.tracks)
        capture(t);
    for (auto& b : project_.buses)
        capture(b);
}

// ----- internals ---------------------------------------------------------------------------

void ProjectSession::publish()
{
    engine_.setProject(project_);
    notify(Change::structure);
}

void ProjectSession::matchDeviceSampleRate()
{
    // An empty project simply adopts the device rate; one with media keeps its own
    // rate and the UI shows the mismatch.
    const auto status = audio_.status();
    if (status.open && status.sampleRate > 0.0 && project_.length() == 0 &&
        project_.sampleRate != status.sampleRate)
        project_.sampleRate = status.sampleRate;
}

void ProjectSession::changeListenerCallback(juce::ChangeBroadcaster*)
{
    const double before = project_.sampleRate;
    matchDeviceSampleRate();
    if (project_.sampleRate != before)
        publish();
    else
        notify(Change::structure);
}

void ProjectSession::timerCallback()
{
    poll();
    autosaveIfDue();
    // Write mode starts when playback does; a stop from anywhere finishes the passes.
    if (engine_.isPlaying())
    {
        if (!writePassesStarted_)
            startWritePasses();
    }
    else
    {
        writePassesStarted_ = false;
        if (!passes_.empty())
            finishAutomationPasses();
    }
}

// ----- automation writing ---------------------------------------------------------------

namespace
{
constexpr double rampInSeconds = 0.01;  // from the curve into a written pass
constexpr double rampOutSeconds = 0.05; // from a released touch back to the curve

bool writes(core::AutomationMode mode)
{
    return mode == core::AutomationMode::touch || mode == core::AutomationMode::latch ||
           mode == core::AutomationMode::write;
}

// Drops points that repeat the value before them (a held control at UI rate).
std::vector<core::AutomationPoint> thinned(const std::vector<core::AutomationPoint>& points)
{
    std::vector<core::AutomationPoint> out;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const bool repeat = !out.empty() && std::abs(points[i].value - out.back().value) < 1.0e-5f;
        const bool lastOfRun =
            i + 1 < points.size() && std::abs(points[i + 1].value - points[i].value) >= 1.0e-5f;
        if (!repeat || lastOfRun || i + 1 == points.size())
            out.push_back(points[i]);
    }
    return out;
}
} // namespace

float ProjectSession::staticValue(const core::Channel& channel, const AutomationPass& pass) const
{
    switch (pass.target)
    {
        case core::AutomationTarget::gain:
            return channel.gain;
        case core::AutomationTarget::pan:
            return channel.pan;
        case core::AutomationTarget::insertParameter:
            if (const auto* insert = channel.findInsert(pass.insert);
                insert != nullptr && pass.parameter >= 0 &&
                static_cast<size_t>(pass.parameter) < insert->params.size())
                return insert->params[static_cast<size_t>(pass.parameter)];
            if (const auto live = engine_.insertInstance(pass.insert))
                return live->parameter(pass.parameter);
            return 0.0f;
    }
    return 0.0f;
}

bool ProjectSession::recordAutomation(const core::Command& command, bool pushValue)
{
    if (!engine_.isPlaying())
        return false;
    AutomationPass key;
    float value = 0.0f;
    if (const auto* gain = dynamic_cast<const core::SetTrackGain*>(&command))
    {
        key.channel = gain->channelId();
        key.target = core::AutomationTarget::gain;
        value = gain->value();
    }
    else if (const auto* pan = dynamic_cast<const core::SetTrackPan*>(&command))
    {
        key.channel = pan->channelId();
        key.target = core::AutomationTarget::pan;
        value = pan->value();
    }
    else if (const auto* param = dynamic_cast<const core::SetInsertParameter*>(&command))
    {
        key.channel = param->channelId();
        key.target = core::AutomationTarget::insertParameter;
        key.insert = param->insertId();
        key.parameter = param->parameter();
        value = param->value();
    }
    else
        return false;
    const auto* channel = project_.findChannel(key.channel);
    if (channel == nullptr || !writes(channel->automationMode))
        return false;
    std::shared_ptr<engine::InsertProcessor> instance;
    if (key.target == core::AutomationTarget::insertParameter)
    {
        instance = engine_.insertInstance(key.insert);
        if (instance == nullptr || key.parameter < 0 ||
            key.parameter >= static_cast<int>(instance->parameters().size()))
            return false;
    }
    const auto position = engine_.position();
    auto it = std::find_if(passes_.begin(),
                           passes_.end(),
                           [&](const AutomationPass& p)
                           {
                               return p.channel == key.channel && p.target == key.target &&
                                      p.insert == key.insert && p.parameter == key.parameter;
                           });
    if (it != passes_.end() && !it->points.empty() && position < it->points.back().position)
    {
        // The transport looped back: what was written so far is one pass.
        std::vector<std::unique_ptr<core::Command>> commands;
        commitPass(*it, it->points.back().position, commands);
        it->points.clear();
        if (!commands.empty())
            executeGroupWithoutRecording(commands);
        it->lane = channel->findLane(key.target, key.insert, key.parameter) != nullptr
                       ? channel->findLane(key.target, key.insert, key.parameter)->id
                       : core::invalidId;
        if (it->lane != core::invalidId)
            engine_.setAutomationTouched(it->lane, true);
    }
    if (it == passes_.end())
    {
        key.mode = channel->automationMode;
        if (const auto* lane = channel->findLane(key.target, key.insert, key.parameter))
        {
            key.lane = lane->id;
            engine_.setAutomationTouched(lane->id, true);
        }
        passes_.push_back(key);
        it = passes_.end() - 1;
    }
    if (!it->points.empty() && it->points.back().position == position)
        it->points.back().value = value;
    else
        it->points.push_back({position, value});
    // The value is heard at once (a plugin's own edit is already in the plugin).
    if (!pushValue)
        return true;
    switch (key.target)
    {
        case core::AutomationTarget::gain:
            engine_.setLiveGain(key.channel, value);
            break;
        case core::AutomationTarget::pan:
            engine_.setLivePan(key.channel, value);
            break;
        case core::AutomationTarget::insertParameter:
            instance->setParameter(key.parameter, value);
            break;
    }
    return true;
}

void ProjectSession::startWritePasses()
{
    writePassesStarted_ = true;
    const auto position = engine_.position();
    const auto start = [&](const core::Channel& channel)
    {
        if (channel.automationMode != core::AutomationMode::write)
            return;
        for (const auto& lane : channel.automation)
        {
            AutomationPass pass;
            pass.channel = channel.id;
            pass.target = lane.target;
            pass.insert = lane.insert;
            pass.parameter = lane.parameter;
            pass.mode = core::AutomationMode::write;
            pass.lane = lane.id;
            pass.points.push_back({position, staticValue(channel, pass)});
            engine_.setAutomationTouched(lane.id, true);
            passes_.push_back(std::move(pass));
        }
    };
    for (const auto& t : project_.tracks)
        start(t);
    for (const auto& b : project_.buses)
        start(b);
}

void ProjectSession::commitPass(const AutomationPass& pass,
                                core::SamplePosition end,
                                std::vector<std::unique_ptr<core::Command>>& out) const
{
    const auto* channel = project_.findChannel(pass.channel);
    if (channel == nullptr || pass.points.empty())
        return;
    const auto* lane = channel->findLane(pass.target, pass.insert, pass.parameter);
    const auto recorded = thinned(pass.points);
    const auto rate = project_.sampleRate;
    const auto start = recorded.front().position;
    const bool touch = pass.mode == core::AutomationMode::touch;
    const auto stop = touch ? recorded.back().position : std::max(end, recorded.back().position);
    const auto rampIn = static_cast<core::SamplePosition>(rate * rampInSeconds);
    const auto rampOut = static_cast<core::SamplePosition>(rate * rampOutSeconds);
    const float fallback = staticValue(*channel, pass);
    const auto curveAt = [&](core::SamplePosition at)
    { return lane != nullptr ? lane->valueAt(at) : fallback; };

    std::vector<core::AutomationPoint> points;
    if (lane != nullptr)
        for (const auto& p : lane->points)
            if (p.position < start - rampIn)
                points.push_back(p);
    if (start - rampIn >= 0)
        points.push_back({start - rampIn, curveAt(start - rampIn)});
    points.insert(points.end(), recorded.begin(), recorded.end());
    core::SamplePosition resume = stop;
    if (touch)
    {
        resume = stop + rampOut; // back to the curve after a short glide
        points.push_back({resume, curveAt(resume)});
    }
    else if (stop > recorded.back().position)
        points.push_back({stop, recorded.back().value}); // latch and write hold to the stop
    if (lane != nullptr)
        for (const auto& p : lane->points)
            if (p.position > resume)
                points.push_back(p);

    if (lane != nullptr)
        out.push_back(
            std::make_unique<core::SetAutomationPoints>(pass.channel, lane->id, points, "Write automation"));
    else
    {
        core::AutomationLane added;
        added.target = pass.target;
        added.insert = pass.insert;
        added.parameter = pass.parameter;
        added.points = points;
        out.push_back(std::make_unique<core::AddAutomationLane>(pass.channel, added));
    }
}

void ProjectSession::finishPasses(bool touchOnly)
{
    if (passes_.empty())
        return;
    const auto end = engine_.position();
    std::vector<std::unique_ptr<core::Command>> commands;
    std::vector<core::Id> lanes;
    for (auto it = passes_.begin(); it != passes_.end();)
    {
        if (touchOnly && (it->mode != core::AutomationMode::touch || it->fromPlugin))
        {
            ++it;
            continue;
        }
        commitPass(*it, end, commands);
        if (it->lane != core::invalidId)
            lanes.push_back(it->lane);
        it = passes_.erase(it);
    }
    if (!commands.empty())
        executeGroupWithoutRecording(commands);
    for (const auto lane : lanes)
        engine_.setAutomationTouched(lane, false);
}

void ProjectSession::finishAutomationPasses()
{
    finishPasses(false);
}

void ProjectSession::handlePluginEdit(const core::Channel& channel,
                                      const core::Insert& insert,
                                      const engine::InsertProcessor::ParameterEdit& edit)
{
    using Kind = engine::InsertProcessor::ParameterEdit::Kind;
    if (edit.kind == Kind::value)
    {
        // A plugin reporting back what a lane just sent it is not the user.
        if (const auto sent = hostedSent_.find({insert.id, edit.index});
            sent != hostedSent_.end() && std::abs(sent->second - edit.value) <= 1.0e-4f)
            return;
        if (recordAutomation(core::SetInsertParameter(channel.id, insert.id, edit.index, edit.value), false))
            for (auto& pass : passes_)
                if (pass.insert == insert.id && pass.parameter == edit.index &&
                    pass.target == core::AutomationTarget::insertParameter)
                    pass.fromPlugin = true;
    }
    else if (edit.kind == Kind::end && channel.automationMode == core::AutomationMode::touch)
        finishPass(channel.id, insert.id, edit.index); // the plugin let go of its control
}

void ProjectSession::finishPass(core::Id channel, core::Id insert, int parameter)
{
    const auto it = std::find_if(passes_.begin(),
                                 passes_.end(),
                                 [&](const AutomationPass& p)
                                 {
                                     return p.channel == channel && p.insert == insert &&
                                            p.parameter == parameter &&
                                            p.target == core::AutomationTarget::insertParameter;
                                 });
    if (it == passes_.end())
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commitPass(*it, engine_.position(), commands);
    const auto lane = it->lane;
    passes_.erase(it);
    if (!commands.empty())
        executeGroupWithoutRecording(commands);
    if (lane != core::invalidId)
        engine_.setAutomationTouched(lane, false);
}

void ProjectSession::playHostedAutomation()
{
    // Edits the plugins made themselves (their own editors) first: while writing
    // they become automation, otherwise the plugin simply keeps them.
    {
        std::vector<std::pair<core::Id, core::Id>> hosted; // channel, insert
        const auto collect = [&](const core::Channel& channel)
        {
            if (channel.hasInstrument() && hosting::PluginRegistry::isHostedType(channel.instrument.type))
                hosted.emplace_back(channel.id, channel.instrument.id);
            for (const auto& insert : channel.inserts)
                if (hosting::PluginRegistry::isHostedType(insert.type))
                    hosted.emplace_back(channel.id, insert.id);
        };
        for (const auto& t : project_.tracks)
            collect(t);
        for (const auto& b : project_.buses)
            collect(b);
        for (const auto& [channelId, insertId] : hosted)
        {
            const auto instance = engine_.insertInstance(insertId);
            if (instance == nullptr)
                continue;
            engine::InsertProcessor::ParameterEdit edit;
            for (int n = 0; n < 1024 && instance->takeParameterEdit(edit); ++n)
            {
                const auto* channel = project_.findChannel(channelId);
                const auto* insert = channel != nullptr ? channel->findInsert(insertId) : nullptr;
                if (insert != nullptr)
                    handlePluginEdit(*channel, *insert, edit);
            }
        }
    }
    const auto position = engine_.position();
    const auto play = [&](const core::Channel& channel)
    {
        if (channel.automationMode != core::AutomationMode::read &&
            channel.automationMode != core::AutomationMode::touch &&
            channel.automationMode != core::AutomationMode::latch)
            return;
        for (const auto& lane : channel.automation)
        {
            if (lane.target != core::AutomationTarget::insertParameter || lane.points.empty())
                continue;
            const auto instance = engine_.insertInstance(lane.insert);
            if (instance == nullptr || !instance->ownsParameters() || lane.parameter < 0 ||
                lane.parameter >= static_cast<int>(instance->parameters().size()))
                continue;
            // A lane being written is the user's.
            if (std::any_of(passes_.begin(),
                            passes_.end(),
                            [&](const AutomationPass& p) { return p.lane == lane.id; }))
                continue;
            // Sent whenever the plugin is somewhere else (the lane moved, the
            // instance is new, or the user turned the plugin's own knob in Read).
            const float value = lane.valueAt(position);
            if (std::abs(instance->parameter(lane.parameter) - value) <= 1.0e-4f)
                continue;
            hostedSent_[{lane.insert, lane.parameter}] = value;
            instance->setParameter(lane.parameter, value);
        }
    };
    for (const auto& t : project_.tracks)
        play(t);
    for (const auto& b : project_.buses)
        play(b);
}

void ProjectSession::executeGroupWithoutRecording(std::vector<std::unique_ptr<core::Command>>& commands)
{
    // Inside an open gesture (a pass committed by a loop wrap mid-drag) the
    // commands join the gesture's step; otherwise they are a step of their own.
    if (!gestureOpen_)
        undo_.beginGroup("Write automation");
    for (auto& command : commands)
        undo_.execute(project_, std::move(command));
    if (!gestureOpen_)
        undo_.endGroup();
    modified_ = true;
    edited();
    publish();
}

void ProjectSession::poll()
{
    engine_.collectGarbage();
    // CLAP plugins' main-thread requests (callbacks, latency, flushes while the
    // device is stopped, GUI closures).
    hosting::HostedClapPlugin::serviceAll(engine_.isRunning());
    if (engine_.insertLatencyChanged() || engine_.insertRestartRequested())
        publish(); // a plugin changed its latency or asked for a restart: new snapshot
    if (engine_.hasRecordingToCollect())
        if (auto finished = engine_.collectRecording())
            finishRecording(std::move(*finished));
}

} // namespace aural
