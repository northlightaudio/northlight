#pragma once

#include "core/Model.h"
#include "engine/Meters.h"
#include "engine/Recording.h"
#include "engine/Renderer.h"
#include "engine/Snapshot.h"
#include "engine/Transport.h"
#include "engine/WorkerPool.h"
#include "engine/rt/SnapshotPublisher.h"
#include "engine/rt/SpscQueue.h"

#include <atomic>
#include <map>
#include <memory>
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

namespace aural::engine
{

class AudioEngine
{
public:
    // workerThreads < 0 picks WorkerPool::recommendedWorkerCount().
    explicit AudioEngine(MediaLoader& loader, int workerThreads = -1);
    ~AudioEngine();

    int workerThreads() const noexcept { return pool_.numWorkers(); }

    // ----- controller thread ------------------------------------------------------
    // Compiles and publishes the project. Media errors are available afterwards.
    void setProject(const core::Project& project);
    const std::map<std::string, std::string>& mediaErrors() const noexcept { return media_.errors(); }

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

    // Frees retired snapshots and unreferenced media. Call periodically (a UI timer).
    void collectGarbage();

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
    rt::SnapshotPublisher<Snapshot> publisher_;
    rt::SpscQueue<TransportCommand> commands_{commandQueueCapacity};
    WorkerPool pool_;
    Renderer renderer_;
    RecordWriter writer_;
    rt::MeterBlock meters_;

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
    std::atomic<std::uint64_t> droppedCommands_{0};
};

} // namespace aural::engine
