#pragma once

#include "engine/AudioSource.h"
#include "engine/rt/SnapshotPublisher.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

// Auditioning a file (the browser's Files tab): one source played from its
// start into the device outputs, over the mix and outside it (no fader, no
// meters, never recorded or bounced), at a preview gain.
//
// Controller to real-time: the source goes over in an rt::SnapshotPublisher.
// A new file or a stop never cuts what is sounding: the controller asks the
// real-time thread to fade the current one out (5 ms), and only once it
// reports the fade done does pump() publish the next one (or nothing), which
// starts with a fade-in. The real-time side reads the source into scratch
// buffers sized in prepare(); it never allocates, locks or frees.

namespace northlight::engine
{

class PreviewPlayer
{
public:
    static constexpr double fadeSeconds = 0.005;

    // ----- controller thread ------------------------------------------------------------
    void prepare(double sampleRate, int maxBlockSize);
    // Plays the source from its start (at the engine's rate; a streaming source's
    // own instance). What plays now fades out first.
    void play(std::shared_ptr<AudioSource> source);
    void stop();
    void setGain(float linear) noexcept { gain_.store(linear, std::memory_order_relaxed); }
    float gain() const noexcept { return gain_.load(std::memory_order_relaxed); }
    // Hands a waiting source over once the one before has faded, and frees what
    // the real-time thread has finished with. Call often while previewing (the
    // engine calls it with its garbage collection); `realtimeRunning` false means
    // no audio thread runs, so nothing waits for a fade.
    void pump(bool realtimeRunning);
    // Whether a preview is playing or about to (until it ends or fades out).
    bool isPlaying() const noexcept;
    // Samples played of the current preview.
    std::int64_t position() const noexcept { return position_.load(std::memory_order_relaxed); }

    // ----- real-time thread -------------------------------------------------------------
    // Adds the preview to the outputs (channels 0 and 1; a mono source to both).
    void process(float* const* outputs, int numOutputs, int numSamples) noexcept;

private:
    struct Item
    {
        std::shared_ptr<AudioSource> source;
        std::uint64_t id = 0;
        std::uint64_t epoch = 0;
    };
    void publish(std::shared_ptr<AudioSource> source);

    rt::SnapshotPublisher<Item> publisher_;
    std::atomic<float> gain_{0.7f};
    std::atomic<std::uint64_t> stopId_{0}; // the id the real-time thread should fade out
    std::atomic<std::uint64_t> doneId_{0}; // the last id it finished (faded or ran out)
    std::atomic<std::int64_t> position_{0};
    // Controller.
    std::shared_ptr<AudioSource> pending_;
    bool hasPending_ = false;
    std::uint64_t nextId_ = 0, currentId_ = 0;
    // Real-time.
    std::vector<float> left_, right_;
    std::uint64_t rtId_ = 0;
    std::int64_t rtPosition_ = 0;
    float rtFade_ = 0.0f, fadeStep_ = 1.0f / 240.0f;
    bool rtDone_ = true;
};

} // namespace northlight::engine
