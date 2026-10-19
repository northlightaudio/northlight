#pragma once

#include "audio/JuceMediaLoader.h"
#include "core/Command.h"
#include "core/Model.h"
#include "engine/AudioEngine.h"

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

    // Non-empty when the device rate differs from the project rate.
    std::string sampleRateWarning() const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void publish();
    void matchDeviceSampleRate();

    audio::AudioDeviceLayer& audio_;
    audio::JuceMediaLoader loader_;
    engine::AudioEngine engine_;
    core::Project project_;
    core::UndoStack undo_;
    juce::File projectFile_;
    bool modified_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectSession)
};

} // namespace aural
