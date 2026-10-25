#include "engine/StreamingAudioSource.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>

namespace northlight::engine
{

StreamingAudioSource::StreamingAudioSource(std::unique_ptr<StreamReader> reader)
    : StreamingAudioSource(std::move(reader), Options{})
{
}

StreamingAudioSource::StreamingAudioSource(std::unique_ptr<StreamReader> reader, Options options)
    : reader_(std::move(reader)), options_(options)
{
    numChannels_ = std::max(1, reader_->numChannels());
    length_ = reader_->lengthSamples();
    sampleRate_ = reader_->sampleRate();

    options_.ringSamples = std::max(options_.ringSamples, 1024);
    options_.chunkSamples = std::clamp(options_.chunkSamples, 256, options_.ringSamples / 4);
    options_.guardSamples = std::clamp(options_.guardSamples, 0, options_.ringSamples / 4);
    options_.readAheadSamples =
        std::clamp(options_.readAheadSamples,
                   options_.chunkSamples,
                   options_.ringSamples - options_.guardSamples - options_.chunkSamples);

    ring_.assign(static_cast<size_t>(numChannels_),
                 std::vector<float>(static_cast<size_t>(options_.ringSamples), 0.0f));
    decoded_.assign(static_cast<size_t>(numChannels_),
                    std::vector<float>(static_cast<size_t>(options_.chunkSamples), 0.0f));
    chunkPointers_.assign(static_cast<size_t>(numChannels_), nullptr);
    frame_.assign(static_cast<size_t>(numChannels_) * pairCapacity, 0.0f);
}

StreamingAudioSource::~StreamingAudioSource() = default;

std::int64_t StreamingAudioSource::clampToFile(std::int64_t position) const noexcept
{
    return std::clamp<std::int64_t>(position, 0, std::max<std::int64_t>(0, length_));
}

// ----- real-time ------------------------------------------------------------------------------

void StreamingAudioSource::read(int channel,
                                std::int64_t sourceStart,
                                float* dest,
                                int numSamples) const noexcept
{
    if (numSamples <= 0)
        return;
    // Every channel of a block from one copy: channel 0 (or a block not copied yet)
    // copies the frame, the others take theirs from it.
    if (numChannels_ > 1 && numChannels_ <= maxFrameChannels && numSamples <= pairCapacity)
    {
        if (channel == 0 || sourceStart != frameStart_ || numSamples != frameCount_)
            copyFrame(sourceStart, numSamples);
        const auto* from = frame_.data() + static_cast<size_t>(channel % numChannels_) * pairCapacity;
        std::copy_n(from, numSamples, dest);
        return;
    }
    copyChannel(channel, sourceStart, dest, numSamples);
}

void StreamingAudioSource::copyFrame(std::int64_t sourceStart, int numSamples) const noexcept
{
    frameStart_ = sourceStart;
    frameCount_ = numSamples;
    // One check of the window for all of them: a tear silences the whole frame.
    copyChannels(sourceStart, numSamples);
}

void StreamingAudioSource::copyChannels(std::int64_t sourceStart, int numSamples) const noexcept
{
    std::array<float*, maxFrameChannels> dests{};
    const int channels = std::min(numChannels_, maxFrameChannels);
    for (int ch = 0; ch < channels; ++ch)
        dests[static_cast<size_t>(ch)] = frame_.data() + static_cast<size_t>(ch) * pairCapacity;
    readInto(dests.data(), channels, sourceStart, numSamples);
}

void StreamingAudioSource::copyChannel(int channel,
                                       std::int64_t sourceStart,
                                       float* dest,
                                       int numSamples) const noexcept
{
    float* dests[1] = {dest};
    readInto(dests, 1, sourceStart, numSamples, channel);
}

void StreamingAudioSource::readInto(float* const* dests,
                                    int channels,
                                    std::int64_t sourceStart,
                                    int numSamples,
                                    int firstChannel) const noexcept
{

    // Tell the disk thread where we are (the last channel read of a block gives the
    // same answer, so publishing on every channel is harmless).
    wanted_.store(sourceStart, std::memory_order_release);

    const auto generation = generation_.load(std::memory_order_acquire);
    const auto start = validStart_.load(std::memory_order_acquire);
    const auto end = validEnd_.load(std::memory_order_acquire);
    const std::int64_t requestEnd = sourceStart + numSamples;

    // Anything outside the file is silence by definition, not an underrun.
    const std::int64_t fileFrom = std::max<std::int64_t>(sourceStart, 0);
    const std::int64_t fileTo = std::min<std::int64_t>(requestEnd, length_);
    const auto silence = [&]
    {
        for (int c = 0; c < channels; ++c)
            std::fill(dests[c], dests[c] + numSamples, 0.0f);
    };
    if (fileTo <= fileFrom)
    {
        silence();
        return;
    }

    if (fileFrom < start || fileTo > end)
    {
        silence();
        underruns_.fetch_add(1, std::memory_order_relaxed);
        // Playback ran off the end of a window that had caught up. A locate lands
        // outside the window instead (or on one still being rebuilt).
        if (firstChannel == 0 && fileFrom >= start && fileFrom <= end &&
            caughtUp_.load(std::memory_order_acquire))
            starvations_.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const auto ringSize = static_cast<std::int64_t>(options_.ringSamples);
    const auto leading = static_cast<int>(fileFrom - sourceStart);
    const auto count = static_cast<int>(fileTo - fileFrom);
    const auto index = static_cast<std::int64_t>(fileFrom % ringSize);
    for (int c = 0; c < channels; ++c)
    {
        const auto& ring = ring_[static_cast<size_t>((firstChannel + c) % numChannels_)];
        float* dest = dests[c];
        std::fill(dest, dest + leading, 0.0f);
        for (int i = 0; i < count; ++i)
        {
            auto slot = index + i;
            if (slot >= ringSize)
                slot -= ringSize;
            // The ring is shared with the disk thread: read through atomic_ref.
            // (A load changes nothing: the const_cast only satisfies atomic_ref.)
            dest[leading + i] = std::atomic_ref<float>(const_cast<float&>(ring[static_cast<size_t>(slot)]))
                                    .load(std::memory_order_relaxed);
        }
        std::fill(dest + leading + count, dest + numSamples, 0.0f);
    }

    // The disk thread may have recycled the slots we just copied: it advanced the
    // window past this range, or a seek rebuilt it while this read was preempted.
    // If so the copy may be torn: hand back silence instead, for every channel
    // copied together. (The fence keeps the copy's loads before the check.)
    std::atomic_thread_fence(std::memory_order_acquire);
    if (generation_.load(std::memory_order_relaxed) != generation ||
        validStart_.load(std::memory_order_relaxed) > fileFrom)
    {
        silence();
        underruns_.fetch_add(1, std::memory_order_relaxed);
    }
}

// ----- controller --------------------------------------------------------------------------------

void StreamingAudioSource::prefetch(std::int64_t position) noexcept
{
    wanted_.store(position, std::memory_order_release);
}

void StreamingAudioSource::setInstanceFactory(OpenReader openReader, OnInstance onInstance)
{
    openReader_ = std::move(openReader);
    onInstance_ = std::move(onInstance);
}

std::shared_ptr<AudioSource> StreamingAudioSource::createInstance() const
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("StreamingAudioSource::createInstance");
    if (!openReader_)
        return nullptr;
    auto reader = openReader_();
    if (reader == nullptr)
        return nullptr;
    auto instance = std::make_shared<StreamingAudioSource>(std::move(reader), options_);
    instance->setInstanceFactory(openReader_, onInstance_);
    if (onInstance_)
        onInstance_(instance);
    return instance;
}

// ----- disk thread ----------------------------------------------------------------------------------

bool StreamingAudioSource::fill(std::int64_t from, int count)
{
    const auto ringSize = static_cast<std::int64_t>(options_.ringSamples);
    const auto index = static_cast<std::int64_t>(from % ringSize);

    // Decoded into scratch, then stored into the ring through atomic_ref: the
    // real-time thread may be reading neighbouring slots.
    for (size_t ch = 0; ch < ring_.size(); ++ch)
        chunkPointers_[ch] = decoded_[ch].data();
    if (!reader_->read(from, count, chunkPointers_.data()))
        return false;
    for (size_t ch = 0; ch < ring_.size(); ++ch)
    {
        auto& ring = ring_[ch];
        const auto& decoded = decoded_[ch];
        for (int i = 0; i < count; ++i)
        {
            auto slot = index + i;
            if (slot >= ringSize)
                slot -= ringSize;
            std::atomic_ref<float>(ring[static_cast<size_t>(slot)])
                .store(decoded[static_cast<size_t>(i)], std::memory_order_relaxed);
        }
    }
    return true;
}

void StreamingAudioSource::bumpGeneration() noexcept
{
    // Seqlock writer: the new generation is visible before any ring write after it.
    generation_.store(generation_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
}

bool StreamingAudioSource::service()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("StreamingAudioSource::service");

    const auto wanted = clampToFile(wanted_.load(std::memory_order_acquire));
    auto start = validStart_.load(std::memory_order_relaxed);
    auto end = validEnd_.load(std::memory_order_relaxed);
    const auto ringSize = static_cast<std::int64_t>(options_.ringSamples);

    if (wanted >= length_ && end >= length_)
        return false; // nothing left to read

    const bool outsideWindow = wanted < start || wanted >= end;
    if (outsideWindow && !(end == start && start == wanted))
    {
        // Seek: empty the window, let any in-flight read finish, then refill from
        // the wanted position.
        caughtUp_.store(false, std::memory_order_release);
        bumpGeneration();
        validStart_.store(wanted, std::memory_order_release);
        validEnd_.store(wanted, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(options_.seekGraceMilliseconds));
        start = end = wanted;
    }

    const auto ahead = end - wanted;
    if (ahead >= options_.readAheadSamples || end >= length_)
    {
        caughtUp_.store(true, std::memory_order_release);
        return false;
    }

    // Append one chunk. The slots it lands in held [end - ringSize, end - ringSize + count),
    // which must be behind the guard.
    const auto count = static_cast<int>(std::min<std::int64_t>(options_.chunkSamples, length_ - end));
    const auto newStart = std::max(start, end + count - ringSize);
    const auto guardLimit = wanted - options_.guardSamples;
    if (newStart > guardLimit && newStart != start)
        return false; // the real-time thread may still read there; wait for it to move on

    validStart_.store(newStart, std::memory_order_release);
    if (!fill(end, count))
    {
        readError_.store(true, std::memory_order_relaxed);
        return false;
    }
    validEnd_.store(end + count, std::memory_order_release);
    return true;
}

} // namespace northlight::engine
