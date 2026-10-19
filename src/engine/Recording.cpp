#include "engine/Recording.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <chrono>
#include <cmath>

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
            input->dropped.fetch_add(static_cast<std::uint64_t>(numSamples), std::memory_order_relaxed);
        input->captured += numSamples;
    }
    captured_ += numSamples;
}

void RecordSession::markTake(core::SamplePosition start) noexcept
{
    takeMarks_.push(TakeMark{captured_, start}); // a full queue drops the mark: the take just continues
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
    r.path = state.writer->path();
    r.samplesWritten = state.writer->samplesWritten();
    r.dropped = input.dropped.load(std::memory_order_relaxed);
    r.startPosition = state.takeIndex == 0 ? session_->startPosition() : state.takeStart;
    r.takeIndex = state.takeIndex;
    r.ok = state.writer->close();
    into.push_back(std::move(r));
}

// Writes `count` samples for one input, splitting files at take marks.
bool RecordWriter::writeTo(size_t inputIndex, const float* samples, int count)
{
    auto& state = inputs_[inputIndex];
    while (count > 0)
    {
        if (state.nextMark < marks_.size() && marks_[state.nextMark].capturedBefore <= state.written)
        {
            // The take boundary is here: finish this file and start the next.
            const auto mark = marks_[state.nextMark++];
            closeTake(inputIndex, results_);
            state.takeIndex += 1;
            state.takeStart = mark.start;
            state.writer = std::make_unique<WavWriter>();
            if (!state.writer->open(takePath(session_->inputs()[inputIndex]->path, state.takeIndex),
                                    1,
                                    session_->sampleRate()))
                return false;
            continue;
        }
        int toWrite = count;
        if (state.nextMark < marks_.size())
            toWrite = static_cast<int>(
                std::min<std::int64_t>(toWrite, marks_[state.nextMark].capturedBefore - state.written));
        if (!state.writer->writeInterleaved(samples, toWrite))
            return false;
        state.written += toWrite;
        samples += toWrite;
        count -= toWrite;
    }
    return true;
}

void RecordWriter::drain(bool final)
{
    RecordSession::TakeMark mark;
    while (session_->popTakeMark(mark))
        marks_.push_back(mark);

    const auto& inputs = session_->inputs();
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        auto& ring = *inputs[i]->ring;
        for (;;)
        {
            const int got = ring.pop(chunk_.data(), static_cast<int>(chunk_.size()));
            if (got == 0)
                break;
            if (!writeTo(i, chunk_.data(), got))
                break;
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
                for (auto& state : inputs_)
                    state.writer->updateHeader();
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
