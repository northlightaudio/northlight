#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Audio that a clip plays. Sources are owned by the controller side (MediaCache) and
// referenced by snapshots; read() is called from the real-time thread and must be
// allocation-free and bounded.

namespace aural::engine
{

class AudioSource
{
public:
    virtual ~AudioSource() = default;

    virtual int numChannels() const noexcept = 0;
    virtual std::int64_t lengthSamples() const noexcept = 0;
    virtual double sampleRate() const noexcept = 0;

    // Real-time. Copies numSamples of `channel` starting at `sourceStart` into dest,
    // zero-filling anything outside [0, lengthSamples). Channels beyond numChannels
    // read as channel (channel % numChannels) so mono sources fan out.
    virtual void read(int channel, std::int64_t sourceStart, float* dest, int numSamples) const noexcept = 0;
};

// A source held entirely in memory. Phase 2's only source; disk streaming adds a
// second implementation behind the same interface.
class MemoryAudioSource final : public AudioSource
{
public:
    MemoryAudioSource(std::vector<std::vector<float>> channels, double sampleRate);

    int numChannels() const noexcept override { return static_cast<int>(channels_.size()); }
    std::int64_t lengthSamples() const noexcept override { return length_; }
    double sampleRate() const noexcept override { return sampleRate_; }
    void read(int channel, std::int64_t sourceStart, float* dest, int numSamples) const noexcept override;

private:
    std::vector<std::vector<float>> channels_;
    std::int64_t length_ = 0;
    double sampleRate_;
};

// Loads media for the engine. Implemented with JUCE in the app; tests use a
// synthetic loader. Called on the controller thread; may block on disk.
class MediaLoader
{
public:
    virtual ~MediaLoader() = default;
    // Returns a source at `targetSampleRate` (resampling if needed) or nullptr with
    // `error` set.
    virtual std::shared_ptr<AudioSource>
    load(const std::string& path, double targetSampleRate, std::string& error) = 0;
};

} // namespace aural::engine
