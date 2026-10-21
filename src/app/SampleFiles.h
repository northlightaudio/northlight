#pragma once

#include "plugins/Sampler.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace aural
{

// Decodes audio files for the built-in Sampler (its sampler::Loader): the whole
// file into memory at its own rate (the sampler interpolates to pitch), up to
// maxSeconds. A file already decoded is shared while any sampler holds it, so
// a recompile, an undo or a bounce does not decode it again; a file that
// changed on disk since is read afresh. Controller threads; may block on disk.
class SampleFiles
{
public:
    SampleFiles();

    static constexpr double maxSeconds = 600.0; // longer files are cut here

    std::shared_ptr<const plugins::sampler::Sample> load(const std::string& path, std::string& error);
    // The loader to give the insert factory (it refers to this object).
    plugins::sampler::Loader loader()
    {
        return [this](const std::string& path, std::string& error) { return load(path, error); };
    }

private:
    struct Cached
    {
        std::weak_ptr<const plugins::sampler::Sample> sample;
        juce::Time modified;
    };
    std::mutex mutex_;
    juce::AudioFormatManager formats_;
    std::map<std::string, Cached> cache_;
};

} // namespace aural
