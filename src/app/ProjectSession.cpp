#include "ProjectSession.h"

#include "audio/AudioDeviceLayer.h"
#include "core/ProjectJson.h"
#include "engine/WavFile.h"

#include <algorithm>
#include <cmath>

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

std::string ProjectSession::startRecording()
{
    if (engine_.isRecording())
        return {};
    const auto folder = mediaFolder();
    if (!folder.createDirectory().wasOk())
        return "could not create " + folder.getFullPathName().toStdString();

    const auto stamp = juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    auto session = std::make_unique<engine::RecordSession>(project_.sampleRate);
    int index = 0;
    for (const auto& t : project_.tracks)
    {
        if (!t.armed || t.inputChannel < 0)
            continue;
        const auto safeName = juce::File::createLegalFileName(juce::String(t.name)).trim();
        const auto file = folder.getChildFile((safeName.isEmpty() ? juce::String("Track") : safeName) + "-" +
                                              stamp + "-" + juce::String(++index) + ".partial.wav");
        session->addInput(t.id, t.inputChannel, file.getFullPathName().toStdString());
    }
    if (session->inputs().empty())
        return "arm a track and give it an input first";

    const double barSamples =
        core::samplesPerBeat(project_.sampleRate, project_.tempoBpm, project_.timeSignature) *
        std::max(1, project_.timeSignature.numerator);
    const auto countIn = static_cast<core::SamplePosition>(std::llround(barSamples * countInBars_));

    // Punching in: roll from one bar before the region so the take has a run-up.
    if (punch_ && regionEnd_ > regionStart_)
        engine_.locate(
            std::max<core::SamplePosition>(0, regionStart_ - static_cast<core::SamplePosition>(barSamples)));

    std::string error;
    if (!engine_.startRecording(std::move(session), error, countIn))
        return error;
    recordingMessage_.clear();
    sendChangeMessage();
    return {};
}

void ProjectSession::finishRecording(engine::AudioEngine::FinishedRecording finished)
{
    const auto latency = recordingLatencySamples();
    std::uint64_t dropped = 0; // per input the count is cumulative over takes: take the largest
    int takes = 0;

    undo_.beginGroup("Record");
    for (const auto& r : finished.results)
    {
        dropped = std::max(dropped, r.dropped);
        if (!r.ok || r.samplesWritten <= 0)
            continue;
        juce::File partial{juce::String(r.path)};
        juce::File final = partial.getSiblingFile(partial.getFileName().replace(".partial.wav", ".wav"));
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

    if (dropped > 0)
        recordingMessage_ = "Recording lost " + std::to_string(dropped) + " samples to disk overruns";
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
                partial.getSiblingFile(partial.getFileName().replace(".partial.wav", ".discarded.wav"))))
            ++discarded;
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
    for (const auto& partial : folder.findChildFiles(juce::File::findFiles, false, "*.partial.wav"))
    {
        const auto path = partial.getFullPathName().toStdString();
        if (!engine::WavWriter::repair(path))
            continue;
        const auto info = engine::readWavInfo(path);
        if (!info.has_value() || info->lengthSamples <= 0)
        {
            partial.deleteFile();
            continue;
        }
        const auto recoveredFile =
            partial.getSiblingFile(partial.getFileName().replace(".partial.wav", ".recovered.wav"));
        if (!partial.moveFileTo(recoveredFile))
            continue;
        core::Track track;
        track.name = "Recovered: " + recoveredFile.getFileNameWithoutExtension().toStdString();
        core::AudioClip clip;
        clip.name = track.name;
        clip.mediaPath = recoveredFile.getFullPathName().toStdString();
        clip.length = info->lengthSamples;
        track.clips.push_back(clip);
        undo_.execute(project_, std::make_unique<core::AddTrack>(track));
        ++recovered;
    }
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
    engine_.collectGarbage();
    if (engine_.hasRecordingToCollect())
        if (auto finished = engine_.collectRecording())
            finishRecording(std::move(*finished));
}

} // namespace aural
