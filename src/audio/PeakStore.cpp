#include "audio/PeakStore.h"

#include "audio/JuceStreamReader.h"
#include "engine/rt/RealtimeGuard.h"

namespace aural::audio
{

PeakStore::PeakStore() : juce::Thread("aural peaks")
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
    AURAL_ASSERT_NOT_REALTIME("PeakStore::peaksFor");
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

bool PeakStore::isBusy() const
{
    const std::lock_guard<std::mutex> lock(mutex_);
    return building_ || !queue_.empty();
}

juce::File PeakStore::cacheFileFor(const Key& key)
{
    const juce::File media{juce::String(key.path)};
    return media.getSiblingFile(media.getFileName() + "." + juce::String(static_cast<int>(key.sampleRate)) +
                                ".aural-peaks");
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

} // namespace aural::audio
