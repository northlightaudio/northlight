#pragma once

#include "core/Model.h"
#include "core/ParameterTouch.h"
#include "engine/Loudness.h"
#include "engine/Meters.h"
#include "engine/Midi.h"
#include "engine/Preview.h"
#include "engine/Recording.h"
#include "engine/Renderer.h"
#include "engine/Snapshot.h"
#include "engine/Transport.h"
#include "engine/WorkerPool.h"
#include "engine/rt/SnapshotPublisher.h"
#include "engine/rt/SpscQueue.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// The engine façade: the controller side (message thread) publishes snapshots and
// transport commands; the real-time side renders and captures. Exactly the three
// transition mechanisms from docs/rt-rules.md are used:
//
//   controller -> RT   snapshot swap (rt::SnapshotPublisher), record session pointer
//   controller -> RT   SPSC command queue (transport)
//   RT -> controller   atomics for position/playing/recording; record rings
//
// No JUCE: the app's AudioCallback calls process(); tests drive it offline.

namespace northlight::engine
{

class AudioEngine
{
public:
    // workerThreads < 0 picks WorkerPool::recommendedWorkerCount().
    AudioEngine(MediaLoader& loader, const InsertFactory& inserts, int workerThreads = -1);
    ~AudioEngine();

    int workerThreads() const noexcept { return pool_.numWorkers(); }
    int realtimeWorkerThreads() const noexcept { return pool_.realtimeWorkers(); }

    // ----- controller thread ------------------------------------------------------
    // Compiles and publishes the project. Media errors are available afterwards.
    void setProject(const core::Project& project);
    // What clips play is passed through this before a snapshot uses it (vocal edits).
    void setClipSourceHook(MediaCache::ClipSourceHook hook) { media_.setClipSourceHook(std::move(hook)); }
    void setStretchHook(MediaCache::ClipSourceHook hook) { media_.setStretchHook(std::move(hook)); }
    // The hooks have something new for unchanged clips: the next compile asks again.
    void forgetClipSources() { media_.forgetClipSources(); }
    const std::map<std::string, std::string>& mediaErrors() const noexcept { return media_.errors(); }
    // Opening a project in steps (ProjectSession): a file decoded elsewhere for
    // the next compile to use, and an insert's instance made ahead of it (one at
    // a time, so the UI can repaint between plugins). The caller keeps each
    // result alive until the compile has taken it; garbage collection drops
    // what no snapshot holds.
    void adoptMedia(const std::string& path, std::shared_ptr<const AudioSource> source)
    {
        media_.adopt(path, std::move(source));
    }
    std::shared_ptr<InsertProcessor> prepareInsert(const core::Insert& insert)
    {
        return inserts_.get(insert);
    }

    void play();
    void stop();
    void locate(core::SamplePosition position);
    void setLoop(core::SamplePosition start, core::SamplePosition end, bool enabled);
    void setMetronome(bool enabled, float gain);
    void setPunch(core::SamplePosition in, core::SamplePosition out, bool enabled);

    // Recording. The session lists the inputs to capture and the files to write;
    // the writer opens them here, then the real-time thread starts capturing at the
    // next block (and the transport plays). stop() ends it. Once the writer has
    // closed the files, collectRecording() hands back the session and results.
    struct FinishedRecording
    {
        std::unique_ptr<RecordSession> session;
        std::vector<RecordWriter::Result> results;
    };
    // countInSamples of click (metronome on or not, the clicks play) precede capture.
    bool startRecording(std::unique_ptr<RecordSession> session,
                        std::string& error,
                        core::SamplePosition countInSamples = 0);
    bool hasRecordingToCollect() const noexcept;
    std::optional<FinishedRecording> collectRecording();
    // The recording in progress (until it is collected), for drawing it live: the
    // session's controller-side accessors (capturedMidi, takeSpans) may be used
    // while it records. Null when there is none.
    RecordSession* activeRecording() noexcept { return activeRecord_.get(); }
    // Brings `into` up to date with the audio takes of the recording in progress
    // (RecordWriter::updateLiveTakes); clears it when there is none.
    void updateLiveTakes(std::vector<LiveTake>& into) const;

    // Frees retired snapshots and unreferenced media. Call periodically (a UI timer).
    void collectGarbage();
    // Auditioning files over the mix (the browser): controller-side calls.
    PreviewPlayer& preview() noexcept { return preview_; }
    const PreviewPlayer& preview() const noexcept { return preview_; }
    // Forgets cached insert instances and delay lines (a different project may
    // reuse the same ids). Live snapshots keep their own references.
    void resetCaches();

    // Device lifecycle. prepare() is called before the device starts delivering
    // blocks (allocates), release() after it has stopped.
    void prepare(double sampleRate, int maxBlockSize);
    void release();
    double deviceSampleRate() const noexcept { return deviceSampleRate_.load(std::memory_order_relaxed); }
    bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }

    // ----- any thread ---------------------------------------------------------------
    // Peak/RMS per channel, slots as laid out by the snapshot compiler
    // (trackMeterSlot / busMeterSlot / masterMeterSlot). The UI takes readings.
    rt::MeterBlock& meters() noexcept { return meters_; }
    // ----- MIDI -------------------------------------------------------------------
    // Live MIDI from a device (any non-real-time thread): `source` names the input
    // it came from (empty = unknown). The event reaches the MIDI tracks listening
    // to that input in the next block, at its sample offset clamped to the block.
    // Live MIDI from a device. With an arrival time (the steady clock, in
    // nanoseconds) the event plays in the next block at the point it arrived
    // during the last one, so a performance keeps its timing for one block of
    // delay instead of snapping to block starts; without one (0) its
    // sampleOffset is used as given.
    void pushMidiInput(const MidiEvent& event, const std::string& source, std::int64_t arrivalNanos = 0);
    // The clock block starts are stamped with (the steady clock); tests replace it.
    using NanoClock = std::int64_t (*)() noexcept;
    void setClockForTesting(NanoClock clock) noexcept { clock_ = clock; }
    static std::int64_t steadyNanos() noexcept;
    // A note the user auditions (the piano roll): it reaches only the given
    // track's instrument, whether or not the track is armed or monitoring, and
    // is never recorded.
    void pushMidiPreview(const MidiEvent& event, core::Id trackId);
    // MIDI output (any one non-real-time consumer thread): the next event the
    // tracks that play the output produced, and how the sample clock maps to
    // wall time (the clock at the start of the latest block and when it began,
    // in steady_clock nanoseconds).
    bool popMidiOutput(MidiOutEvent& event) noexcept { return midiOut_.pop(event); }
    struct ClockSync
    {
        std::int64_t clock = 0;
        std::int64_t steadyNanos = 0;
    };
    ClockSync clockSync() const noexcept;

    // The engine's index for an input name (registering it the first time), the
    // value TrackNode::midiSource filters on.
    int midiSourceIndex(const std::string& name);

    // The stereo input buses the device offers now, by name (controller thread).
    // Tracks that choose a bus read these inputs from the next setProject().
    void setInputBuses(std::map<std::string, InputPair> buses);
    InputPair inputBus(const std::string& name) const;
    // The stereo output pairs the device has (controller thread): channels played
    // to a hardware output the device lacks go to their bus or the master from the
    // next setProject(). 1 (the default, and offline) plays every one that way.
    void setOutputPairs(int pairs) noexcept { outputPairs_ = std::max(1, pairs); }
    int outputPairs() const noexcept { return outputPairs_; }
    // Forgets an insert's instance (a crashed plugin being reloaded): the next
    // setProject() makes a fresh one from the model's state. A crashed instance
    // is kept alive, never destroyed, so its plugin code is not called again.
    void forgetInsert(core::Id insertId) { inserts_.forget(insertId); }

    // Automation. While the user touches a control to write its lane, the lane
    // is not played (controller thread).
    void setAutomationTouched(core::Id laneId, bool touched);
    // What a channel's automation is playing now, for the UI: which of gain and
    // pan are automated and their values.
    struct AutomatedValues
    {
        bool gain = false, pan = false;
        float gainValue = 1.0f, panValue = 0.0f;
    };
    AutomatedValues automatedValues(core::Id channelId) const;
    // A control's value pushed straight to the running graph without touching
    // the model (a value being written into automation).
    void setLiveGain(core::Id channelId, float gain);
    void setLivePan(core::Id channelId, float pan);

    // Live values: pushed to the running graph without a recompile (the renderer
    // ramps gains and pans; insert parameters are atomics on the instance).
    // `only`: just that channel's values (a fader drag), else every channel's.
    void applyLiveValues(const core::Project& project, core::Id only = core::invalidId);
    // Pushes the model's value of the given insert parameters to their instances,
    // whether or not the processor owns its parameters (a command touched them).
    void pushInsertParameters(const core::Project& project, const std::vector<core::ParameterTouch>& touches);
    // The meter slot of the master in the current snapshot.
    int masterMeterSlot() const noexcept { return masterMeterSlot_.load(std::memory_order_relaxed); }
    // Restarts the integrated loudness measurement.
    void resetIntegratedLoudness() noexcept { loudness_.resetIntegrated(); }
    // Samples the master output lags the timeline because of insert latency
    // (plugin delay compensation aligns every path to the slowest).
    int outputLatencySamples() const noexcept { return outputLatency_.load(std::memory_order_relaxed); }
    // The current snapshot, for tests that check what the compiler produced.
    const Snapshot* snapshotForTest() const noexcept { return publisher_.currentForController(); }
    // True when an insert's latency changed since the last setProject(): recompile.
    bool insertLatencyChanged() const noexcept { return inserts_.latencyChanged(); }
    // A hosted plugin asked to be recreated: setProject() makes the replacement.
    bool insertRestartRequested() const noexcept { return inserts_.restartRequested(); }
    // The live processor behind a model insert, if one exists (controller thread).
    std::shared_ptr<InsertProcessor> insertInstance(core::Id insertId) const
    {
        return inserts_.find(insertId);
    }
    // Momentary and short-term loudness of the master (BS.1770).
    LoudnessMeter::Reading loudness() const noexcept { return loudness_.reading(); }
    core::SamplePosition position() const noexcept { return position_.load(std::memory_order_relaxed); }
    bool isPlaying() const noexcept { return playing_.load(std::memory_order_relaxed); }
    bool isRecording() const noexcept { return recording_.load(std::memory_order_relaxed); }
    bool isCountingIn() const noexcept { return countingIn_.load(std::memory_order_relaxed); }
    std::uint64_t droppedCommands() const noexcept
    {
        return droppedCommands_.load(std::memory_order_relaxed);
    }

    // ----- real-time thread -------------------------------------------------------
    // Overwrites the outputs with the mix and captures armed inputs. The caller has
    // already entered the real-time section and set FTZ/DAZ.
    void process(const float* const* inputs,
                 int numInputs,
                 float* const* outputs,
                 int numOutputs,
                 int numSamples) noexcept;
    void process(float* const* outputs, int numOutputs, int numSamples) noexcept
    {
        process(nullptr, 0, outputs, numOutputs, numSamples);
    }

private:
    static constexpr std::size_t commandQueueCapacity = 256;
    static constexpr int maxCommandsPerBlock = 64;

    void send(const TransportCommand& command);
    void applyCommand(const TransportCommand& command) noexcept;
    void endRecordingOnRt() noexcept;

    MediaCache media_;
    InsertCache inserts_;
    DelayCache delays_;
    RuntimeCache runtimes_;
    struct QueuedMidi
    {
        MidiEvent event;
        int source = -1;
        core::Id target = core::invalidId; // a preview's track; invalidId for device input
        std::int64_t arrival = 0;          // steady-clock nanoseconds; 0: keep the event's offset
    };
    static constexpr size_t midiQueueCapacity = 4096;
    rt::SpscQueue<QueuedMidi> midiIn_{midiQueueCapacity}; // producer side under midiMutex_
    MidiEventList liveMidi_{midiQueueCapacity};
    std::vector<int> liveMidiSources_ = std::vector<int>(midiQueueCapacity);
    std::vector<core::Id> liveMidiTargets_ = std::vector<core::Id>(midiQueueCapacity);
    size_t liveMidiCount_ = 0;
    std::vector<std::string> midiSourceNames_;    // index = source
    std::map<std::string, InputPair> inputBuses_; // controller thread
    int outputPairs_ = 1;                         // controller thread
    std::mutex midiMutex_;
    rt::SpscQueue<MidiOutEvent> midiOut_{4096};
    std::int64_t sampleClock_ = 0; // real-time: samples rendered since start
    // The clock sync, published by the real-time thread with a sequence count
    // (odd while it writes) so a reader never sees half of a pair.
    std::atomic<std::uint64_t> syncSequence_{0};
    std::atomic<std::int64_t> syncClock_{0}, syncNanos_{0};
    NanoClock clock_ = &AudioEngine::steadyNanos;
    std::int64_t lastBlockNanos_ = 0; // when the previous block started (audio thread)
    std::atomic<int> masterMeterSlot_{-1};
    bool flushDelays_ = false; // real-time thread only: a locate happened
    std::atomic<int> outputLatency_{0};
    rt::SnapshotPublisher<Snapshot> publisher_;
    PreviewPlayer preview_;
    rt::SpscQueue<TransportCommand> commands_{commandQueueCapacity};
    WorkerPool pool_;
    Renderer renderer_;
    RecordWriter writer_;
    rt::MeterBlock meters_;
    LoudnessMeter loudness_;

    TransportState transport_;               // real-time thread only
    RecordSession* recordSession_ = nullptr; // real-time thread only
    std::atomic<RecordSession*> pendingRecord_{nullptr};
    std::unique_ptr<RecordSession> activeRecord_; // controller-side ownership

    std::atomic<core::SamplePosition> position_{0};
    std::atomic<bool> playing_{false};
    std::atomic<bool> recording_{false};
    std::atomic<bool> countingIn_{false};
    std::atomic<bool> running_{false};
    std::atomic<double> deviceSampleRate_{0.0};
    // The block prepare() was given: what a compile sizes the work buffers for
    // (the most a snapshot can hold until the engine is prepared).
    std::atomic<int> preparedBlock_{Snapshot::maxBlockSamples};
    std::atomic<std::uint64_t> droppedCommands_{0};
};

} // namespace northlight::engine
