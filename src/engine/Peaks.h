#pragma once

#include "engine/StreamingAudioSource.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Mipmapped waveform peaks. Built once per media file on a background thread
// (PeakBuilder in the audio layer), cached beside the media, and read by the arrange
// view at whatever zoom level is on screen: lookup() picks the coarsest level whose
// bins are still finer than the requested span, so painting never scans samples.

namespace aural::engine
{

struct PeakLevel
{
    int samplesPerBin = 0;
    std::vector<float> minimum; // one per bin
    std::vector<float> maximum;
};

struct PeakRange
{
    float minimum = 0.0f;
    float maximum = 0.0f;
};

class PeakData
{
public:
    static constexpr int defaultBaseSamplesPerBin = 256;
    static constexpr int levelRatio = 4;
    static constexpr int defaultLevelCount = 6; // 256 .. 262144 samples per bin

    int numChannels() const noexcept { return static_cast<int>(channels_.size()); }
    std::int64_t lengthSamples() const noexcept { return length_; }
    double sampleRate() const noexcept { return sampleRate_; }
    int levelCount() const noexcept { return channels_.empty() ? 0 : static_cast<int>(channels_[0].size()); }
    const PeakLevel& level(int channel, int index) const
    {
        return channels_[static_cast<size_t>(channel)][static_cast<size_t>(index)];
    }

    // Min/max over [start, end) of `channel`, from the coarsest level whose bins fit
    // the span (so a span of 2000 samples uses the 256-sample level, 1 million uses
    // the 262144 one). Out-of-range parts read as silence.
    PeakRange lookup(int channel, std::int64_t start, std::int64_t end) const noexcept;

    // Builds every level by reading the whole file through `reader` in chunks.
    // `progress` (0..1) is reported after each chunk; return false from it to abort.
    static PeakData build(StreamReader& reader,
                          int baseSamplesPerBin = defaultBaseSamplesPerBin,
                          int levelCount = defaultLevelCount,
                          const std::function<bool(double)>& progress = {});

    // Compact binary serialisation for the cache file beside the media.
    std::string serialise() const;
    static bool deserialise(const std::string& bytes, PeakData& out);

    bool empty() const noexcept { return channels_.empty(); }

private:
    std::int64_t length_ = 0;
    double sampleRate_ = 0.0;
    std::vector<std::vector<PeakLevel>> channels_; // [channel][level]
};

} // namespace aural::engine
