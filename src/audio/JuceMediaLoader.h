#pragma once

#include "engine/AudioSource.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <optional>
#include <string>

// Loads audio files into MemoryAudioSource through JUCE's format readers (WAV, AIFF,
// FLAC, OGG, MP3 where available), resampling to the target rate on load. Phase 2:
// whole files in memory; the streaming source replaces this for long files.

namespace aural::audio
{

class JuceMediaLoader final : public engine::MediaLoader
{
public:
    JuceMediaLoader();

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
    juce::AudioFormatManager formats_;
};

} // namespace aural::audio
