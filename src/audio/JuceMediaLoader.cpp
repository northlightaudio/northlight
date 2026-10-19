#include "audio/JuceMediaLoader.h"

#include "engine/rt/RealtimeGuard.h"

#include <cmath>

namespace aural::audio
{

namespace
{
std::vector<float> resample(const std::vector<float>& input, double ratio, std::int64_t outputLength)
{
    // ratio = source rate / target rate (the "speed" the interpolator reads input at).
    std::vector<float> padded(input);
    padded.resize(input.size() + 16, 0.0f); // the interpolator may look a few samples ahead
    std::vector<float> output(static_cast<size_t>(outputLength), 0.0f);
    juce::LagrangeInterpolator interpolator;
    interpolator.reset();
    interpolator.process(ratio, padded.data(), output.data(), static_cast<int>(outputLength));
    return output;
}
} // namespace

std::int64_t JuceMediaLoader::Info::lengthAt(double rate) const noexcept
{
    if (sampleRate <= 0.0 || rate <= 0.0)
        return lengthSamples;
    return static_cast<std::int64_t>(std::llround(static_cast<double>(lengthSamples) * rate / sampleRate));
}

JuceMediaLoader::JuceMediaLoader()
{
    formats_.registerBasicFormats();
}

juce::String JuceMediaLoader::supportedExtensions() const
{
    return formats_.getWildcardForAllFormats();
}

std::optional<JuceMediaLoader::Info> JuceMediaLoader::probe(const std::string& path, std::string& error)
{
    AURAL_ASSERT_NOT_REALTIME("JuceMediaLoader::probe");
    const juce::File file{juce::String(path)};
    if (!file.existsAsFile())
    {
        error = "file not found";
        return std::nullopt;
    }
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "unsupported or corrupt audio file";
        return std::nullopt;
    }
    return Info{reader->sampleRate, static_cast<int>(reader->numChannels), reader->lengthInSamples};
}

std::shared_ptr<engine::AudioSource>
JuceMediaLoader::load(const std::string& path, double targetSampleRate, std::string& error)
{
    AURAL_ASSERT_NOT_REALTIME("JuceMediaLoader::load");
    const juce::File file{juce::String(path)};
    if (!file.existsAsFile())
    {
        error = "file not found";
        return nullptr;
    }
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr)
    {
        error = "unsupported or corrupt audio file";
        return nullptr;
    }
    if (reader->lengthInSamples <= 0 || reader->numChannels == 0)
    {
        error = "empty audio file";
        return nullptr;
    }
    if (reader->lengthInSamples > (1 << 30))
    {
        error = "file too long to load into memory";
        return nullptr;
    }

    const auto length = static_cast<int>(reader->lengthInSamples);
    const auto numChannels = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> buffer(numChannels, length);
    if (!reader->read(&buffer, 0, length, 0, true, true))
    {
        error = "could not read audio data";
        return nullptr;
    }

    std::vector<std::vector<float>> channels(static_cast<size_t>(numChannels));
    for (int ch = 0; ch < numChannels; ++ch)
        channels[static_cast<size_t>(ch)].assign(buffer.getReadPointer(ch),
                                                 buffer.getReadPointer(ch) + length);

    if (targetSampleRate > 0.0 && std::abs(reader->sampleRate - targetSampleRate) > 0.5)
    {
        const double ratio = reader->sampleRate / targetSampleRate;
        const auto outputLength = static_cast<std::int64_t>(std::llround(length / ratio));
        for (auto& c : channels)
            c = resample(c, ratio, outputLength);
        return std::make_shared<engine::MemoryAudioSource>(std::move(channels), targetSampleRate);
    }
    return std::make_shared<engine::MemoryAudioSource>(std::move(channels), reader->sampleRate);
}

} // namespace aural::audio
