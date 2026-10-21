#include "engine/AudioSource.h"

#include <algorithm>

namespace northlight::engine
{

MemoryAudioSource::MemoryAudioSource(std::vector<std::vector<float>> channels, double sampleRate)
    : channels_(std::move(channels)), sampleRate_(sampleRate)
{
    for (const auto& c : channels_)
        length_ = std::max(length_, static_cast<std::int64_t>(c.size()));
    for (auto& c : channels_)
        c.resize(static_cast<size_t>(length_), 0.0f);
}

void MemoryAudioSource::read(int channel,
                             std::int64_t sourceStart,
                             float* dest,
                             int numSamples) const noexcept
{
    if (numSamples <= 0)
        return;
    if (channels_.empty() || length_ == 0)
    {
        std::fill(dest, dest + numSamples, 0.0f);
        return;
    }

    const auto& data = channels_[static_cast<size_t>(channel % numChannels())];
    const std::int64_t first = std::max<std::int64_t>(sourceStart, 0);
    const std::int64_t last = std::min<std::int64_t>(sourceStart + numSamples, length_); // exclusive

    if (last <= first)
    {
        std::fill(dest, dest + numSamples, 0.0f);
        return;
    }

    const auto leadingZeros = static_cast<int>(first - sourceStart);
    const auto count = static_cast<int>(last - first);
    std::fill(dest, dest + leadingZeros, 0.0f);
    std::copy_n(data.data() + first, count, dest + leadingZeros);
    std::fill(dest + leadingZeros + count, dest + numSamples, 0.0f);
}

} // namespace northlight::engine
