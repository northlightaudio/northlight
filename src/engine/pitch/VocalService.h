#pragma once

#include "core/Model.h"
#include "engine/AudioSource.h"
#include "engine/pitch/PitchAnalysis.h"
#include "engine/pitch/VocalRender.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

// Vocal pitch editing for the engine (docs/design/screens/pitch-editing.md, "Where it fits").
//
// The VocalService owns, per media file, its samples and its pitch analysis (made
// on a background thread, cached beside the media as "<file>.<rate>.northlight-pitch"),
// and the rendered spans of every vocal edit. The snapshot compiler asks it for a
// clip's source (MediaCache's clip hook): a clip whose edit changes the sound gets a
// PitchEditedSource that plays the rendered spans over its ordinary source. Spans
// still rendering are left out (or the clip's previous render of the same span
// plays) and onChanged fires when they are ready so the owner recompiles; the audio
// thread only ever sees finished, immutable spans.
//
// Controller-thread API (the message thread, or a bounce's own thread when
// synchronous): everything except onChanged, which is called from the worker.

namespace northlight::engine
{

// A clip's ordinary source with rendered spans laid over it. Real-time safe.
class PitchEditedSource final : public AudioSource
{
public:
    PitchEditedSource(std::shared_ptr<const AudioSource> base,
                      std::vector<std::shared_ptr<const pitch::RenderedSpan>> spans);

    int numChannels() const noexcept override { return base_->numChannels(); }
    std::int64_t lengthSamples() const noexcept override { return base_->lengthSamples(); }
    double sampleRate() const noexcept override { return base_->sampleRate(); }
    void read(int channel, std::int64_t sourceStart, float* dest, int numSamples) const noexcept override;

    const std::vector<std::shared_ptr<const pitch::RenderedSpan>>& spans() const noexcept { return spans_; }

private:
    std::shared_ptr<const AudioSource> base_;
    std::vector<std::shared_ptr<const pitch::RenderedSpan>> spans_; // sorted by start
};

class VocalService
{
public:
    // `loader` must load files whole (into memory): the renderer reads anywhere in
    // them. Synchronous: every analysis and render happens on the calling thread
    // inside the call (offline bounce, tests); no worker thread.
    explicit VocalService(MediaLoader& loader, bool synchronous = false);
    ~VocalService();
    VocalService(const VocalService&) = delete;
    VocalService& operator=(const VocalService&) = delete;

    // Called on the worker thread when an analysis or a render finished.
    std::function<void()> onChanged;

    enum class State
    {
        none,
        analysing,
        ready,
        failed,
    };
    struct Status
    {
        State state = State::none;
        double progress = 0.0; // while analysing
        std::string error;     // when failed
    };

    // The analysis of a file at the project rate; null until it is ready (the first
    // call starts it).
    std::shared_ptr<const pitch::PitchAnalysis> analysis(const std::string& path, double sampleRate);
    Status status(const std::string& path, double sampleRate) const;
    // The media's samples once the analysis has loaded them (for auditioning).
    std::shared_ptr<const std::vector<std::vector<float>>> samples(const std::string& path,
                                                                   double sampleRate) const;

    // MediaCache's clip hook: `base` (the clip's ordinary source) when the edit plays
    // the media as recorded or the analysis is not ready, else an edited source.
    std::shared_ptr<const AudioSource>
    sourceFor(const core::AudioClip& clip, double sampleRate, std::shared_ptr<const AudioSource> base);

    // Whether analyses or renders are queued or running.
    bool isBusy() const;
    // Blocks until nothing is queued or running (tests).
    void waitUntilIdle();
    // Where the analysis of `path` is cached ("" to disable caching for a path).
    static std::string cachePathFor(const std::string& path, double sampleRate);

private:
    struct Media
    {
        std::string path;
        double sampleRate = 0.0;
        State state = State::none;
        std::atomic<double> progress{0.0};
        std::string error;
        std::shared_ptr<const std::vector<std::vector<float>>> samples;
        std::shared_ptr<const pitch::PitchAnalysis> analysis;
    };
    struct RenderJob
    {
        std::shared_ptr<Media> media;
        core::VocalEdit edit;
        std::vector<pitch::SpanPlan> plans;
    };
    using Key = std::pair<std::string, double>;

    std::shared_ptr<Media> mediaFor(const std::string& path, double sampleRate, bool start);
    void analyseNow(Media& media);
    void renderNow(const RenderJob& job);
    void workerLoop();
    void prune();
    static std::uint64_t mediaKey(const std::string& path, double sampleRate) noexcept;

    MediaLoader& loader_;
    const bool synchronous_;
    mutable std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::map<Key, std::shared_ptr<Media>> media_;
    std::deque<std::shared_ptr<Media>> analyses_;
    std::deque<RenderJob> renders_;
    std::set<std::uint64_t> pending_; // span keys queued or rendering
    std::map<std::uint64_t, std::shared_ptr<const pitch::RenderedSpan>> spans_;
    std::deque<std::uint64_t> spanOrder_; // oldest first, for pruning
    std::map<core::Id, std::vector<std::shared_ptr<const pitch::RenderedSpan>>>
        clipSpans_; // last given per clip
    int running_ = 0;
    bool quit_ = false;
    std::thread worker_;
};

} // namespace northlight::engine
