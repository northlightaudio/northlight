#pragma once

#include "core/Model.h"
#include "engine/WavFile.h"
#include "engine/rt/SpscQueue.h"

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
//
// Loop recording: every time the transport wraps while capturing, the audio thread
// marks a take boundary (the cumulative captured count plus the new timeline
// start); the writer closes the current file at that sample and opens the next
// take's file, so each pass through the loop becomes its own clip.

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

// Where a recorded file belongs on the timeline after latency compensation: the
// capture started `latency` samples after the audio actually happened, so the clip
// moves earlier by that much; anything that would land before zero is trimmed off
// the front of the source instead.
struct RecordingPlacement
{
    core::SamplePosition start = 0;
    core::SamplePosition sourceOffset = 0;
    core::SamplePosition length = 0;
};
RecordingPlacement placeRecording(core::SamplePosition captureStart,
                                  std::int64_t samplesWritten,
                                  core::SamplePosition latency) noexcept;

class RecordSession
{
public:
    struct Input
    {
        core::Id trackId = core::invalidId;
        int inputChannel = 0; // index into the active device inputs
        std::string path;     // file the writer records to (first take)
        std::unique_ptr<RecordRing> ring;
        std::atomic<std::uint64_t> dropped{0};
        std::int64_t captured = 0; // audio thread only
    };

    struct TakeMark
    {
        std::int64_t capturedBefore = 0; // samples captured (per input) before the new take
        core::SamplePosition start = 0;  // timeline position the new take begins at
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
    // A loop wrap: the next captured sample starts a new take at `start`.
    void markTake(core::SamplePosition start) noexcept;
    void finish() noexcept { finished_.store(true, std::memory_order_release); }
    std::int64_t capturedSamples() const noexcept { return captured_; }

    // Writer thread.
    bool popTakeMark(TakeMark& mark) noexcept { return takeMarks_.pop(mark); }

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
    std::int64_t captured_ = 0; // audio thread only
    rt::SpscQueue<TakeMark> takeMarks_{256};
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
        core::SamplePosition startPosition = 0; // timeline position of the first sample
        int takeIndex = 0;                      // 0 for the first pass
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
    struct InputState
    {
        std::unique_ptr<WavWriter> writer;
        std::int64_t written = 0; // across all takes
        int takeIndex = 0;
        core::SamplePosition takeStart = 0; // timeline start of the open take
        size_t nextMark = 0;                // index into marks_
    };

    void run();
    void drain(bool final);
    bool writeTo(size_t inputIndex, const float* samples, int count);
    void closeTake(size_t inputIndex, std::vector<Result>& into);
    static std::string takePath(const std::string& firstPath, int takeIndex);

    RecordSession* session_ = nullptr;
    std::vector<InputState> inputs_;
    std::vector<RecordSession::TakeMark> marks_;
    std::vector<Result> results_;
    std::vector<float> chunk_;
    std::atomic<bool> writing_{false};
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
};

} // namespace aural::engine
