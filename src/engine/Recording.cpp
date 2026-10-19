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

    std::vector<std::unique_ptr<WavWriter>> writers;
    for (const auto& input : session.inputs())
    {
        auto writer = std::make_unique<WavWriter>();
        if (!writer->open(input->path, 1, session.sampleRate()))
            return false;
        writers.push_back(std::move(writer));
    }

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        session_ = &session;
        writers_ = std::move(writers);
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

void RecordWriter::drain(bool final)
{
    const auto& inputs = session_->inputs();
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        auto& ring = *inputs[i]->ring;
        auto& writer = *writers_[i];
        for (;;)
        {
            const int got = ring.pop(chunk_.data(), static_cast<int>(chunk_.size()));
            if (got == 0)
                break;
            if (!writer.writeInterleaved(chunk_.data(), got))
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
                for (auto& w : writers_)
                    w->updateHeader();
                lastHeader = now;
            }
            if (session_->isFinished())
            {
                drain(true);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(drainIntervalMilliseconds));
        }

        std::vector<Result> results;
        const auto& inputs = session_->inputs();
        for (size_t i = 0; i < inputs.size(); ++i)
        {
            Result r;
            r.trackId = inputs[i]->trackId;
            r.path = inputs[i]->path;
            r.samplesWritten = writers_[i]->samplesWritten();
            r.dropped = inputs[i]->dropped.load(std::memory_order_relaxed);
            r.ok = writers_[i]->close();
            results.push_back(std::move(r));
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            results_ = std::move(results);
            writers_.clear();
            session_ = nullptr;
            writing_.store(false, std::memory_order_release);
        }
    }
}

} // namespace aural::engine
