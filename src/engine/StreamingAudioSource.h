#pragma once

#include "engine/AudioSource.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Disk-streamed audio.
//
// A StreamingAudioSource keeps a window of decoded samples per channel in a ring
// buffer. The real-time thread reads from the window and records the position it
// wants next; the disk thread (DiskThread) calls service() to keep the window filled
// ahead of that position, or to rebuild it after a locate. Reads that fall outside
// the window return silence and count as underruns rather than blocking.
//
// Most underruns are expected: the first blocks after a locate play silence while
// the window is rebuilt. A starvation is the other kind: the window had caught up
// (filled to the read-ahead since the last rebuild) and playback then ran past its
// end, so the disk thread fell behind. Starvations are audible gaps in steady
// playback and are what the dropout counter reports.
//
// One window serves one reader: every clip gets its own instance (createInstance,
// through the factory the loader installs) so two clips of the same file, or a loop
// longer than the window, never fight over where the window should be.
//
// Thread safety without locks:
//   - validStart_/validEnd_ are written only by the disk thread (release) and read by
//     the real-time thread (acquire); wanted_ the other way round.
//   - The disk thread never overwrites samples within `guard` samples behind the
//     real-time thread's wanted position, so a read that passed the window check
//     cannot have its data replaced underneath it while appending.
//   - A rebuild (seek) first empties the window, then waits a short grace period so
//     any read already in flight (bounded by one block) has usually finished, then
//     refills. "Usually" is not enough on a loaded machine (a preempted reader can
//     outlast the grace), so a seek first bumps generation_, and a read that saw it
//     change while copying returns silence.

namespace northlight::engine
{

// Pulls decoded samples from a file. Implemented with JUCE in the audio layer.
// Called from the disk thread; may block.
class StreamReader
{
public:
    virtual ~StreamReader() = default;
    virtual int numChannels() const noexcept = 0;
    virtual std::int64_t lengthSamples() const noexcept = 0;
    virtual double sampleRate() const noexcept = 0;
    // Fills dest[channel][0..numSamples) with samples from `start`, zero-filling past
    // the end. Returns false on an I/O error (dest is then undefined).
    virtual bool read(std::int64_t start, int numSamples, float* const* dest) = 0;
};

class StreamingAudioSource final : public AudioSource
{
public:
    struct Options
    {
        int ringSamples = 1 << 17;      // ~2.7 s at 48 kHz per channel
        int readAheadSamples = 1 << 16; // refill when less than this is buffered ahead
        int chunkSamples = 1 << 14;     // read granularity
        int guardSamples = 1 << 12;     // never overwrite this much behind the wanted position
        int seekGraceMilliseconds = 5;
    };

    StreamingAudioSource(std::unique_ptr<StreamReader> reader, Options options = {});
    ~StreamingAudioSource() override;

    // AudioSource (real-time read)
    int numChannels() const noexcept override { return numChannels_; }
    std::int64_t lengthSamples() const noexcept override { return length_; }
    double sampleRate() const noexcept override { return sampleRate_; }
    void read(int channel, std::int64_t sourceStart, float* dest, int numSamples) const noexcept override;

    // Disk thread. Does at most one chunk of work; returns true if it did any.
    bool service();

    // Controller: hint where playback will start so the window is ready.
    void prefetch(std::int64_t position) noexcept;

    // Installs how to open another reader on the same file and what to do with a
    // new instance (register it with the disk thread). With a factory installed,
    // createInstance() returns a private window per clip.
    using OpenReader = std::function<std::unique_ptr<StreamReader>()>;
    using OnInstance = std::function<void(const std::shared_ptr<StreamingAudioSource>&)>;
    void setInstanceFactory(OpenReader openReader, OnInstance onInstance);
    std::shared_ptr<AudioSource> createInstance() const override;

    std::uint64_t underruns() const noexcept { return underruns_.load(std::memory_order_relaxed); }
    // Underruns that were the disk falling behind, not a locate. Counted once per
    // block (on channel 0), and once per gap: the next block past the window is a
    // rebuild.
    std::uint64_t starvations() const noexcept { return starvations_.load(std::memory_order_relaxed); }
    bool hadReadError() const noexcept { return readError_.load(std::memory_order_relaxed); }

    // Window currently valid for reads, for tests and diagnostics.
    std::int64_t windowStart() const noexcept { return validStart_.load(std::memory_order_acquire); }
    std::int64_t windowEnd() const noexcept { return validEnd_.load(std::memory_order_acquire); }

private:
    bool fill(std::int64_t from, int count);
    void bumpGeneration() noexcept; // before a seek rewrites the ring
    std::int64_t clampToFile(std::int64_t position) const noexcept;

    std::unique_ptr<StreamReader> reader_;
    Options options_;
    OpenReader openReader_;
    OnInstance onInstance_;
    int numChannels_ = 0;
    std::int64_t length_ = 0;
    double sampleRate_ = 0.0;

    std::vector<std::vector<float>> ring_; // [channel][ringSamples]
    std::vector<float*> chunkPointers_;    // scratch for reader_->read into the ring

    std::atomic<std::int64_t> validStart_{0};
    std::atomic<std::int64_t> validEnd_{0};
    std::atomic<std::uint64_t> generation_{0}; // bumped before ring slots a read may hold are rewritten
    mutable std::atomic<std::int64_t> wanted_{0};
    mutable std::atomic<std::uint64_t> underruns_{0};
    mutable std::atomic<std::uint64_t> starvations_{0};
    std::atomic<bool> caughtUp_{false}; // filled to the read-ahead since the last rebuild
    std::atomic<bool> readError_{false};
};

} // namespace northlight::engine
