#include "engine/Recording.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace aural::engine
{

// ----- ring ---------------------------------------------------------------------------------

RecordRing::RecordRing(std::size_t capacity) : data_(std::max<std::size_t>(capacity, 1024), 0.0f) {}

std::size_t RecordRing::available() const noexcept
{
    return static_cast<std::size_t>(written_.load(std::memory_order_acquire) -
                                    read_.load(std::memory_order_acquire));
}

bool RecordRing::push(const float* samples, int numSamples) noexcept
{
    if (numSamples <= 0)
        return true;
    const auto written = written_.load(std::memory_order_relaxed);
    const auto read = read_.load(std::memory_order_acquire);
    const auto count = static_cast<std::size_t>(numSamples);
    if (data_.size() - static_cast<std::size_t>(written - read) < count)
        return false;

    const auto index = static_cast<std::size_t>(written % data_.size());
    const auto first = std::min(count, data_.size() - index);
    if (samples != nullptr)
    {
        std::copy_n(samples, first, data_.data() + index);
        if (first < count)
            std::copy_n(samples + first, count - first, data_.data());
    }
    else
    {
        std::fill_n(data_.data() + index, first, 0.0f);
        if (first < count)
            std::fill_n(data_.data(), count - first, 0.0f);
    }
    written_.store(written + count, std::memory_order_release);
    return true;
}

int RecordRing::pop(float* dest, int maxSamples) noexcept
{
    const auto read = read_.load(std::memory_order_relaxed);
    const auto written = written_.load(std::memory_order_acquire);
    const auto count =
        std::min(static_cast<std::size_t>(written - read), static_cast<std::size_t>(maxSamples));
    if (count == 0)
        return 0;
    const auto index = static_cast<std::size_t>(read % data_.size());
    const auto first = std::min(count, data_.size() - index);
    std::copy_n(data_.data() + index, first, dest);
    if (first < count)
        std::copy_n(data_.data(), count - first, dest + first);
    read_.store(read + count, std::memory_order_release);
    return static_cast<int>(count);
}

// ----- placement -------------------------------------------------------------------------------

RecordingPlacement placeRecording(core::SamplePosition captureStart,
                                  std::int64_t samplesWritten,
                                  core::SamplePosition latency) noexcept
{
    RecordingPlacement p;
    p.start = captureStart - latency;
    p.length = samplesWritten;
    if (p.start < 0)
    {
        p.sourceOffset = -p.start;
        p.length = std::max<core::SamplePosition>(0, p.length - p.sourceOffset);
        p.start = 0;
    }
    return p;
}

// ----- session ---------------------------------------------------------------------------------

RecordSession::RecordSession(double sampleRate, double ringSeconds)
    : sampleRate_(sampleRate),
      ringCapacity_(static_cast<std::size_t>(std::max(1.0, sampleRate * ringSeconds)))
{
}

void RecordSession::addInput(core::Id trackId, int inputChannel, std::string path)
{
    AURAL_ASSERT_NOT_REALTIME("RecordSession::addInput");
    auto input = std::make_unique<Input>();
    input->trackId = trackId;
    input->inputChannel = inputChannel;
    input->path = std::move(path);
    input->ring = std::make_unique<RecordRing>(ringCapacity_);
    input->gaps = std::make_unique<rt::SpscQueue<Gap>>(1024);
    inputs_.push_back(std::move(input));
}

void RecordSession::capture(const float* const* inputs,
                            int numInputs,
                            int numSamples,
                            core::SamplePosition position) noexcept
{
    if (startPosition_.load(std::memory_order_relaxed) < 0)
        startPosition_.store(position, std::memory_order_release);
    for (auto& input : inputs_)
    {
        const float* samples = input->inputChannel >= 0 && input->inputChannel < numInputs
                                   ? inputs[input->inputChannel]
                                   : nullptr;
        if (!input->ring->push(samples, numSamples))
        {
            input->dropped.fetch_add(static_cast<std::uint64_t>(numSamples), std::memory_order_relaxed);
            input->gaps->push(Gap{input->captured, numSamples}); // a full gap queue: the file runs short
        }
        input->captured += numSamples;
    }
    captured_ += numSamples;
    endPosition_.store(position + numSamples, std::memory_order_release);
}

void RecordSession::addMidiTrack(core::Id trackId)
{
    AURAL_ASSERT_NOT_REALTIME("RecordSession::addMidiTrack");
    midiTracks_.push_back(trackId);
}

void RecordSession::markTake(core::SamplePosition start) noexcept
{
    takeMarks_.push(TakeMark{captured_, start}); // a full queue drops the mark: the take just continues
    midiMarks_.push(TakeSpan{start, endPosition_.load(std::memory_order_relaxed)});
    ++take_;
}

void RecordSession::captureMidi(core::Id trackId,
                                const MidiEvent& event,
                                core::SamplePosition position) noexcept
{
    midi_.push(CapturedMidi{trackId, event, position, take_});
}

std::vector<RecordSession::TakeSpan> RecordSession::takeSpans()
{
    AURAL_ASSERT_NOT_REALTIME("RecordSession::takeSpans");
    std::vector<TakeSpan> spans;
    spans.push_back({std::max<core::SamplePosition>(0, startPosition()), 0});
    TakeSpan mark;
    while (midiMarks_.pop(mark))
    {
        spans.back().end = mark.end;
        spans.push_back({mark.start, 0});
    }
    spans.back().end = std::max(spans.back().start, endPosition());
    return spans;
}

std::vector<MidiTake> buildMidiTakes(const std::vector<RecordSession::CapturedMidi>& events,
                                     const std::vector<RecordSession::TakeSpan>& spans,
                                     core::SamplePosition latency)
{
    AURAL_ASSERT_NOT_REALTIME("buildMidiTakes");
    struct Open
    {
        core::SamplePosition start;
        int velocity;
    };
    struct Building
    {
        MidiTake take;
        int index = 0;
        std::optional<Open> held[128];
    };
    std::vector<Building> building;
    const auto closeNote = [](Building& b, int note, core::SamplePosition at)
    {
        auto& o = b.held[note];
        if (!o.has_value())
            return;
        b.take.clip.notes.push_back(
            {core::invalidId, note, o->velocity, o->start, std::max<core::SamplePosition>(1, at - o->start)});
        o.reset();
    };
    for (const auto& e : events)
    {
        if (e.take < 0 || static_cast<size_t>(e.take) >= spans.size())
            continue;
        Building* b = nullptr;
        for (auto& candidate : building)
            if (candidate.take.trackId == e.trackId && candidate.index == e.take)
                b = &candidate;
        if (b == nullptr)
        {
            building.emplace_back();
            b = &building.back();
            b->take.trackId = e.trackId;
            b->index = e.take;
        }
        const int note = e.event.note & 127;
        switch (e.event.type)
        {
            case MidiEvent::Type::noteOn:
                closeNote(*b, note, e.position); // a retrigger ends the first
                if (e.event.velocity > 0)
                    b->held[note] = Open{e.position, e.event.velocity};
                break;
            case MidiEvent::Type::noteOff:
                closeNote(*b, note, e.position);
                break;
            case MidiEvent::Type::allNotesOff:
                for (int n = 0; n < 128; ++n)
                    closeNote(*b, n, e.position);
                break;
            case MidiEvent::Type::controller:
                break;
        }
    }
    std::vector<MidiTake> takes;
    for (auto& b : building)
    {
        const auto& span = spans[static_cast<size_t>(b.index)];
        for (int n = 0; n < 128; ++n)
            if (b.held[n].has_value())
                closeNote(b, n, std::max(span.end, b.held[n]->start + 1));
        if (b.take.clip.notes.empty())
            continue;
        // Timeline to clip-relative, the take moved earlier by the latency.
        auto& clip = b.take.clip;
        clip.start = std::max<core::SamplePosition>(0, span.start - latency);
        clip.length = std::max<core::SamplePosition>(1, span.end - latency - clip.start);
        for (auto& n : clip.notes)
        {
            n.start = std::max<core::SamplePosition>(0, n.start - latency - clip.start);
            clip.length = std::max(clip.length, n.start + n.length);
        }
        clip.sortNotes();
        takes.push_back(std::move(b.take));
    }
    return takes;
}

std::uint64_t RecordSession::totalDropped() const noexcept
{
    std::uint64_t total = 0;
    for (const auto& input : inputs_)
        total += input->dropped.load(std::memory_order_relaxed);
    return total;
}

// ----- writer ------------------------------------------------------------------------------------

RecordWriter::RecordWriter() : thread_([this] { run(); }) {}

RecordWriter::~RecordWriter()
{
    stop_.store(true, std::memory_order_release);
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

bool RecordWriter::begin(RecordSession& session)
{
    AURAL_ASSERT_NOT_REALTIME("RecordWriter::begin");
    if (isWriting())
        return false;

    std::vector<InputState> states;
    for (const auto& input : session.inputs())
    {
        InputState state;
        state.writer = std::make_unique<WavWriter>();
        if (!state.writer->open(input->path, 1, session.sampleRate()))
            return false;
        states.push_back(std::move(state));
    }

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        session_ = &session;
        inputs_ = std::move(states);
        marks_.clear();
        results_.clear();
        chunk_.resize(1 << 16);
        zeros_.assign(1 << 12, 0.0f);
        writing_.store(true, std::memory_order_release);
    }
    wake_.notify_all();
    return true;
}

std::vector<RecordWriter::Result> RecordWriter::takeResults()
{
    const std::lock_guard<std::mutex> lock(mutex_);
    auto out = std::move(results_);
    results_.clear();
    return out;
}

std::string RecordWriter::takePath(const std::string& firstPath, int takeIndex)
{
    const std::string suffix = ".partial.wav";
    const std::string stem = firstPath.size() > suffix.size() && firstPath.ends_with(suffix)
                                 ? firstPath.substr(0, firstPath.size() - suffix.size())
                                 : firstPath;
    return stem + "-take" + std::to_string(takeIndex + 1) + suffix;
}

void RecordWriter::closeTake(size_t inputIndex, std::vector<Result>& into)
{
    auto& state = inputs_[inputIndex];
    const auto& input = *session_->inputs()[inputIndex];
    Result r;
    r.trackId = input.trackId;
    r.path = state.writer != nullptr ? state.writer->path() : std::string{};
    r.samplesWritten = state.writer != nullptr ? state.writer->samplesWritten() : 0;
    r.dropped = input.dropped.load(std::memory_order_relaxed);
    r.samplesLost = state.lost;
    r.startPosition = state.takeIndex == 0 ? session_->startPosition() : state.takeStart;
    r.takeIndex = state.takeIndex;
    const bool closed = state.writer != nullptr && state.writer->close();
    r.ok = closed && !state.failed;
    into.push_back(std::move(r));
}

void RecordWriter::fail(size_t inputIndex, int lostNow)
{
    auto& state = inputs_[inputIndex];
    state.failed = true;
    state.lost += lostNow;
}

// Applies every take mark and gap that falls at the current written position.
// Returns false after a failure.
bool RecordWriter::applyEventsAt(size_t inputIndex)
{
    auto& state = inputs_[inputIndex];
    for (;;)
    {
        const bool gapHere =
            state.nextGap < state.gaps.size() && state.gaps[state.nextGap].capturedBefore <= state.written;
        // On the final flush a mark with no samples after it (a loop wrap at the very
        // end) does not open an empty take; it still does when a gap's zeros follow.
        const bool markHere = state.nextMark < marks_.size() &&
                              marks_[state.nextMark].capturedBefore <= state.written &&
                              (!flushing_ || state.nextGap < state.gaps.size());
        if (markHere)
        {
            // The take boundary is here: finish this file and start the next. A mark
            // and a gap at the same sample: the new take starts, then the silence.
            const auto mark = marks_[state.nextMark++];
            closeTake(inputIndex, results_);
            state.takeIndex += 1;
            state.takeStart = mark.start;
            state.writer = std::make_unique<WavWriter>();
            if (!state.writer->open(takePath(session_->inputs()[inputIndex]->path, state.takeIndex),
                                    1,
                                    session_->sampleRate()))
            {
                fail(inputIndex, 0);
                return false;
            }
            continue;
        }
        if (gapHere)
        {
            const auto gap = state.gaps[state.nextGap++];
            // Zeros stand in for the dropped block; they pass through writeSamples so a
            // mark inside the span (none expected) would still split correctly.
            int remaining = gap.count;
            while (remaining > 0)
            {
                const int n = std::min(remaining, static_cast<int>(zeros_.size()));
                if (!writeSamples(inputIndex, zeros_.data(), n))
                    return false;
                remaining -= n;
            }
            continue;
        }
        return true;
    }
}

// Writes samples up to the next event boundary, repeatedly.
bool RecordWriter::writeSamples(size_t inputIndex, const float* samples, int count)
{
    auto& state = inputs_[inputIndex];
    while (count > 0)
    {
        std::int64_t boundary = state.written + count;
        if (state.nextMark < marks_.size())
            boundary = std::min(boundary, marks_[state.nextMark].capturedBefore);
        if (state.nextGap < state.gaps.size())
            boundary = std::min(boundary, state.gaps[state.nextGap].capturedBefore);
        const auto toWrite = static_cast<int>(std::max<std::int64_t>(0, boundary - state.written));
        if (toWrite == 0)
        {
            if (!applyEventsAt(inputIndex))
            {
                state.lost += count;
                return false;
            }
            continue;
        }
        if (!state.writer->writeInterleaved(samples, toWrite))
        {
            fail(inputIndex, count);
            return false;
        }
        state.written += toWrite;
        samples += toWrite;
        count -= toWrite;
    }
    return true;
}

// Writes `count` samples for one input (count may be 0 to flush pending events).
bool RecordWriter::writeTo(size_t inputIndex, const float* samples, int count)
{
    auto& state = inputs_[inputIndex];
    if (state.failed)
    {
        state.lost += count;
        return false;
    }
    if (!applyEventsAt(inputIndex))
    {
        state.lost += count;
        return false;
    }
    return writeSamples(inputIndex, samples, count);
}

void RecordWriter::drain(bool final)
{
    const auto& inputs = session_->inputs();
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        auto& ring = *inputs[i]->ring;
        for (;;)
        {
            // Samples first, then the events that may refer to them: the audio thread
            // pushes a mark or gap before the samples that follow it, so once those
            // samples are visible the event is too. The other order could write a
            // take's first block into the previous file.
            const int got = ring.pop(chunk_.data(), static_cast<int>(chunk_.size()));
            RecordSession::TakeMark mark;
            while (session_->popTakeMark(mark))
                marks_.push_back(mark);
            RecordSession::Gap gap;
            while (session_->popGap(i, gap))
                inputs_[i].gaps.push_back(gap);

            if (got == 0)
            {
                if (final)
                {
                    flushing_ = true;
                    writeTo(i, chunk_.data(), 0); // trailing events (a drop at the very end)
                    flushing_ = false;
                }
                break;
            }
            writeTo(i, chunk_.data(), got);
            if (!final && got < static_cast<int>(chunk_.size()))
                break;
        }
    }
}

void RecordWriter::run()
{
    while (!stop_.load(std::memory_order_acquire))
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_.load(std::memory_order_acquire) || session_ != nullptr; });
            if (stop_.load(std::memory_order_acquire))
                return;
        }

        auto lastHeader = std::chrono::steady_clock::now();
        while (!stop_.load(std::memory_order_acquire))
        {
            drain(false);
            const auto now = std::chrono::steady_clock::now();
            if (now - lastHeader >= std::chrono::milliseconds(headerUpdateMilliseconds))
            {
                for (size_t i = 0; i < inputs_.size(); ++i)
                    if (!inputs_[i].failed && !inputs_[i].writer->updateHeader())
                        fail(i, 0);
                lastHeader = now;
            }
            if (session_->isFinished())
            {
                drain(true);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(drainIntervalMilliseconds));
        }

        for (size_t i = 0; i < inputs_.size(); ++i)
            closeTake(i, results_);
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            inputs_.clear();
            session_ = nullptr;
            writing_.store(false, std::memory_order_release);
        }
    }
}

} // namespace aural::engine
