#include "audio/PeakStore.h"

#include "audio/JuceStreamReader.h"
#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace northlight::audio
{

PeakStore::PeakStore() : juce::Thread("northlight peaks")
{
    formats_.registerBasicFormats();
    startThread(juce::Thread::Priority::low);
}

PeakStore::~PeakStore()
{
    signalThreadShouldExit();
    notify();
    stopThread(5000);
    cancelPendingUpdate();
}

std::shared_ptr<const engine::PeakData> PeakStore::peaksFor(const std::string& path, double sampleRate)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("PeakStore::peaksFor");
    const Key key{path, sampleRate};
    const std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = ready_.find(key); it != ready_.end())
        return it->second;
    if (!queued_.contains(key))
    {
        queued_[key] = true;
        queue_.push_back(key);
        notify();
    }
    return nullptr;
}

engine::WaveformView PeakStore::viewFor(
    const std::string& path, double sampleRate, std::int64_t start, std::int64_t end, double samplesPerPixel)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("PeakStore::viewFor");
    auto peaks = peaksFor(path, sampleRate);
    if (peaks == nullptr || peaks->empty() ||
        samplesPerPixel >= static_cast<double>(engine::PeakData::defaultBaseSamplesPerBin))
        return engine::WaveformView(std::move(peaks), 0, {});

    const auto span = engine::blocksFor(start, end, peaks->lengthSamples());
    std::vector<std::shared_ptr<const engine::SampleBlock>> blocks;
    blocks.reserve(static_cast<size_t>(span.last - span.first));
    const auto now = juce::Time::getMillisecondCounter();
    bool queued = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (auto i = span.first; i < span.last; ++i)
        {
            BlockKey key{{path, sampleRate}, i};
            if (const auto it = blocks_.find(key); it != blocks_.end())
            {
                it->second.lastUse = now;
                blocks.push_back(it->second.block);
                continue;
            }
            blocks.push_back(nullptr);
            if (blockQueued_.insert(key).second)
            {
                blockQueue_.push_back(std::move(key));
                queued = true;
            }
        }
    }
    if (queued)
        notify();
    return engine::WaveformView(std::move(peaks), span.first, std::move(blocks));
}

std::shared_ptr<const engine::SampleBlock> PeakStore::readBlock(const BlockKey& key)
{
    if (reader_ == nullptr || readerKey_.path != key.file.path ||
        readerKey_.sampleRate != key.file.sampleRate)
    {
        reader_.reset();
        std::unique_ptr<juce::AudioFormatReader> reader(
            formats_.createReaderFor(juce::File{juce::String(key.file.path)}));
        if (reader == nullptr)
            return nullptr;
        reader_ = std::make_unique<JuceStreamReader>(std::move(reader), key.file.sampleRate);
        readerKey_ = key.file;
    }
    const auto start = key.index * engine::SampleBlock::samplesPerBlock;
    const auto count = std::min(engine::SampleBlock::samplesPerBlock, reader_->lengthSamples() - start);
    if (count <= 0 || reader_->numChannels() <= 0)
        return nullptr;
    auto block = std::make_shared<engine::SampleBlock>();
    block->index = key.index;
    block->channels.assign(static_cast<size_t>(reader_->numChannels()),
                           std::vector<float>(static_cast<size_t>(count)));
    std::vector<float*> dest;
    for (auto& channel : block->channels)
        dest.push_back(channel.data());
    if (!reader_->read(start, static_cast<int>(count), dest.data()))
        return nullptr;
    return block;
}

bool PeakStore::storeBlock(const BlockKey& key, std::shared_ptr<const engine::SampleBlock> block)
{
    const auto sizeOf = [](const engine::SampleBlock& b)
    { return b.length() * static_cast<std::int64_t>(b.channels.size()); };
    const auto size = sizeOf(*block);
    const auto now = juce::Time::getMillisecondCounter();
    while (blockSamples_ + size > blockBudgetSamples)
    {
        // Evict the least recently drawn block, unless it is still on screen.
        auto oldest = blocks_.end();
        for (auto it = blocks_.begin(); it != blocks_.end(); ++it)
            if (oldest == blocks_.end() || it->second.lastUse < oldest->second.lastUse)
                oldest = it;
        if (oldest == blocks_.end() || now - oldest->second.lastUse < blockKeepMilliseconds)
            return false;
        blockSamples_ -= sizeOf(*oldest->second.block);
        blocks_.erase(oldest);
    }
    blockSamples_ += size;
    blocks_[key] = {std::move(block), now};
    return true;
}

bool PeakStore::isBusy() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return building_ || !queue_.empty();
}

juce::File PeakStore::cacheFileFor(const Key& key)
{
    const juce::File media{juce::String(key.path)};
    return media.getSiblingFile(media.getFileName() + "." + juce::String(static_cast<int>(key.sampleRate)) +
                                ".northlight-peaks");
}

std::shared_ptr<const engine::PeakData> PeakStore::buildOrLoad(const Key& key)
{
    const juce::File media{juce::String(key.path)};
    const auto cache = cacheFileFor(key);

    if (cache.existsAsFile() && cache.getLastModificationTime() >= media.getLastModificationTime())
    {
        juce::MemoryBlock bytes;
        if (cache.loadFileAsData(bytes))
        {
            auto peaks = std::make_shared<engine::PeakData>();
            if (engine::PeakData::deserialise(
                    std::string(static_cast<const char*>(bytes.getData()), bytes.getSize()), *peaks))
                return peaks;
        }
    }

    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(media));
    if (reader == nullptr)
        return std::make_shared<engine::PeakData>(); // empty: unreadable media

    JuceStreamReader stream(std::move(reader), key.sampleRate);
    auto peaks = std::make_shared<engine::PeakData>(
        engine::PeakData::build(stream,
                                engine::PeakData::defaultBaseSamplesPerBin,
                                engine::PeakData::defaultLevelCount,
                                [this](double) { return !threadShouldExit(); }));
    if (peaks->empty())
        return nullptr; // aborted

    const auto bytes = peaks->serialise();
    cache.replaceWithData(bytes.data(), bytes.size()); // best effort; a read-only folder just means no cache
    return peaks;
}

void PeakStore::run()
{
    while (!threadShouldExit())
    {
        // Blocks for a view on screen go ahead of peak builds.
        std::optional<BlockKey> blockKey;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (!blockQueue_.empty())
            {
                blockKey = blockQueue_.front();
                blockQueue_.pop_front();
            }
        }
        if (blockKey.has_value())
        {
            auto block = readBlock(*blockKey);
            bool stored = false;
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                blockQueued_.erase(*blockKey);
                if (block != nullptr)
                    stored = storeBlock(*blockKey, std::move(block));
            }
            if (stored)
                triggerAsyncUpdate();
            continue;
        }

        Key key;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty())
            {
                building_ = false;
            }
            else
            {
                key = queue_.front();
                queue_.pop_front();
                building_ = true;
            }
        }
        if (!building_)
        {
            wait(-1);
            continue;
        }

        auto peaks = buildOrLoad(key);
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            queued_.erase(key);
            if (peaks != nullptr)
                ready_[key] = std::move(peaks);
            building_ = false;
        }
        triggerAsyncUpdate();
    }
}

void PeakStore::handleAsyncUpdate()
{
    sendChangeMessage();
}

} // namespace northlight::audio
