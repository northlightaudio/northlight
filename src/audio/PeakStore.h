#pragma once

#include "engine/Peaks.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>

// Waveform peaks for the arrange view. peaksFor() returns the peaks for a media
// file if they are ready; otherwise it queues a build on the background thread and
// returns nullptr, and the store broadcasts a change (message thread) when they
// arrive. Built peaks are cached beside the media as "<file>.<rate>.aural-peaks" and
// reused while they are newer than the media file.

namespace aural::audio
{

class PeakStore final : public juce::ChangeBroadcaster, private juce::Thread, private juce::AsyncUpdater
{
public:
    PeakStore();
    ~PeakStore() override;

    // Message thread. Peaks are in samples at `sampleRate` (the project rate), so
    // they line up with clip positions.
    std::shared_ptr<const engine::PeakData> peaksFor(const std::string& path, double sampleRate);

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

    void run() override;
    void handleAsyncUpdate() override;
    static juce::File cacheFileFor(const Key& key);
    std::shared_ptr<const engine::PeakData> buildOrLoad(const Key& key);

    juce::AudioFormatManager formats_;
    mutable std::mutex mutex_;
    std::map<Key, std::shared_ptr<const engine::PeakData>> ready_;
    std::deque<Key> queue_;
    std::map<Key, bool> queued_;
    bool building_ = false;
};

} // namespace aural::audio
