#pragma once

#include "engine/AudioSource.h"
#include "engine/DiskThread.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <optional>
#include <string>

// Opens audio files through JUCE's format readers (WAV, AIFF, FLAC, OGG, MP3 where
// available). Short files are decoded into memory (resampled on load); longer ones
// become StreamingAudioSources fed by the disk thread (resampled as they stream).

namespace northlight::audio
{

class JuceMediaLoader final : public engine::MediaLoader
{
public:
    // With `alwaysInMemory` every file is loaded whole (offline rendering, which
    // would outrun the disk thread's streaming).
    explicit JuceMediaLoader(engine::DiskThread& disk, bool alwaysInMemory = false);

    // Files longer than this (at the target rate) are streamed from disk.
    static constexpr double memoryThresholdSeconds = 30.0;

    std::shared_ptr<engine::AudioSource>
    load(const std::string& path, double targetSampleRate, std::string& error) override;

    struct Info
    {
        double sampleRate = 0.0;
        int numChannels = 0;
        std::int64_t lengthSamples = 0; // at the file's own rate

        std::int64_t lengthAt(double rate) const noexcept;
    };
    // Reads the header only.
    std::optional<Info> probe(const std::string& path, std::string& error);

    juce::String supportedExtensions() const; // e.g. "*.wav;*.flac"

private:
    engine::DiskThread& disk_;
    bool alwaysInMemory_ = false;
    juce::AudioFormatManager formats_;
};

} // namespace northlight::audio
