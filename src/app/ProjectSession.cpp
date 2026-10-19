#include "ProjectSession.h"

#include "audio/AudioDeviceLayer.h"
#include "core/ProjectJson.h"
#include "engine/WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>

namespace aural
{

namespace
{
constexpr int garbageTimerMilliseconds = 250;
}

ProjectSession::ProjectSession(audio::AudioDeviceLayer& audio)
    : audio_(audio), loader_(disk_), engine_(loader_, inserts_)
{
    audio_.callback().setEngine(&engine_);
    audio_.addChangeListener(this);
    matchDeviceSampleRate();
    publish();
    startTimer(garbageTimerMilliseconds);
}

ProjectSession::~ProjectSession()
{
    stopTimer();
    audio_.removeChangeListener(this);
    // The device must not call into a dead engine: detach, then let the layer stop
    // the device (it shuts down after this object in the application).
    audio_.callback().setEngine(nullptr);
}

// ----- editing --------------------------------------------------------------------------

void ProjectSession::execute(std::unique_ptr<core::Command> command)
{
    undo_.execute(project_, std::move(command));
    modified_ = true;
    publish();
}

void ProjectSession::undo()
{
    if (undo_.undo(project_))
    {
        modified_ = true;
        publish();
    }
}

void ProjectSession::redo()
{
    if (undo_.redo(project_))
    {
        modified_ = true;
        publish();
    }
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
    project_ = core::Project{};
    undo_.clear();
    projectFile_ = juce::File();
    modified_ = false;
    matchDeviceSampleRate();
    publish();
}

std::string ProjectSession::open(const juce::File& file)
{
    std::string error;
    const auto loaded = core::projectFromJson(file.loadFileAsString().toStdString(), &error);
    if (!loaded.has_value())
        return error.empty() ? "could not read project" : error;
    project_ = *loaded;
    undo_.clear();
    projectFile_ = file;
    modified_ = false;
    publish();
    return {};
}

std::string ProjectSession::save(const juce::File& file)
{
    const auto text = core::projectToJson(project_);
    if (!file.replaceWithText(juce::String(text)))
        return "could not write " + file.getFullPathName().toStdString();
    projectFile_ = file;
    modified_ = false;
    sendChangeMessage();
    return {};
}

// ----- transport ---------------------------------------------------------------------------

void ProjectSession::play()
{
    engine_.play();
}

void ProjectSession::stop()
{
    engine_.stop();
}

void ProjectSession::togglePlay()
{
    if (engine_.isPlaying() || engine_.isRecording())
        engine_.stop();
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
    engine_.setMetronome(enabled, 0.5f);
    sendChangeMessage();
}

void ProjectSession::setCountInBars(int bars)
{
    countInBars_ = std::clamp(bars, 0, 4);
    sendChangeMessage();
}

void ProjectSession::setRegion(core::SamplePosition start, core::SamplePosition end)
{
    regionStart_ = std::max<core::SamplePosition>(0, std::min(start, end));
    regionEnd_ = std::max(start, end);
    engine_.setLoop(regionStart_, regionEnd_, loop_);
    engine_.setPunch(regionStart_, regionEnd_, punch_);
    sendChangeMessage();
}

void ProjectSession::setLoopEnabled(bool enabled)
{
    loop_ = enabled;
    engine_.setLoop(regionStart_, regionEnd_, loop_);
    sendChangeMessage();
}

void ProjectSession::setPunchEnabled(bool enabled)
{
    punch_ = enabled;
    engine_.setPunch(regionStart_, regionEnd_, punch_);
    sendChangeMessage();
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
    if (const auto calibration = audio_.currentCalibration())
        return calibration->roundTripSamples;
    const auto s = audio_.status();
    return s.inputLatencySamples + s.outputLatencySamples;
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
    if (session->inputs().empty())
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
    sendChangeMessage();
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
    else
        recordingMessage_ = "Recorded " + std::to_string(finished.session->inputs().size()) + " track(s)" +
                            (takes > 1 ? ", " + std::to_string(takes) + " takes" : std::string{}) +
                            ", placed " + std::to_string(latency) + " samples early for latency";
    publish();
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
    node.inputChannel = track.inputChannel;
    node.armed = track.armed;
    node.monitor = track.monitor;
    return engine::Renderer::monitorsInput(node, engine_.isPlaying(), engine_.isRecording());
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

// ----- internals ---------------------------------------------------------------------------

void ProjectSession::publish()
{
    engine_.setProject(project_);
    sendChangeMessage();
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
        sendChangeMessage();
}

void ProjectSession::timerCallback()
{
    poll();
}

void ProjectSession::poll()
{
    engine_.collectGarbage();
    if (engine_.hasRecordingToCollect())
        if (auto finished = engine_.collectRecording())
            finishRecording(std::move(*finished));
}

} // namespace aural
