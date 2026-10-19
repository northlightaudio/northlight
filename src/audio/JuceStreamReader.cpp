#include "audio/JuceStreamReader.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <cmath>

namespace aural::audio
{

JuceStreamReader::JuceStreamReader(std::unique_ptr<juce::AudioFormatReader> reader, double targetSampleRate)
    : reader_(std::move(reader)), numChannels_(static_cast<int>(reader_->numChannels)),
      targetRate_(targetSampleRate > 0.0 ? targetSampleRate : reader_->sampleRate)
{
    ratio_ = reader_->sampleRate / targetRate_;
    if (std::abs(ratio_ - 1.0) < 1e-9)
    {
        ratio_ = 1.0;
        length_ = reader_->lengthInSamples;
    }
    else
    {
        length_ =
            static_cast<std::int64_t>(std::llround(static_cast<double>(reader_->lengthInSamples) / ratio_));
        interpolators_.resize(static_cast<size_t>(numChannels_));
    }
}

bool JuceStreamReader::read(std::int64_t start, int numSamples, float* const* dest)
{
    AURAL_ASSERT_NOT_REALTIME("JuceStreamReader::read");
    if (numSamples <= 0)
        return true;

    if (ratio_ == 1.0)
        return reader_->read(dest, numChannels_, start, numSamples);

    // Resample: pull enough source samples for this chunk, continuing the
    // interpolator state when the request follows the previous one.
    if (start != nextSequentialTarget_)
    {
        for (auto& i : interpolators_)
            i.reset();
        sourcePosition_ = static_cast<double>(start) * ratio_;
    }

    const auto sourceStart = static_cast<std::int64_t>(std::floor(sourcePosition_));
    const int sourceCount = static_cast<int>(std::ceil(numSamples * ratio_)) + 8;
    if (sourceBuffer_.getNumChannels() < numChannels_ || sourceBuffer_.getNumSamples() < sourceCount)
        sourceBuffer_.setSize(numChannels_, sourceCount, false, false, true);
    sourceBuffer_.clear();
    if (!reader_->read(sourceBuffer_.getArrayOfWritePointers(), numChannels_, sourceStart, sourceCount))
        return false;

    int used = 0;
    for (int ch = 0; ch < numChannels_; ++ch)
        used = interpolators_[static_cast<size_t>(ch)].process(
            ratio_, sourceBuffer_.getReadPointer(ch), dest[ch], numSamples);

    sourcePosition_ = static_cast<double>(sourceStart) + used;
    nextSequentialTarget_ = start + numSamples;
    return true;
}

} // namespace aural::audio
