#pragma once

#include "core/Model.h"
#include "engine/WavFile.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Recording path (docs/PLAN.md 3.4):
//
//   audio thread  --RecordRing per input-->  RecordWriter thread  --> temp WAV
//
// A RecordSession is created on the controller thread with one entry per armed
// track (its input channel and the file to write). The real-time thread captures
// every block of each input into that entry's ring; the writer thread drains the
// rings into float WAV files, rewriting the headers every second so a crash loses
// at most that much. When the session stops, the writer finishes the files and the
// controller places the clips (latency-compensated) on the tracks.

namespace aural::engine
{

// Single-producer (audio thread) single-consumer (writer thread) ring of samples.
class RecordRing
{
public:
    explicit RecordRing(std::size_t capacity);

    // Audio thread. Returns false (dropping the block) when there is no room.
    bool push(const float* samples, int numSamples) noexcept;

    // Writer thread. Copies up to maxSamples into dest, returns the count.
    int pop(float* dest, int maxSamples) noexcept;

    std::size_t capacity() const noexcept { return data_.size(); }
    std::size_t available() const noexcept;

private:
    std::vector<float> data_;
    std::atomic<std::uint64_t> written_{0};
    std::atomic<std::uint64_t> read_{0};
};

class RecordSession
{
public:
    struct Input
    {
        core::Id trackId = core::invalidId;
        int inputChannel = 0; // index into the active device inputs
        std::string path;     // file the writer records to
        std::unique_ptr<RecordRing> ring;
        std::atomic<std::uint64_t> dropped{0};
        std::int64_t captured = 0; // audio thread only
    };

    // ringSeconds of buffering per input; the writer must drain faster than that.
    RecordSession(double sampleRate, double ringSeconds = 4.0);

    // Controller, before the session is handed to the engine.
    void addInput(core::Id trackId, int inputChannel, std::string path);

    double sampleRate() const noexcept { return sampleRate_; }
    const std::vector<std::unique_ptr<Input>>& inputs() const noexcept { return inputs_; }

    // ----- audio thread -----
    // Called once per block while recording. `position` is the timeline position of
    // the block's first sample; the first call fixes startPosition().
    void capture(const float* const* inputs,
                 int numInputs,
                 int numSamples,
                 core::SamplePosition position) noexcept;
    void finish() noexcept { finished_.store(true, std::memory_order_release); }

    // ----- any thread -----
    bool isFinished() const noexcept { return finished_.load(std::memory_order_acquire); }
    core::SamplePosition startPosition() const noexcept
    {
        return startPosition_.load(std::memory_order_acquire);
    }
    std::uint64_t totalDropped() const noexcept;

private:
    double sampleRate_;
    std::size_t ringCapacity_;
    std::vector<std::unique_ptr<Input>> inputs_;
    std::atomic<core::SamplePosition> startPosition_{-1};
    std::atomic<bool> finished_{false};
};

// One writer thread per engine. begin() takes a session the engine is capturing
// into; the thread drains it until the session is finished and the rings are empty,
// then closes the files and reports.
class RecordWriter
{
public:
    struct Result
    {
        core::Id trackId = core::invalidId;
        std::string path;
        std::int64_t samplesWritten = 0;
        std::uint64_t dropped = 0;
        bool ok = true;
    };

    RecordWriter();
    ~RecordWriter();
    RecordWriter(const RecordWriter&) = delete;
    RecordWriter& operator=(const RecordWriter&) = delete;

    // Controller. Opens the files; returns false (and leaves the session untouched)
    // if any could not be opened. The session must outlive the writer's work.
    bool begin(RecordSession& session);
    bool isWriting() const noexcept { return writing_.load(std::memory_order_acquire); }
    // Valid once isWriting() is false again after a begin().
    std::vector<Result> takeResults();

    static constexpr int headerUpdateMilliseconds = 1000;
    static constexpr int drainIntervalMilliseconds = 10;

private:
    void run();
    void drain(bool final);

    RecordSession* session_ = nullptr;
    std::vector<std::unique_ptr<WavWriter>> writers_;
    std::vector<Result> results_;
    std::vector<float> chunk_;
    std::atomic<bool> writing_{false};
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
};

} // namespace aural::engine
