#pragma once

#include "core/Model.h"
#include "engine/Renderer.h"
#include "engine/Snapshot.h"
#include "engine/Transport.h"
#include "engine/WorkerPool.h"
#include "engine/rt/SnapshotPublisher.h"
#include "engine/rt/SpscQueue.h"

#include <atomic>
#include <map>
#include <string>

// The engine façade: the controller side (message thread) publishes snapshots and
// transport commands; the real-time side renders. Exactly the three transition
// mechanisms from docs/rt-rules.md are used:
//
//   controller -> RT   snapshot swap (rt::SnapshotPublisher)
//   controller -> RT   SPSC command queue (transport)
//   RT -> controller   atomics for position/playing (meters follow in Phase 4)
//
// No JUCE: the app's AudioCallback calls process(); tests drive it offline.

namespace aural::engine
{

class AudioEngine
{
public:
    // workerThreads < 0 picks WorkerPool::recommendedWorkerCount().
    explicit AudioEngine(MediaLoader& loader, int workerThreads = -1);

    int workerThreads() const noexcept { return pool_.numWorkers(); }

    // ----- controller thread ------------------------------------------------------
    // Compiles and publishes the project. Media errors are available afterwards.
    void setProject(const core::Project& project);
    const std::map<std::string, std::string>& mediaErrors() const noexcept { return media_.errors(); }

    void play();
    void stop();
    void locate(core::SamplePosition position);
    void setLoop(core::SamplePosition start, core::SamplePosition end, bool enabled);

    // Frees retired snapshots and unreferenced media. Call periodically (a UI timer).
    void collectGarbage();

    // Device lifecycle. prepare() is called before the device starts delivering
    // blocks (allocates), release() after it has stopped.
    void prepare(double sampleRate, int maxBlockSize);
    void release();
    double deviceSampleRate() const noexcept { return deviceSampleRate_.load(std::memory_order_relaxed); }
    bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }

    // ----- any thread ---------------------------------------------------------------
    core::SamplePosition position() const noexcept { return position_.load(std::memory_order_relaxed); }
    bool isPlaying() const noexcept { return playing_.load(std::memory_order_relaxed); }
    std::uint64_t droppedCommands() const noexcept
    {
        return droppedCommands_.load(std::memory_order_relaxed);
    }

    // ----- real-time thread -------------------------------------------------------
    // Overwrites the outputs with the mix. The caller has already entered the
    // real-time section and set FTZ/DAZ.
    void process(float* const* outputs, int numOutputs, int numSamples) noexcept;

private:
    static constexpr std::size_t commandQueueCapacity = 256;
    static constexpr int maxCommandsPerBlock = 64;

    void send(const TransportCommand& command);

    MediaCache media_;
    rt::SnapshotPublisher<Snapshot> publisher_;
    rt::SpscQueue<TransportCommand> commands_{commandQueueCapacity};
    WorkerPool pool_;
    Renderer renderer_;

    TransportState transport_; // real-time thread only
    std::atomic<core::SamplePosition> position_{0};
    std::atomic<bool> playing_{false};
    std::atomic<bool> running_{false};
    std::atomic<double> deviceSampleRate_{0.0};
    std::atomic<std::uint64_t> droppedCommands_{0};
};

} // namespace aural::engine
