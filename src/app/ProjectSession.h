#pragma once

#include "Bounce.h"
#include "audio/JuceMediaLoader.h"
#include "audio/PeakStore.h"
#include "core/Command.h"
#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/DiskThread.h"
#include "hosting/HostFactory.h"
#include "hosting/PluginRegistry.h"
#include "hosting/PluginScanner.h"

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
    // `registryFile` holds the plugin registry (an empty File keeps it in memory).
    explicit ProjectSession(audio::AudioDeviceLayer& audio, juce::File registryFile = {});
    ~ProjectSession() override;

    const core::Project& project() const noexcept { return project_; }
    engine::AudioEngine& engine() noexcept { return engine_; }
    audio::JuceMediaLoader& mediaLoader() noexcept { return loader_; }
    const hosting::HostFactory& insertFactory() const noexcept { return inserts_; }
    const hosting::PluginRegistry& pluginRegistry() const noexcept { return registry_; }
    // The live processor behind a model insert (for its editor), if any.
    std::shared_ptr<engine::InsertProcessor> liveInsert(core::Id insertId) const
    {
        return engine_.insertInstance(insertId);
    }

    // ----- bounce -----------------------------------------------------------------
    // Renders the mix (or stems) offline on a background thread. Returns an error
    // or empty; progress and the result are reported through the job.
    std::string startBounce(BounceJob::Options options);
    bool isBouncing() const noexcept { return bounce_ != nullptr && bounce_->isRunning(); }
    float bounceProgress() const noexcept { return bounce_ != nullptr ? bounce_->progress() : 0.0f; }
    // The last bounce's outcome, as a sentence for the status bar.
    const std::string& lastBounceMessage() const noexcept { return bounceMessage_; }

    // ----- plugins ----------------------------------------------------------------
    // Scans the VST3 folders in the background (changed files only unless `all`);
    // the registry is saved when it finishes and listeners are notified as it goes.
    void scanPlugins(bool all = false);
    bool isScanningPlugins() const noexcept { return scanner_.isScanning(); }
    void clearPluginBlocklist();
    std::string pluginScanStatus() const { return scanner_.status(); }
    audio::PeakStore& peakStore() noexcept { return peaks_; }

    // ----- editing ----------------------------------------------------------------
    void execute(std::unique_ptr<core::Command> command);
    // Several commands as one undo step (a drag that moved many clips).
    void executeGroup(const std::string& name, std::vector<std::unique_ptr<core::Command>> commands);
    // A gesture (a fader drag): every command until endGesture() is one undo step.
    void beginGesture(const std::string& name);
    void endGesture();
    // Sets a clip's gain so its loudest sample peaks at `targetDb` (reads the
    // source). Returns an error message or empty.
    std::string normaliseClip(core::Id trackId, core::Id clipId, float targetDb = -0.3f);
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

    // ----- metronome, loop, punch ---------------------------------------------------
    void setMetronome(bool enabled);
    bool metronomeEnabled() const noexcept { return metronome_; }
    // Bars of click before a recording starts (0 = none).
    void setCountInBars(int bars);
    int countInBars() const noexcept { return countInBars_; }
    // The loop/punch region on the timeline (shared by both).
    void setRegion(core::SamplePosition start, core::SamplePosition end);
    core::SamplePosition regionStart() const noexcept { return regionStart_; }
    core::SamplePosition regionEnd() const noexcept { return regionEnd_; }
    void setLoopEnabled(bool enabled);
    bool loopEnabled() const noexcept { return loop_; }
    // Punch: capture only inside the region; recording starts one bar before it.
    void setPunchEnabled(bool enabled);
    bool punchEnabled() const noexcept { return punch_; }
    bool isCountingIn() const noexcept { return engine_.isCountingIn(); }

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
    // Unfinished recordings (*.partial.wav) left in the media folder by a crash.
    int countPartialRecordings() const;
    // Repairs them and puts each back where it was captured (its sidecar names the
    // track, start and latency), or on a new "Recovered" track without one.
    int recoverPartialRecordings();
    // Sets them aside as *.discarded.wav so they are not offered again.
    int discardPartialRecordings();
    // Whether the track is hearing its input right now (monitor mode, arm and
    // transport state combined).
    bool isTrackMonitoring(const core::Track& track) const;
    // Human-readable note about the last recording (dropouts, recovery), if any.
    const std::string& lastRecordingMessage() const noexcept { return recordingMessage_; }
    // Collects finished recordings (placing their clips) and engine garbage. A timer
    // calls this every 250 ms; tests call it directly.
    void poll();

    // Non-empty when the device rate differs from the project rate.
    std::string sampleRateWarning() const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void publish();
    void matchDeviceSampleRate();
    void finishRecording(engine::AudioEngine::FinishedRecording finished);
    void saveRegistry();
    // Pulls each hosted plugin's current state into the model before a save.
    void captureInsertStates();

    audio::AudioDeviceLayer& audio_;
    engine::DiskThread disk_;
    audio::JuceMediaLoader loader_;
    juce::File registryFile_;
    hosting::PluginRegistry registry_;
    hosting::PluginScanner scanner_;
    hosting::HostFactory inserts_;
    engine::AudioEngine engine_;
    audio::PeakStore peaks_;
    core::Project project_;
    core::UndoStack undo_;
    juce::File projectFile_;
    bool modified_ = false;
    std::string recordingMessage_;
    std::string bounceMessage_;
    std::unique_ptr<BounceJob> bounce_;
    bool gestureOpen_ = false;
    bool metronome_ = false;
    int countInBars_ = 1;
    core::SamplePosition regionStart_ = 0, regionEnd_ = 0;
    bool loop_ = false;
    bool punch_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectSession)
};

} // namespace aural
