#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

// Live values a processor publishes for its editor (the channel strip's meters,
// gain-reduction history and spectrum analyser). The real-time thread writes,
// the UI thread reads; no locks, no allocation after construction.
//
// Every stored value is an atomic with one writer. A reader can see a frame or a
// tap sample that a concurrent write is replacing, which only ever shows on a
// display, and the rings are long enough (seconds) that it does not happen
// while the UI keeps up. This is the "RT to controller" path of rt-rules.md:
// a preallocated ring the RT side never blocks on.

namespace northlight::engine
{

class ProcessorFeedback
{
public:
    // One point of the level history.
    struct Frame
    {
        float input = 0.0f;     // peak |x| going in, linear
        float output = 0.0f;    // peak coming out, linear
        float reduction = 0.0f; // the most gain reduction, dB (>= 0)
        float extra = 0.0f;     // processor-specific: a gate's key level, a de-esser's band level
        float extra2 = 0.0f;    // a second processor-specific value (the glue compressor's output RMS)
    };
    static constexpr int historyLength = 1024;    // frames (about five seconds)
    static constexpr double frameSeconds = 0.005; // 200 frames a second
    static constexpr int tapLength = 16384;       // samples per tap
    static constexpr int tapCount = 2;            // 0 = before the processor, 1 = after
    static constexpr int valuesPerFrame = 5;

    explicit ProcessorFeedback(bool withTaps = false)
        : history_(new std::atomic<float>[historyLength * valuesPerFrame]()),
          taps_(withTaps ? new std::atomic<float>[tapCount * tapLength]() : nullptr)
    {
    }

    // ----- controller thread ------------------------------------------------------------
    void prepare(double sampleRate) noexcept
    {
        sampleRate_.store(sampleRate, std::memory_order_relaxed);
        interval_ = std::max(1, static_cast<int>(sampleRate * frameSeconds));
        building_ = {};
        count_ = 0;
    }

    // ----- real-time thread -------------------------------------------------------------
    // One sample's values; a frame is published every frameSeconds of audio.
    void
    addSample(float input, float output, float reductionDb, float extra = 0.0f, float extra2 = 0.0f) noexcept
    {
        accumulate(input, output, reductionDb, extra, extra2);
        if (++count_ >= interval_)
            publish();
    }

    // A block's peaks at once, for values that are only known per block.
    void addBlock(float input, float output, float reductionDb, float extra, int numSamples) noexcept
    {
        accumulate(input, output, reductionDb, extra, 0.0f);
        count_ += numSamples;
        while (count_ >= interval_)
        {
            const auto frame = building_;
            publish();
            if (count_ >= interval_)
                building_ = frame; // a long block fills several frames with its peaks
        }
    }

    // A bypassed processor's block: it passes the signal, so input and output
    // read the same peak and nothing is reduced; both taps get the signal. Only
    // while an editor is watching.
    void addBypassed(const float* left, const float* right, int numSamples) noexcept
    {
        if (!watched())
            return;
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
        addBlock(peak, peak, 0.0f, 0.0f, numSamples);
        tap(0, left, right, numSamples);
        tap(1, left, right, numSamples);
    }

    // The mono sum of a stereo block into tap `which`, when an analyser asked for it.
    void tap(int which, const float* left, const float* right, int numSamples) noexcept
    {
        if (taps_ == nullptr || !tapsEnabled_.load(std::memory_order_relaxed) || which < 0 ||
            which >= tapCount)
            return;
        auto* ring = taps_.get() + which * tapLength;
        auto written = tapWritten_[which].load(std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i, ++written)
            ring[written % tapLength].store(0.5f * (left[i] + right[i]), std::memory_order_relaxed);
        tapWritten_[which].store(written, std::memory_order_release);
    }

    bool tapsEnabled() const noexcept { return tapsEnabled_.load(std::memory_order_relaxed); }
    // Whether an editor is showing this processor (a bypassed processor only
    // measures its input while one is).
    bool watched() const noexcept { return watched_.load(std::memory_order_relaxed); }

    // ----- UI thread --------------------------------------------------------------------
    void setWatched(bool watched) noexcept { watched_.store(watched, std::memory_order_relaxed); }
    void setTapsEnabled(bool enabled) noexcept { tapsEnabled_.store(enabled, std::memory_order_relaxed); }
    bool hasTaps() const noexcept { return taps_ != nullptr; }
    double sampleRate() const noexcept { return sampleRate_.load(std::memory_order_relaxed); }
    // The project tempo the processor last played at (for a synced display).
    double tempoBpm() const noexcept { return tempoBpm_.load(std::memory_order_relaxed); }
    void setTempoBpm(double bpm) noexcept { tempoBpm_.store(bpm, std::memory_order_relaxed); }
    // Where the song was and whether it was playing, as of the processor's last
    // block (a tape machine's counter and reels).
    double transportSeconds() const noexcept { return transportSeconds_.load(std::memory_order_relaxed); }
    bool transportPlaying() const noexcept { return transportPlaying_.load(std::memory_order_relaxed); }
    void setTransport(double seconds, bool playing) noexcept
    {
        transportSeconds_.store(seconds, std::memory_order_relaxed);
        transportPlaying_.store(playing, std::memory_order_relaxed);
    }

    // Frames published since `cursor` (the newest `maxFrames` of them), oldest
    // first; advances the cursor. Start with cursor 0.
    int readHistory(std::uint64_t& cursor, Frame* out, int maxFrames) const noexcept
    {
        const auto end = written_.load(std::memory_order_acquire);
        constexpr std::uint64_t safe = historyLength - 16; // keep clear of the slot being written
        std::uint64_t start = std::max(cursor, end > safe ? end - safe : 0);
        if (end - start > static_cast<std::uint64_t>(std::max(0, maxFrames)))
            start = end - static_cast<std::uint64_t>(std::max(0, maxFrames));
        int n = 0;
        for (auto i = start; i < end; ++i)
            out[n++] = frameAt(i);
        cursor = end;
        return n;
    }

    // The newest frame (all zeros before the first).
    Frame latest() const noexcept
    {
        const auto end = written_.load(std::memory_order_acquire);
        return end == 0 ? Frame{} : frameAt(end - 1);
    }
    std::uint64_t framesWritten() const noexcept { return written_.load(std::memory_order_acquire); }

    // Samples written to a tap so far (to tell whether there is anything new).
    std::uint64_t tapPosition(int which) const noexcept
    {
        return which >= 0 && which < tapCount ? tapWritten_[which].load(std::memory_order_acquire) : 0;
    }
    // The newest `numSamples` (at most tapLength / 2) of a tap, oldest first;
    // false while fewer have been written.
    bool readTap(int which, float* out, int numSamples) const noexcept
    {
        if (taps_ == nullptr || which < 0 || which >= tapCount || numSamples > tapLength / 2)
            return false;
        const auto end = tapWritten_[which].load(std::memory_order_acquire);
        if (end < static_cast<std::uint64_t>(numSamples))
            return false;
        const auto* ring = taps_.get() + which * tapLength;
        auto at = end - static_cast<std::uint64_t>(numSamples);
        for (int i = 0; i < numSamples; ++i, ++at)
            out[i] = ring[at % tapLength].load(std::memory_order_relaxed);
        return true;
    }

private:
    void accumulate(float input, float output, float reductionDb, float extra, float extra2) noexcept
    {
        building_.input = std::max(building_.input, input);
        building_.output = std::max(building_.output, output);
        building_.reduction = std::max(building_.reduction, reductionDb);
        building_.extra = std::max(building_.extra, extra);
        building_.extra2 = std::max(building_.extra2, extra2);
    }

    void publish() noexcept
    {
        const auto w = written_.load(std::memory_order_relaxed);
        auto* slot = history_.get() + (w % historyLength) * valuesPerFrame;
        slot[0].store(building_.input, std::memory_order_relaxed);
        slot[1].store(building_.output, std::memory_order_relaxed);
        slot[2].store(building_.reduction, std::memory_order_relaxed);
        slot[3].store(building_.extra, std::memory_order_relaxed);
        slot[4].store(building_.extra2, std::memory_order_relaxed);
        written_.store(w + 1, std::memory_order_release);
        building_ = {};
        count_ = std::max(0, count_ - interval_);
    }

    Frame frameAt(std::uint64_t index) const noexcept
    {
        const auto* slot = history_.get() + (index % historyLength) * valuesPerFrame;
        return {slot[0].load(std::memory_order_relaxed),
                slot[1].load(std::memory_order_relaxed),
                slot[2].load(std::memory_order_relaxed),
                slot[3].load(std::memory_order_relaxed),
                slot[4].load(std::memory_order_relaxed)};
    }

    std::unique_ptr<std::atomic<float>[]> history_;
    std::unique_ptr<std::atomic<float>[]> taps_;
    std::atomic<std::uint64_t> written_{0};
    std::atomic<std::uint64_t> tapWritten_[tapCount] = {};
    std::atomic<double> sampleRate_{48000.0};
    std::atomic<double> tempoBpm_{120.0};
    std::atomic<double> transportSeconds_{0.0};
    std::atomic<bool> transportPlaying_{false};
    std::atomic<bool> tapsEnabled_{false};
    std::atomic<bool> watched_{false};
    // Real-time side only.
    Frame building_;
    int count_ = 0;
    int interval_ = 240;
};

} // namespace northlight::engine
