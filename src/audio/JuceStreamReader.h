#pragma once

#include "engine/StreamingAudioSource.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

// Feeds a StreamingAudioSource from a juce::AudioFormatReader, resampling on the
// disk thread (engine/Resampling.h) when the file's rate differs from the target
// rate. Positions and lengths seen by the engine are in target-rate samples; every
// read maps its output positions to exact source positions, so reads can land
// anywhere with no state carried between them.

namespace aural::audio
{

class JuceStreamReader final : public engine::StreamReader
{
public:
    JuceStreamReader(std::unique_ptr<juce::AudioFormatReader> reader, double targetSampleRate);

    int numChannels() const noexcept override { return numChannels_; }
    std::int64_t lengthSamples() const noexcept override { return length_; }
    double sampleRate() const noexcept override { return targetRate_; }
    bool read(std::int64_t start, int numSamples, float* const* dest) override;

private:
    std::unique_ptr<juce::AudioFormatReader> reader_;
    int numChannels_ = 0;
    double targetRate_ = 0.0;
    double ratio_ = 1.0; // source samples per target sample
    std::int64_t length_ = 0;
    juce::AudioBuffer<float> sourceBuffer_; // disk thread only
};

} // namespace aural::audio
