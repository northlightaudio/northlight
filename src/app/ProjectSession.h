#pragma once

#include "Bounce.h"
#include "SampleFiles.h"
#include "audio/JuceMediaLoader.h"
#include "audio/PeakStore.h"
#include "core/Command.h"
#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/DiskThread.h"
#include "engine/Stretch.h"
#include "engine/TempoDetect.h"
#include "engine/pitch/VocalService.h"
#include "hosting/HostFactory.h"
#include "hosting/PluginRegistry.h"
#include "hosting/PluginScanner.h"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

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
    // Vocal pitch editing: analyses, and the renders the engine plays.
    engine::VocalService& vocal() noexcept { return vocal_; }
    engine::StretchService& stretch() noexcept { return stretch_; }
    // Guesses the tempo of a clip's media (its name, beats and length), for
    // stretching it to the project's tempo. 0 when nothing could be guessed.
    engine::TempoGuess guessClipTempo(core::Id trackId, core::Id clipId);
    // Stretches a clip so it plays at the project's tempo, given the tempo it was
    // recorded at, and makes it follow tempo changes from now on.
    void conformClipTempo(core::Id trackId, core::Id clipId, double sourceBpm);
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
    // The last bounce's outcome, as a sentence for the completion toast ("Bouncing..."
    // while it runs), and its result (the files, for Show in folder).
    const std::string& lastBounceMessage() const noexcept { return bounceMessage_; }
    const BounceJob::Result& lastBounceResult() const noexcept { return bounceResult_; }

    // ----- plugins ----------------------------------------------------------------
    // Scans the VST3 folders in the background (changed files only unless `all`);
    // the registry is saved when it finishes and listeners are notified as it goes.
    void scanPlugins(bool all = false);
    bool isScanningPlugins() const noexcept { return scanner_.isScanning(); }
    void clearPluginBlocklist();
    void cancelPluginScan() { scanner_.cancel(); }
    std::pair<int, int> pluginScanProgress() const { return {scanner_.scanned(), scanner_.total()}; }
    // Forgets a blocked file and scans again so it gets another try.
    void retryBlockedPlugin(const std::string& path);
    std::string pluginScanStatus() const { return scanner_.status(); }
    audio::PeakStore& peakStore() noexcept { return peaks_; }

    // ----- editing ----------------------------------------------------------------
    void execute(std::unique_ptr<core::Command> command);
    // What the changes notified since the last call touched, so views can
    // refresh only what shows it (a knob drag notifies many times a second).
    // Clears it.
    enum class Change
    {
        structure,  // anything: refresh everything
        values,     // live values only (gain, pan, mute, bypass, parameters)
        parameters, // insert and strip parameters only
    };
    Change takeChange() noexcept;
    // See the implementation: the model's parameter list of a plugin that owns its
    // parameters follows the instance before a command touches one of them.
    void mirrorOwnedParameters(const std::vector<core::Command::ParameterTouch>& touches);
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
    std::string undoName() const { return undo_.undoName(); }
    std::string redoName() const { return undo_.redoName(); }
    bool canRedo() const noexcept { return undo_.canRedo(); }

    // Adds a track with the file as a clip starting at `at`. Returns an error
    // message or empty on success.
    std::string importAudioFile(const juce::File& file, core::SamplePosition at);
    // Adds a MIDI track with the built-in synth as its instrument, armed and
    // listening to every input. Returns the new track's id.
    // A MIDI track playing a new instrument channel (Aurora unless a type is
    // given); returns the MIDI track.
    core::Id addMidiTrack(const std::string& instrumentType = {});
    // A MIDI track that plays an existing instrument channel (placed just above
    // it), or plays nothing when `channel` is invalidId (placed at the end).
    core::Id addMidiTrackPlaying(core::Id channel);
    // A new instrument channel playing `type`, placed under the MIDI track, which
    // is aimed at it (one step). Returns the channel's id, or invalidId.
    core::Id playNewInstrument(core::Id midiTrack, const std::string& type);
    // A MIDI track playing a new Sampler with the file loaded (its start and end
    // points at these fractions of the file), as one step; the MIDI track's id.
    core::Id addSamplerTrack(const juce::File& file, double startFraction = 0.0, double endFraction = 1.0);
    // Auditions a file over the mix through the device (the browser's Files tab):
    // what plays now fades out first. An error, or empty.
    std::string previewFile(const juce::File& file);
    void stopPreview();
    void setPreviewGain(float linear) { engine_.preview().setGain(linear); }
    bool isPreviewing() const noexcept { return engine_.preview().isPlaying(); }
    // Hands the next preview over once the last has faded (a UI timer while previewing).
    void pumpPreview() { engine_.preview().pump(engine_.isRunning()); }
    // The Sampler's decoded files (the insert factory's loader).
    SampleFiles& sampleFiles() noexcept { return sampleFiles_; }
    // A plugin chosen for a channel (the browser's double-click or drop). An
    // instrument goes to an instrument channel: the one given, or the one the
    // given MIDI track plays (a new one under it when it plays none), or a new
    // MIDI track and channel. An effect is added to the channel's inserts.
    enum class PluginPlacement
    {
        instrumentReplaced, // on an existing instrument channel
        newInstrument,      // a new instrument channel (the result's channel)
        effectAdded,
        noChannel, // an effect with no channel to go on: nothing happened
    };
    struct PluginInsertion
    {
        PluginPlacement placement = PluginPlacement::noChannel;
        core::Id channel = core::invalidId;   // where it went
        core::Id midiTrack = core::invalidId; // a new instrument: the MIDI track that plays it
    };
    PluginInsertion insertPlugin(core::Id channelId, const std::string& type);
    // Whether an insert type is an instrument (a built-in synth or a scanned plugin
    // the registry calls one).
    bool isInstrumentType(const std::string& type) const;

    // ----- files ------------------------------------------------------------------
    void newProject();
    std::string open(const juce::File& file);
    std::string save(const juce::File& file);
    const juce::File& projectFile() const noexcept { return projectFile_; }
    bool isModified() const noexcept { return modified_; }

    // ----- templates ----------------------------------------------------------------
    // Templates are projects without clips, notes or markers (tracks, buses,
    // routing, inserts, the strips, automation modes), in Documents/Aural/Templates.
    juce::File templatesFolder() const;
    void setTemplatesFolder(juce::File folder) { templatesFolder_ = std::move(folder); } // tests
    std::vector<juce::File> templates() const;
    // Saves the current project's layout as a template. Returns an error or empty.
    std::string saveAsTemplate(const juce::String& name);
    // Starts a new, unsaved project from a template. Returns an error or empty.
    std::string newFromTemplate(const juce::File& file);

    // ----- media management ---------------------------------------------------------
    // Media the clips use that lives outside the project's media folder, and files
    // in the media folder that no clip uses.
    struct MediaReport
    {
        std::vector<juce::File> outside;
        std::vector<juce::File> unused;
        std::vector<juce::File> missing;
    };
    MediaReport mediaReport() const;
    // Copies the outside media into the media folder and points the clips there
    // (one undo step). Needs a saved project. Returns an error or empty;
    // `copied` gets the number of files copied.
    std::string consolidateMedia(int& copied);
    // Moves unused media (and their peak files) to the Recycle Bin. Returns how many.
    int removeUnusedMedia();

    // ----- autosave -----------------------------------------------------------------
    // While the project has unsaved changes it is written to `folder` every
    // `intervalSeconds()` (0 = off), once edits pause for a second. Saving,
    // starting another project or closing normally deletes the copy, so what is
    // left in the folder at launch is what a session that did not close left.
    void setAutosave(juce::File folder, std::function<int()> intervalSeconds);
    struct Autosave
    {
        juce::File file;    // the project as it was autosaved
        juce::File project; // the file it belongs to (empty for an unsaved project)
        juce::Time time;
        std::string name;
    };
    // Autosaves left by earlier sessions, newest first.
    std::vector<Autosave> leftoverAutosaves() const;
    // Opens an autosave as the current project (unsaved, belonging to its file);
    // the leftover is removed. Returns an error or empty.
    std::string restoreAutosave(const Autosave& autosave);
    static void discardAutosave(const Autosave& autosave);
    // Writes the autosave now if there are unsaved changes; returns whether it wrote.
    bool autosaveNow();
    juce::File currentAutosaveFile() const;

    // ----- automation writing ---------------------------------------------------------
    // While the transport plays, a gain, pan or built-in parameter moved on a
    // channel in touch, latch or write mode writes its lane: the value goes
    // straight to the engine (the model's static value and the undo history are
    // left alone) and the lane stops playing while it is written. Touch ends at
    // the end of the gesture; latch and write when the transport stops; write
    // also rewrites the channel's existing lanes from where playback started.
    // Each finished pass becomes one undoable "Write automation" step.
    bool isWritingAutomation() const noexcept { return !passes_.empty(); }
    // Finishes every pass now (the transport stopped; tests call it directly).
    void finishAutomationPasses();
    // Hosted plugins own their parameters, so their lanes play from here (50
    // times a second, on the message thread) rather than on the audio thread.
    // Public for tests.
    void playHostedAutomation();

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
    void setMetronomeLevel(float gain);
    // Extra folders the plugin scanner searches besides the standard locations.
    void setPluginSearchPaths(std::vector<std::string> folders);
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
    // The same for MIDI: only the output side (the player plays to what they hear).
    core::SamplePosition midiRecordingLatencySamples() const;
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
    // The latest plugin that crashed while processing (it was switched off), or empty.
    const std::string& lastPluginMessage() const noexcept { return pluginMessage_; }
    // Brings `into` up to date with the recording in progress (its audio takes'
    // peaks so far and its MIDI takes), for the arrange view to draw it growing;
    // clears it when nothing is recording. Call once per UI frame. Finished
    // recordings are collected here too, so their clips replace the live takes in
    // the same frame.
    void updateLiveRecording(engine::LiveRecording& into);
    // Collects finished recordings (placing their clips) and engine garbage. A timer
    // calls this every 250 ms; tests call it directly.
    void poll();

    // Non-empty when the device rate differs from the project rate.
    std::string sampleRateWarning() const;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void publish();
    class SessionKeys;
    // The pieces every "new MIDI track / new instrument" path shares.
    core::Track makeInstrumentChannel(const std::string& type) const;
    std::string nextMidiTrackName() const;
    // A project just read from a file: its parameters put where the processors want them, then published.
    void adoptLoadedParameters();
    // Applies through the undo stack; false (and nothing changed) when the command
    // refers to something that is gone.
    bool applyChecked(std::unique_ptr<core::Command> command);
    // The one way an edit is applied: the default strip for a new channel, an
    // automation write while writing, the live plugin state captured before a
    // removal, the undo stack, then the engine work its change kind needs
    // (deferred to the end of an open Batch). False when the command was stale.
    bool run(std::unique_ptr<core::Command> command, bool mayWriteAutomation = true);
    // Does the engine work for the changes run since the last call: the widest
    // change kind decides (compile, push values, push parameters, or nothing).
    void finishChange(core::Command::ChangeKind kind,
                      const std::vector<core::Command::ParameterTouch>& touches);
    // Several runs as one undo step (when named) with the engine work done once,
    // at the end of the outermost batch.
    class Batch;
    int batchDepth_ = 0;
    std::optional<core::Command::ChangeKind> pendingKind_;
    std::vector<core::Command::ParameterTouch> pendingTouches_;
    void matchDeviceSampleRate();
    void finishRecording(engine::AudioEngine::FinishedRecording finished);
    void saveRegistry();
    // Every change notification goes through here; a structural one marks the
    // pending notification as more than values.
    void notify(Change change);
    struct AutomationPass
    {
        core::Id channel = core::invalidId;
        core::AutomationTarget target = core::AutomationTarget::gain;
        core::Id insert = core::invalidId;
        int parameter = 0;
        core::AutomationMode mode = core::AutomationMode::touch;
        core::Id lane = core::invalidId; // the lane being rewritten (touched in the engine), if any
        std::vector<core::AutomationPoint> points;
        bool fromPlugin = false; // driven by the plugin's own controls: ended by its gesture, not ours
    };
    bool recordAutomation(const core::Command::AutomationWrite& write, bool pushValue = true);
    // An edit a hosted plugin made itself: recorded while writing.
    void handlePluginEdit(const core::Channel& channel,
                          const core::Insert& insert,
                          const engine::InsertProcessor::ParameterEdit& edit);
    void finishPass(core::Id channel, core::Id insert, int parameter);
    void startWritePasses();
    void finishPasses(bool touchOnly);
    void executeGroupWithoutRecording(std::vector<std::unique_ptr<core::Command>>& commands);
    void commitPass(const AutomationPass& pass,
                    core::SamplePosition end,
                    std::vector<std::unique_ptr<core::Command>>& out) const;
    float staticValue(const core::Channel& channel, const AutomationPass& pass) const;
    void edited();
    void removeOwnAutosave();
    void autosaveIfDue();
    // Pulls each hosted plugin's current state into the model before a save.
    void captureInsertStates();

    audio::AudioDeviceLayer& audio_;
    engine::DiskThread disk_;
    audio::JuceMediaLoader loader_;
    SampleFiles sampleFiles_; // before the factory that loads through it
    juce::File registryFile_;
    hosting::PluginRegistry registry_;
    hosting::PluginScanner scanner_;
    hosting::HostFactory inserts_;
    engine::AudioEngine engine_;
    // A render or an analysis finished (on the vocal worker): recompile so the
    // clips play it. Declared before the service, so it outlives the worker.
    struct VocalUpdates final : juce::AsyncUpdater
    {
        std::function<void()> onUpdate;
        void handleAsyncUpdate() override
        {
            if (onUpdate)
                onUpdate();
        }
    } vocalUpdates_, stretchUpdates_;
    audio::JuceMediaLoader vocalLoader_{disk_, true}; // whole files: the renderer reads anywhere
    engine::VocalService vocal_{vocalLoader_};
    // Stretched and transposed clips render in the background (two workers); a
    // finished render repaints the clips that show its progress.
    audio::JuceMediaLoader stretchLoader_{disk_, true};
    engine::StretchService stretch_{stretchLoader_};
    audio::PeakStore peaks_;
    core::Project project_;
    core::UndoStack undo_;
    juce::File projectFile_;
    bool modified_ = false;
    std::string recordingMessage_;
    std::string pluginMessage_;
    std::set<core::Id> reportedCrashes_; // inserts whose crash the user has been told about
    void checkCrashedPlugins();
    std::string bounceMessage_;
    BounceJob::Result bounceResult_;
    std::unique_ptr<BounceJob> bounce_;
    bool gestureOpen_ = false;
    bool metronome_ = false;
    float metronomeGain_ = 0.5f;
    int countInBars_ = 1;
    core::SamplePosition regionStart_ = 0, regionEnd_ = 0;
    bool loop_ = false;
    bool punch_ = false;
    bool recordingPunched_ = false; // the recording in progress punches in (its passes become takes)
    juce::File templatesFolder_;
    juce::File autosaveFolder_;
    std::function<int()> autosaveInterval_;
    juce::File autosaveWritten_; // the copy this session last wrote
    juce::String untitledStem_;  // the autosave name of an unsaved project
    double dirtySinceMs_ = 0.0;  // unsaved changes not yet autosaved, since
    double lastEditMs_ = 0.0;
    Change pending_ = Change::parameters; // the widest change notified since the last take
    std::vector<AutomationPass> passes_;
    bool writePassesStarted_ = false;
    // What each hosted lane last sent, per insert and parameter (to tell an
    // echo of it from the plugin from a move by the user); cleared with the project.
    std::map<std::pair<core::Id, int>, float> hostedSent_;
    juce::TimedCallback hostedAutomation_{[this] { playHostedAutomation(); }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectSession)
};

} // namespace aural
