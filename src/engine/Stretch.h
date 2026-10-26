#pragma once

#include "core/Model.h"
#include "engine/AudioSource.h"
#include "engine/pitch/VocalRender.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

// Time-stretch and pitch-shift for audio clips (AudioClip::stretch, ::transpose),
// with the Signalsmith Stretch library.
//
// A stretched clip plays a StretchedSource: the media around the part the clip
// uses, stretched and transposed, rendered on a background worker into a buffer
// sized up front. The worker publishes how much of the buffer is finished through
// an atomic (release), and the audio thread reads only that much (acquire); the
// rest reads as silence until it is ready, so playback starts as soon as the
// render reaches the playhead, and no recompile is needed while it renders.
//
// Positions in a StretchedSource are stretched source samples (source sample x
// stretch), so the snapshot compiler scales a clip's sourceOffset by its stretch
// and the renderer reads it like any other source.
//
// Pitch edits come first: a clip with vocal edits reaches sourceFor as the
// vocal service's PitchEditedSource, and the render stretches the media with
// those rendered spans laid over it (the edits are in media samples and the
// clip's own pitch, so the stretch and the transposition act on the edited
// voice). A new set of spans renders afresh; until it is ready the clip's
// previous render of the same range plays on, so a pitch edit never drops out.
//
// Controller-thread API, except onChanged (called from a worker).

namespace northlight::engine
{

// Stretches `input` (channels of equal length) to `outputLength` samples and
// transposes it by `semitones`, aligned so output sample i plays input sample
// i * inputLength / outputLength. Calls `progress(done)` after each chunk of
// output it finishes (done = samples from the front). Not real-time.
void stretchAudio(const std::vector<const float*>& input,
                  std::int64_t inputLength,
                  const std::vector<float*>& output,
                  std::int64_t outputLength,
                  double sampleRate,
                  double semitones,
                  const std::function<void(std::int64_t done)>& progress = {});

// One render: a range of a file's samples, stretched.
struct StretchRender
{
    std::string path;
    double sampleRate = 0.0;
    double stretch = 1.0;
    double transpose = 0.0;
    std::int64_t sourceFrom = 0, sourceTo = 0; // the source range, in source samples
    std::int64_t start = 0;                    // its first sample in stretched units
    std::int64_t length = 0;                   // samples it holds when finished
    int numChannels = 1;
    // Rendered pitch-edit spans laid over the media before it is stretched (none:
    // the media as recorded).
    std::vector<std::shared_ptr<const pitch::RenderedSpan>> spans;
    bool supersedes = false; // a source plays an earlier render under it (guarded by the service's mutex)
    // Written by the worker before `ready` covers them; read by the audio thread
    // only below `ready`.
    std::vector<std::vector<float>> channels;
    std::atomic<std::int64_t> ready{0}; // finished samples from the front
    std::atomic<bool> finished{false};
    std::atomic<bool> failed{false};
};

// What a stretched clip plays. Real-time safe. `fallback` (optional) is an
// earlier render of the same range: it plays where `render` is not ready yet.
class StretchedSource final : public AudioSource
{
public:
    StretchedSource(int numChannels,
                    std::int64_t length,
                    double sampleRate,
                    std::shared_ptr<const StretchRender> render,
                    std::shared_ptr<const StretchRender> fallback = nullptr);

    int numChannels() const noexcept override { return numChannels_; }
    std::int64_t lengthSamples() const noexcept override { return length_; }
    double sampleRate() const noexcept override { return sampleRate_; }
    void read(int channel, std::int64_t sourceStart, float* dest, int numSamples) const noexcept override;

    const StretchRender& render() const noexcept { return *render_; }
    const StretchRender* fallback() const noexcept { return fallback_.get(); }

private:
    int numChannels_;
    std::int64_t length_;
    double sampleRate_;
    std::shared_ptr<const StretchRender> render_, fallback_;
};

class StretchService
{
public:
    // `loader` must load files whole (into memory). Synchronous: every render
    // happens inside sourceFor on the calling thread (offline bounce, tests).
    explicit StretchService(MediaLoader& loader, bool synchronous = false, int workers = 2);
    ~StretchService();
    StretchService(const StretchService&) = delete;
    StretchService& operator=(const StretchService&) = delete;

    // Called on a worker when a render finished or failed.
    std::function<void()> onChanged;

    // MediaCache's stretch hook: the clip's stretched source (rendering it in the
    // background on first sight); `base` is the clip's ordinary source, which
    // gives the channel count and length, or its PitchEditedSource, whose spans
    // the render lays over the media. Null for a clip it cannot stretch.
    std::shared_ptr<const AudioSource>
    sourceFor(const core::AudioClip& clip, double sampleRate, const std::shared_ptr<const AudioSource>& base);
    // True once after a render finished that replaced an earlier one still held
    // as a fallback: the owner recompiles (forgetting kept clip sources) so the
    // earlier render is released.
    bool takeRecompileRequest() noexcept { return recompile_.exchange(false); }

    // How far the render a clip plays has got, 0..1 (1 when not stretched or done).
    double progress(const core::AudioClip& clip, double sampleRate) const;
    bool isBusy() const;
    void waitUntilIdle();

    // The source range a clip's render covers: what it uses, widened by a margin
    // and rounded outwards so small trims keep their render.
    static std::pair<std::int64_t, std::int64_t>
    sourceRange(const core::AudioClip& clip, double sampleRate, std::int64_t mediaLength) noexcept;

private:
    using Key = std::tuple<std::string,
                           double,
                           double,
                           double,
                           std::int64_t,
                           std::int64_t,
                           std::vector<const pitch::RenderedSpan*>>;
    static bool sameRange(const StretchRender& a, const StretchRender& b) noexcept;
    void renderNow(StretchRender& render);
    std::shared_ptr<const AudioSource> media(const std::string& path, double sampleRate);
    void workerLoop();
    void prune();

    MediaLoader& loader_;
    const bool synchronous_;
    mutable std::mutex mutex_;
    std::mutex loadMutex_; // the loader is not thread-safe
    std::condition_variable wake_, idle_;
    std::map<Key, std::shared_ptr<StretchRender>> renders_;
    std::deque<std::shared_ptr<StretchRender>> queue_;
    std::map<std::pair<std::string, double>, std::shared_ptr<const AudioSource>> media_;
    std::map<core::Id, std::weak_ptr<StretchRender>> clipRenders_; // the last render given per clip
    std::atomic<bool> recompile_{false};
    int running_ = 0;
    bool quit_ = false;
    std::vector<std::thread> workers_;
};

} // namespace northlight::engine
