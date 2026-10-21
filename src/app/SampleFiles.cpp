#include "app/SampleFiles.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>

namespace aural
{

SampleFiles::SampleFiles()
{
    formats_.registerBasicFormats();
}

std::shared_ptr<const plugins::sampler::Sample> SampleFiles::load(const std::string& path, std::string& error)
{
    AURAL_ASSERT_NOT_REALTIME("SampleFiles::load");
    const juce::File file(juce::String::fromUTF8(path.c_str()));
    if (!file.existsAsFile())
    {
        error = "file not found";
        return nullptr;
    }
    const std::scoped_lock lock(mutex_);
    const auto modified = file.getLastModificationTime();
    if (const auto it = cache_.find(path); it != cache_.end() && it->second.modified == modified)
        if (auto shared = it->second.sample.lock())
            return shared;
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0)
    {
        error = "unsupported or corrupt audio file";
        return nullptr;
    }
    const auto frames = static_cast<int>(std::min<juce::int64>(
        reader->lengthInSamples, static_cast<juce::int64>(maxSeconds * reader->sampleRate)));
    const int channels = std::min(2, static_cast<int>(reader->numChannels));
    if (frames <= 0 || channels <= 0)
    {
        error = "the file has no audio";
        return nullptr;
    }
    juce::AudioBuffer<float> buffer(channels, frames);
    if (!reader->read(&buffer, 0, frames, 0, true, channels > 1))
    {
        error = "could not read the file";
        return nullptr;
    }
    auto sample = std::make_shared<plugins::sampler::Sample>();
    sample->path = path;
    sample->sampleRate = reader->sampleRate;
    sample->left.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + frames);
    if (channels > 1)
        sample->right.assign(buffer.getReadPointer(1), buffer.getReadPointer(1) + frames);
    std::erase_if(cache_, [](const auto& entry) { return entry.second.sample.expired(); });
    cache_[path] = {sample, modified};
    return sample;
}

} // namespace aural
