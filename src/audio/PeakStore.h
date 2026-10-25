#pragma once

#include "engine/Peaks.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

// Waveform peaks for the arrange view. peaksFor() returns the peaks for a media
// file if they are ready; otherwise it queues a build on the background thread and
// returns nullptr, and the store broadcasts a change (message thread) when they
// arrive. Built peaks are cached beside the media as "<file>.<rate>.northlight-peaks" and
// reused while they are newer than the media file.
//
// Zoomed in past the finest peak level, viewFor() also hands out decoded samples for
// the visible range. They are read in fixed blocks on the same thread (ahead of any
// peak build) and kept in memory up to a budget. A block drawn in the last couple of
// seconds is never evicted; when nothing can be, a new block is dropped rather than
// stored, and the view keeps drawing peaks there. So a block arriving never pushes
// out one that is on screen, and loading cannot loop.

namespace northlight::audio
{

class PeakStore final : public juce::ChangeBroadcaster, private juce::Thread, private juce::AsyncUpdater
{
public:
    PeakStore();
    ~PeakStore() override;

    // Message thread. Peaks are in samples at `sampleRate` (the project rate), so
    // they line up with clip positions.
    std::shared_ptr<const engine::PeakData> peaksFor(const std::string& path, double sampleRate);

    // Message thread. Peaks plus, when `samplesPerPixel` is finer than the finest peak
    // level, whatever decoded blocks cover source samples [start, end); missing blocks
    // are queued and a change message follows when they land. Empty until the peaks
    // are ready.
    engine::WaveformView viewFor(const std::string& path,
                                 double sampleRate,
                                 std::int64_t start,
                                 std::int64_t end,
                                 double samplesPerPixel);

    // Decoded samples kept in memory across every file (summed over channels).
    static constexpr std::int64_t blockBudgetSamples = std::int64_t{1} << 24; // 64 MB of floats
    static constexpr std::uint32_t blockKeepMilliseconds = 2000;

    // True while any build is queued or running.
    bool isBusy() const;

private:
    struct Key
    {
        std::string path;
        double sampleRate;
        bool operator<(const Key& o) const noexcept
        {
            return path < o.path || (path == o.path && sampleRate < o.sampleRate);
        }
    };
    // A key looked up without copying its path (a view paints this per clip).
    struct KeyView
    {
        std::string_view path;
        double sampleRate;
    };
    struct KeyLess
    {
        using is_transparent = void;
        static bool less(std::string_view ap, double ar, std::string_view bp, double br) noexcept
        {
            return ap < bp || (ap == bp && ar < br);
        }
        bool operator()(const Key& a, const Key& b) const noexcept { return a < b; }
        bool operator()(const Key& a, const KeyView& b) const noexcept
        {
            return less(a.path, a.sampleRate, b.path, b.sampleRate);
        }
        bool operator()(const KeyView& a, const Key& b) const noexcept
        {
            return less(a.path, a.sampleRate, b.path, b.sampleRate);
        }
    };

    struct BlockKey
    {
        Key file;
        std::int64_t index;
        bool operator<(const BlockKey& o) const noexcept
        {
            return file < o.file || (!(o.file < file) && index < o.index);
        }
    };
    struct BlockKeyView
    {
        KeyView file;
        std::int64_t index;
    };
    struct BlockKeyLess
    {
        using is_transparent = void;
        static bool less(const KeyView& a, std::int64_t ai, const KeyView& b, std::int64_t bi) noexcept
        {
            if (KeyLess::less(a.path, a.sampleRate, b.path, b.sampleRate))
                return true;
            if (KeyLess::less(b.path, b.sampleRate, a.path, a.sampleRate))
                return false;
            return ai < bi;
        }
        static KeyView view(const Key& k) noexcept { return {k.path, k.sampleRate}; }
        bool operator()(const BlockKey& a, const BlockKey& b) const noexcept { return a < b; }
        bool operator()(const BlockKey& a, const BlockKeyView& b) const noexcept
        {
            return less(view(a.file), a.index, b.file, b.index);
        }
        bool operator()(const BlockKeyView& a, const BlockKey& b) const noexcept
        {
            return less(a.file, a.index, view(b.file), b.index);
        }
    };
    struct CachedBlock
    {
        std::shared_ptr<const engine::SampleBlock> block;
        std::uint32_t lastUse = 0; // juce::Time::getMillisecondCounter()
    };

    void run() override;
    void handleAsyncUpdate() override;
    static juce::File cacheFileFor(const Key& key);
    std::shared_ptr<const engine::PeakData> buildOrLoad(const Key& key);
    std::shared_ptr<const engine::SampleBlock> readBlock(const BlockKey& key);              // peak thread
    bool storeBlock(const BlockKey& key, std::shared_ptr<const engine::SampleBlock> block); // holds mutex_

    juce::AudioFormatManager formats_;
    mutable std::mutex mutex_;
    std::map<Key, std::shared_ptr<const engine::PeakData>, KeyLess> ready_;
    std::deque<Key> queue_;
    std::map<Key, bool, KeyLess> queued_;
    bool building_ = false;

    std::map<BlockKey, CachedBlock, BlockKeyLess> blocks_;
    std::deque<BlockKey> blockQueue_;
    std::set<BlockKey> blockQueued_;
    std::int64_t blockSamples_ = 0; // held in blocks_, all channels

    // Peak thread only: the last file opened for block reads, kept open for the next.
    Key readerKey_;
    std::unique_ptr<engine::StreamReader> reader_;
};

} // namespace northlight::audio
