#pragma once

#include "core/Model.h"
#include "engine/Midi.h"
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
//
// MIDI: an armed MIDI track's live events are stamped with their timeline
// position and the take they fall in and queued for the controller, which turns
// them into clips when the session is collected (buildMidiTakes).
//
// Live view: as the writer puts samples in a file it also keeps min/max peaks of
// them (LiveTake), and the controller drains the MIDI events and take marks as
// they come, so the arrange view can draw each take growing while it records
// (04-arrange.md "Recording clip") instead of waiting for the files to close.

namespace northlight::engine
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
    // A block the ring could not take: the writer inserts `count` zeros at
    // `capturedBefore` so file sample numbers stay equal to captured sample numbers
    // and later take boundaries land where they should.
    struct Gap
    {
        std::int64_t capturedBefore = 0;
        int count = 0;
    };

    struct Input
    {
        core::Id trackId = core::invalidId;
        int inputChannel = 0; // index into the active device inputs
        std::string path;     // file the writer records to (first take)
        std::unique_ptr<RecordRing> ring;
        std::unique_ptr<rt::SpscQueue<Gap>> gaps;
        std::atomic<std::uint64_t> dropped{0};
        std::int64_t captured = 0; // audio thread only
    };

    struct TakeMark
    {
        std::int64_t capturedBefore = 0; // samples captured (per input) before the new take
        core::SamplePosition start = 0;  // timeline position the new take begins at
    };
    struct CapturedMidi
    {
        core::Id trackId = core::invalidId;
        MidiEvent event;
        core::SamplePosition position = 0; // timeline position of the event
        int take = 0;                      // 0 for the first pass, +1 per loop wrap
    };
    struct TakeSpan
    {
        core::SamplePosition start = 0, end = 0; // timeline span of one take
    };

    // ringSeconds of buffering per input; the writer must drain faster than that.
    RecordSession(double sampleRate, double ringSeconds = 4.0);

    // Controller, before the session is handed to the engine.
    void addInput(core::Id trackId, int inputChannel, std::string path);
    // A MIDI track whose live input is captured (its source filter is the track's).
    void addMidiTrack(core::Id trackId);

    double sampleRate() const noexcept { return sampleRate_; }
    const std::vector<std::unique_ptr<Input>>& inputs() const noexcept { return inputs_; }
    const std::vector<core::Id>& midiTracks() const noexcept { return midiTracks_; }
    bool recordsMidi(core::Id trackId) const noexcept
    {
        for (const auto id : midiTracks_)
            if (id == trackId)
                return true;
        return false;
    }

    // ----- audio thread -----
    // Called once per block while recording. `position` is the timeline position of
    // the block's first sample; the first call fixes startPosition().
    void capture(const float* const* inputs,
                 int numInputs,
                 int numSamples,
                 core::SamplePosition position) noexcept;
    // A loop wrap: the next captured sample starts a new take at `start`.
    void markTake(core::SamplePosition start) noexcept;
    // A live MIDI event on a recorded track, at its timeline position (after
    // capture() for the block, which fixes the take). A full queue drops it.
    void captureMidi(core::Id trackId, const MidiEvent& event, core::SamplePosition position) noexcept;
    // Controller, while recording or once finished: every captured event so far
    // in time order, and the takes' spans so far (index = take; the current take
    // ends at endPosition()). Both drain the audio thread's queues into
    // controller-side lists, so they can be called as often as the UI likes.
    const std::vector<CapturedMidi>& capturedMidi();
    std::vector<TakeSpan> takeSpans();
    // Timeline position just after the last captured block.
    core::SamplePosition endPosition() const noexcept { return endPosition_.load(std::memory_order_acquire); }
    void finish() noexcept { finished_.store(true, std::memory_order_release); }
    std::int64_t capturedSamples() const noexcept { return captured_; }

    // Writer thread.
    bool popTakeMark(TakeMark& mark) noexcept { return takeMarks_.pop(mark); }
    bool popGap(size_t input, Gap& gap) noexcept { return inputs_[input]->gaps->pop(gap); }

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
    std::vector<core::Id> midiTracks_;
    rt::SpscQueue<CapturedMidi> midi_{16384};
    rt::SpscQueue<TakeSpan> midiMarks_{256}; // a wrap: {new start, previous end}
    std::vector<CapturedMidi> midiEvents_;   // controller only: drained from midi_
    std::vector<TakeSpan> wraps_;            // controller only: drained from midiMarks_
    int take_ = 0;                           // audio thread only
    std::atomic<core::SamplePosition> endPosition_{-1};
    std::atomic<core::SamplePosition> startPosition_{-1};
    std::atomic<bool> finished_{false};
    std::int64_t captured_ = 0; // audio thread only
    rt::SpscQueue<TakeMark> takeMarks_{256};
};

// One writer thread per engine. begin() takes a session the engine is capturing
// into; the thread drains it until the session is finished and the rings are empty,
// then closes the files and reports.
// Turns a finished session's MIDI events into clips: one per track and take,
// spanning the take, every note-on paired with its note-off (or ended at the
// take's end), controller events (CC, pitch bend, pressure) kept as they came,
// the whole thing moved `latency` samples earlier like an audio take. Takes with
// neither notes nor controller events make no clip.
struct MidiTake
{
    core::Id trackId = core::invalidId;
    int takeIndex = 0; // 0 for the first pass
    core::MidiClip clip;
};

// What one audio take looks like so far, for drawing it while it records: the
// min/max of every `samplesPerBin` samples the writer has put in its file, at a
// fine level and a coarse one (like PeakData, so a zoomed-out view needs no scan),
// and how far the file has got. Timeline placement is placeRecording()'s.
struct LivePeakLevel
{
    int samplesPerBin = 0;
    std::vector<float> minimum; // one per completed bin
    std::vector<float> maximum;
};
struct LiveTake
{
    core::Id trackId = core::invalidId;
    int takeIndex = 0;
    core::SamplePosition startPosition = 0; // timeline position of the first sample
    std::int64_t samplesWritten = 0;
    std::vector<LivePeakLevel> levels; // fine first
    // Min/max over the written samples [start, end) from the coarsest level whose
    // bins fit the span; anything past samplesWritten reads as silence.
    void lookup(std::int64_t start, std::int64_t end, float& minimum, float& maximum) const noexcept;
};

// The recording in progress as the UI draws it: the audio takes written so far,
// the MIDI takes captured so far (every take with notes, plus the current take of
// each recorded MIDI track even before a note, so an armed track visibly records),
// and the latency the audio takes will be placed early by. ProjectSession fills
// it in place once per UI frame, so a long take costs only its new bins.
struct LiveRecording
{
    std::vector<LiveTake> takes;
    std::vector<MidiTake> midi;
    core::SamplePosition latency = 0;
    bool empty() const noexcept { return takes.empty() && midi.empty(); }
};
std::vector<MidiTake> buildMidiTakes(const std::vector<RecordSession::CapturedMidi>& events,
                                     const std::vector<RecordSession::TakeSpan>& spans,
                                     core::SamplePosition latency);

class RecordWriter
{
public:
    struct Result
    {
        core::Id trackId = core::invalidId;
        std::string path;
        std::int64_t samplesWritten = 0;
        std::uint64_t dropped = 0;              // ring overruns, written as silence
        std::int64_t samplesLost = 0;           // samples that never reached disk (write failure)
        core::SamplePosition startPosition = 0; // timeline position of the first sample
        int takeIndex = 0;                      // 0 for the first pass
        bool ok = true;                         // false after any open, write or header failure
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

    // Any thread. Brings `into` up to date with every take of the current (or
    // last) recording: takes it already holds (same track and take index) get only
    // the bins written since, new ones are appended. begin() starts afresh.
    void updateLiveTakes(std::vector<LiveTake>& into) const;

    static constexpr int headerUpdateMilliseconds = 1000;
    static constexpr int drainIntervalMilliseconds = 10;
    static constexpr int livePeakSamplesPerBin = 256;
    static constexpr int livePeakLevelRatio = 64; // the coarse level: 16384 samples per bin

private:
    struct InputState
    {
        std::unique_ptr<WavWriter> writer;
        std::int64_t written = 0; // across all takes (file sample == captured sample)
        int takeIndex = 0;
        core::SamplePosition takeStart = 0; // timeline start of the open take
        size_t nextMark = 0;                // index into marks_
        std::vector<RecordSession::Gap> gaps;
        size_t nextGap = 0;
        bool failed = false;   // an open, write or header update failed: nothing more is written
        std::int64_t lost = 0; // samples discarded after the failure
        // Live peaks: the take being built in live_ and the unfinished bin per level.
        size_t liveIndex = 0;
        struct Tail
        {
            float minimum = 0.0f, maximum = 0.0f;
            int count = 0;
        };
        Tail tails[2];
    };

    void run();
    void drain(bool final);
    bool writeTo(size_t inputIndex, const float* samples, int count);
    bool writeSamples(size_t inputIndex, const float* samples, int count);
    bool applyEventsAt(size_t inputIndex);
    void fail(size_t inputIndex, int lostNow);
    void closeTake(size_t inputIndex, std::vector<Result>& into);
    void openLiveTake(size_t inputIndex, int takeIndex, core::SamplePosition start);
    void addLivePeaks(size_t inputIndex, const float* samples, int count);
    void flushLivePeaks(size_t inputIndex);
    static std::string takePath(const std::string& firstPath, int takeIndex);

    RecordSession* session_ = nullptr;
    std::vector<InputState> inputs_;
    std::vector<RecordSession::TakeMark> marks_;
    std::vector<Result> results_;
    std::vector<float> chunk_;
    std::vector<float> zeros_;
    bool flushing_ = false; // inside the final flush of trailing events
    std::atomic<bool> writing_{false};
    std::atomic<bool> stop_{false};
    std::vector<LiveTake> live_; // under liveMutex_
    mutable std::mutex liveMutex_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
};

} // namespace northlight::engine
