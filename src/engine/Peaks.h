#pragma once

#include "engine/StreamingAudioSource.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Mipmapped waveform peaks. Built once per media file on a background thread
// (PeakStore in the audio layer), cached beside the media, and read by the waveform
// views at whatever zoom level is on screen: lookup() picks the coarsest level whose
// bins are still finer than the requested span, so painting never scans much.
//
// Zoomed in past the finest level (256 samples per bin) the peaks have no more
// detail, so the store also decodes the visible part of the file in blocks, and a
// WaveformView reads those samples directly where they cover the span.

namespace northlight::engine
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

// Decoded samples for one aligned stretch of a file: samples
// [index * samplesPerBlock, (index + 1) * samplesPerBlock), shorter at the file's end.
struct SampleBlock
{
    static constexpr std::int64_t samplesPerBlock = 1 << 16;

    std::int64_t index = 0;
    std::vector<std::vector<float>> channels; // [channel][sample]

    std::int64_t start() const noexcept { return index * samplesPerBlock; }
    std::int64_t length() const noexcept
    {
        return channels.empty() ? 0 : static_cast<std::int64_t>(channels[0].size());
    }
};

// What a painter reads for one file: its peaks, plus decoded blocks for the visible
// range when zoomed in past the finest peak level.
class WaveformView
{
public:
    WaveformView() = default;
    // `blocks` holds consecutive block indices from `firstBlock`; null where a block is
    // not decoded yet.
    WaveformView(std::shared_ptr<const PeakData> peaks,
                 std::int64_t firstBlock,
                 std::vector<std::shared_ptr<const SampleBlock>> blocks);

    bool empty() const noexcept { return peaks_ == nullptr || peaks_->empty(); }
    int numChannels() const noexcept { return empty() ? 0 : peaks_->numChannels(); }
    const PeakData* peaks() const noexcept { return peaks_.get(); }

    // Min/max over [start, end). A span shorter than the finest peak bin reads the
    // decoded samples when blocks cover it, including the sample just before `start`
    // so neighbouring pixel columns join into a continuous trace. Otherwise it falls
    // back to the peaks.
    PeakRange lookup(int channel, std::int64_t start, std::int64_t end) const noexcept;

    // True when [start, end) is wholly covered by decoded blocks (clamped to the file).
    bool hasSamples(std::int64_t start, std::int64_t end) const noexcept;

private:
    const SampleBlock* blockAt(std::int64_t sample) const noexcept;

    std::shared_ptr<const PeakData> peaks_;
    std::int64_t firstBlock_ = 0;
    std::vector<std::shared_ptr<const SampleBlock>> blocks_;
};

// The block indices a view needs to draw [start, end) of a file `length` samples
// long, as a half-open range; empty when the range misses the file.
struct BlockSpan
{
    std::int64_t first = 0;
    std::int64_t last = 0; // one past
};
BlockSpan blocksFor(std::int64_t start, std::int64_t end, std::int64_t length) noexcept;

} // namespace northlight::engine
