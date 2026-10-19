#include "ProjectSession.h"

#include "audio/AudioDeviceLayer.h"
#include "core/ProjectJson.h"

namespace aural
{

namespace
{
constexpr int garbageTimerMilliseconds = 250;
}

ProjectSession::ProjectSession(audio::AudioDeviceLayer& audio)
    : audio_(audio), loader_(disk_), engine_(loader_)
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
    if (engine_.isPlaying())
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
}

} // namespace aural
