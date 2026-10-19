#pragma once

#include "audio/JuceMediaLoader.h"
#include "audio/PeakStore.h"
#include "core/Command.h"
#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/DiskThread.h"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>

namespace aural::audio
{
class AudioDeviceLayer;
}

namespace aural
{

// The open project: the model, its undo history and the engine that plays it.
// Message thread only. Every model edit goes through execute(), which applies the
// command, republishes the engine snapshot and notifies listeners.
class ProjectSession final : public juce::ChangeBroadcaster, private juce::ChangeListener, private juce::Timer
{
public:
    explicit ProjectSession(audio::AudioDeviceLayer& audio);
    ~ProjectSession() override;

    const core::Project& project() const noexcept { return project_; }
    engine::AudioEngine& engine() noexcept { return engine_; }
    audio::JuceMediaLoader& mediaLoader() noexcept { return loader_; }
    audio::PeakStore& peakStore() noexcept { return peaks_; }

    // ----- editing ----------------------------------------------------------------
    void execute(std::unique_ptr<core::Command> command);
    void undo();
    void redo();
    bool canUndo() const noexcept { return undo_.canUndo(); }
    bool canRedo() const noexcept { return undo_.canRedo(); }

    // Adds a track with the file as a clip starting at `at`. Returns an error
    // message or empty on success.
    std::string importAudioFile(const juce::File& file, core::SamplePosition at);

    // ----- files ------------------------------------------------------------------
    void newProject();
    std::string open(const juce::File& file);
    std::string save(const juce::File& file);
    const juce::File& projectFile() const noexcept { return projectFile_; }
    bool isModified() const noexcept { return modified_; }

    // ----- transport --------------------------------------------------------------
    void play();
    void stop();
    void togglePlay();
    void returnToStart();
    void locate(core::SamplePosition position);
    bool isPlaying() const noexcept { return engine_.isPlaying(); }
    core::SamplePosition position() const noexcept { return engine_.position(); }

    // ----- recording ----------------------------------------------------------------
    // Starts recording every armed track that has an input, from the current
    // position. Returns an error message or empty. stop() ends the recording; the
    // files are finished on the disk thread and the clips placed (shifted back by the
    // measured or reported round-trip latency) when they are ready.
    std::string startRecording();
    bool isRecording() const noexcept { return engine_.isRecording(); }
    // The folder new recordings go to: next to the project file, or the user's
    // Documents/Aural/Recordings when the project has not been saved.
    juce::File mediaFolder() const;
    // Latency used to place recordings: the loopback measurement when it matches
    // the device configuration, otherwise the driver's reported input + output.
    core::SamplePosition recordingLatencySamples() const;
    // Repairs and imports any *.partial.wav left in the media folder by a crash.
    // Returns how many were recovered.
    int recoverPartialRecordings();
    // Human-readable note about the last recording (dropouts, recovery), if any.
    const std::string& lastRecordingMessage() const noexcept { return recordingMessage_; }

    // Non-empty when the device rate differs from the project rate.
    std::string sampleRateWarning() const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void publish();
    void matchDeviceSampleRate();
    void finishRecording(engine::AudioEngine::FinishedRecording finished);

    audio::AudioDeviceLayer& audio_;
    engine::DiskThread disk_;
    audio::JuceMediaLoader loader_;
    engine::AudioEngine engine_;
    audio::PeakStore peaks_;
    core::Project project_;
    core::UndoStack undo_;
    juce::File projectFile_;
    bool modified_ = false;
    std::string recordingMessage_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectSession)
};

} // namespace aural
