#include "audio/JuceStreamReader.h"

#include "engine/Resampling.h"
#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <cmath>

namespace northlight::audio
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
    }
}

bool JuceStreamReader::read(std::int64_t start, int numSamples, float* const* dest)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("JuceStreamReader::read");
    if (numSamples <= 0)
        return true;

    if (ratio_ == 1.0)
        return reader_->read(dest, numChannels_, start, numSamples);

    std::int64_t sourceStart = 0;
    int sourceCount = 0;
    engine::sourceRangeFor(ratio_, start, numSamples, sourceStart, sourceCount);
    if (sourceBuffer_.getNumChannels() < numChannels_ || sourceBuffer_.getNumSamples() < sourceCount)
        sourceBuffer_.setSize(numChannels_, sourceCount, false, false, true);
    // The format reader zero-fills outside the file, so negative starts are fine.
    if (!reader_->read(sourceBuffer_.getArrayOfWritePointers(), numChannels_, sourceStart, sourceCount))
        return false;

    for (int ch = 0; ch < numChannels_; ++ch)
        engine::resampleHermite(
            sourceBuffer_.getReadPointer(ch), sourceStart, sourceCount, ratio_, start, numSamples, dest[ch]);
    return true;
}

} // namespace northlight::audio
