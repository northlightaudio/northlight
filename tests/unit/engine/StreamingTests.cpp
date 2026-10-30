#include "engine/DiskThread.h"
#include "engine/StreamingAudioSource.h"
#include "engine/rt/RealtimeGuard.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace northlight::engine;

namespace
{
// A "file" whose sample i on channel c is (i + c * 1000000). Reads are counted.
class RampReader final : public StreamReader
{
public:
    RampReader(std::int64_t length, int channels) : length_(length), channels_(channels) {}
    int numChannels() const noexcept override { return channels_; }
    std::int64_t lengthSamples() const noexcept override { return length_; }
    double sampleRate() const noexcept override { return 48000.0; }
    bool read(std::int64_t start, int numSamples, float* const* dest) override
    {
        ++reads;
        for (int c = 0; c < channels_; ++c)
            for (int i = 0; i < numSamples; ++i)
            {
                const auto s = start + i;
                dest[c][i] = s >= 0 && s < length_ ? static_cast<float>(s + c * 1000000) : 0.0f;
            }
        return true;
    }
    std::atomic<int> reads{0};

private:
    std::int64_t length_;
    int channels_;
};

float expected(std::int64_t sample, int channel)
{
    return static_cast<float>(sample + channel * 1000000);
}

StreamingAudioSource::Options smallOptions()
{
    StreamingAudioSource::Options o;
    o.ringSamples = 4096;
    o.readAheadSamples = 2048;
    o.chunkSamples = 1024;
    o.guardSamples = 256;
    o.seekGraceMilliseconds = 1;
    return o;
}
} // namespace

TEST_CASE("Streaming source serves sequential reads exactly once the window is filled", "[engine][stream]")
{
    auto reader = std::make_unique<RampReader>(100000, 2);
    StreamingAudioSource source(std::move(reader), smallOptions());
    source.prefetch(0);
    while (source.service())
    {
    }
    CHECK(source.windowStart() == 0);
    CHECK(source.windowEnd() >= 2048);

    std::vector<float> left(64), right(64);
    std::int64_t position = 0;
    for (int block = 0; block < 500; ++block)
    {
        source.read(0, position, left.data(), 64);
        source.read(1, position, right.data(), 64);
        for (int i = 0; i < 64; ++i)
        {
            REQUIRE(left[static_cast<size_t>(i)] == expected(position + i, 0));
            REQUIRE(right[static_cast<size_t>(i)] == expected(position + i, 1));
        }
        position += 64;
        while (source.service())
        {
        }
    }
    CHECK(source.underruns() == 0);
    CHECK_FALSE(source.hadReadError());
}

TEST_CASE("Reads outside the window are silent underruns and a seek rebuilds the window", "[engine][stream]")
{
    StreamingAudioSource source(std::make_unique<RampReader>(100000, 1), smallOptions());
    source.prefetch(0);
    while (source.service())
    {
    }

    std::vector<float> buffer(64, 1.0f);
    source.read(0, 50000, buffer.data(), 64); // far outside
    CHECK(source.underruns() == 1);
    for (float s : buffer)
        CHECK(s == 0.0f);

    while (source.service())
    {
    }
    CHECK(source.windowStart() == 50000);
    source.read(0, 50000, buffer.data(), 64);
    CHECK(source.underruns() == 1);
    CHECK(buffer[0] == expected(50000, 0));
    CHECK(buffer[63] == expected(50063, 0));
}

TEST_CASE("Running off a caught-up window counts one starvation; a locate never does", "[engine][stream]")
{
    StreamingAudioSource source(std::make_unique<RampReader>(100000, 1), smallOptions());
    source.prefetch(0);
    while (source.service())
    {
    }
    source.service(); // sees the window full: caught up

    std::vector<float> buffer(64, 1.0f);

    // A locate lands outside the window: an underrun, not a starvation.
    source.read(0, 50000, buffer.data(), 64);
    CHECK(source.underruns() == 1);
    CHECK(source.starvations() == 0);

    // Straight after the rebuild starts the window has not caught up yet, so
    // running off its end is still the locate's silence.
    source.service();
    REQUIRE(source.windowStart() == 50000);
    source.read(0, source.windowEnd() - 10, buffer.data(), 64);
    CHECK(source.underruns() == 2);
    CHECK(source.starvations() == 0);

    // Once caught up, running off the end is the disk falling behind: counted once
    // per block (channel 0 only) and once per gap (the next block is a rebuild).
    while (source.service())
    {
    }
    source.service();
    const auto end = source.windowEnd();
    source.read(0, end - 10, buffer.data(), 64);
    source.read(1, end - 10, buffer.data(), 64);
    CHECK(source.starvations() == 1);
    source.read(0, end + 54, buffer.data(), 64);
    CHECK(source.starvations() == 1);
    CHECK(source.underruns() == 5);
}

namespace
{
// A RampReader whose reads wait while the gate is closed: a disk that has stalled.
class GatedReader final : public StreamReader
{
public:
    explicit GatedReader(std::int64_t length) : ramp_(length, 1) {}
    int numChannels() const noexcept override { return 1; }
    std::int64_t lengthSamples() const noexcept override { return ramp_.lengthSamples(); }
    double sampleRate() const noexcept override { return 48000.0; }
    bool read(std::int64_t start, int numSamples, float* const* dest) override
    {
        while (!open.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return ramp_.read(start, numSamples, dest);
    }
    std::atomic<bool> open{true};

private:
    RampReader ramp_;
};
} // namespace

TEST_CASE("The disk thread's starvation total outlives the source that starved", "[engine][stream]")
{
    auto reader = std::make_unique<GatedReader>(48000 * 20);
    auto* gate = reader.get();
    auto source = std::make_shared<StreamingAudioSource>(std::move(reader), smallOptions());
    DiskThread disk(1);
    disk.add(source);
    source->prefetch(0);
    disk.poke();

    for (int i = 0; i < 500 && source->windowEnd() < 2048; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(source->windowEnd() == 2048);
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); // a few idle passes: caught up

    // Stall the disk, then play off the end of the window.
    gate->open = false;
    const auto end = source->windowEnd();
    std::vector<float> buffer(64);
    source->read(0, end - 10, buffer.data(), 64);
    CHECK(source->starvations() == 1);
    CHECK(disk.totalStarvations() == 1);

    // Let the disk catch up far enough that it has made a full pass since the gap,
    // then drop the source.
    gate->open = true;
    for (int i = 0; i < 500 && source->windowEnd() < end + 2048; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(source->windowEnd() >= end + 2048);
    source.reset();
    disk.poke();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    CHECK(disk.totalStarvations() == 1);
    CHECK(disk.totalUnderruns() == 0); // underruns are only summed over live sources
}

TEST_CASE("Reads straddling the file edges zero-fill without counting underruns", "[engine][stream]")
{
    StreamingAudioSource source(std::make_unique<RampReader>(1000, 1), smallOptions());
    source.prefetch(0);
    while (source.service())
    {
    }
    CHECK(source.windowEnd() == 1000);

    std::vector<float> buffer(64, 1.0f);
    source.read(0, -10, buffer.data(), 64);
    CHECK(buffer[9] == 0.0f);
    CHECK(buffer[10] == expected(0, 0));
    source.read(0, 980, buffer.data(), 64);
    CHECK(buffer[19] == expected(999, 0));
    CHECK(buffer[20] == 0.0f);
    source.read(0, 5000, buffer.data(), 64);
    CHECK(buffer[0] == 0.0f);
    CHECK(source.underruns() == 0);
}

// [paced]: reads in real time and needs the disk thread to get a core when it wakes,
// so CI runs it on its own (docs/building.md, "Tests").
TEST_CASE("The disk thread keeps a paced reader fed with no underruns after the first fill",
          "[engine][stream][paced]")
{
    auto reader = std::make_unique<RampReader>(48000 * 20, 2);
    auto* rawReader = reader.get();
    auto source = std::make_shared<StreamingAudioSource>(std::move(reader), smallOptions());
    DiskThread disk(1);
    disk.add(source);
    source->prefetch(0);
    disk.poke();

    // Wait for the initial window.
    for (int i = 0; i < 200 && source->windowEnd() < 2048; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(source->windowEnd() >= 2048);

    // Read at a realistic pace: 64 samples every ~1.3 ms, for one second of audio,
    // checking every sample.
    std::vector<float> left(64), right(64);
    std::int64_t position = 0;
    bool allCorrect = true;
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < 750; ++block)
    {
        source->read(0, position, left.data(), 64);
        source->read(1, position, right.data(), 64);
        for (int i = 0; i < 64; ++i)
            allCorrect = allCorrect && left[static_cast<size_t>(i)] == expected(position + i, 0) &&
                         right[static_cast<size_t>(i)] == expected(position + i, 1);
        position += 64;
        std::this_thread::sleep_until(start + std::chrono::microseconds(1333) * (block + 1));
    }
    CHECK(allCorrect);
    CHECK(source->underruns() == 0);
    CHECK(disk.totalUnderruns() == 0);
    CHECK(disk.totalStarvations() == 0);
    CHECK(rawReader->reads > 10);

    // A locate far ahead: the window is rebuilt within a few service rounds.
    source->prefetch(48000 * 10);
    disk.poke();
    for (int i = 0; i < 500 && source->windowStart() != 48000 * 10; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(source->windowStart() == 48000 * 10);
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> streamViolations{0};
void countStreamViolation(const northlight::rt::Violation&) noexcept
{
    streamViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Streaming reads do not allocate on a real-time thread", "[engine][stream][rt]")
{
    StreamingAudioSource source(std::make_unique<RampReader>(100000, 2), smallOptions());
    source.prefetch(0);
    while (source.service())
    {
    }
    std::vector<float> buffer(64);
    const auto previous = northlight::rt::setViolationHandler(&countStreamViolation);
    streamViolations = 0;
    {
        northlight::rt::ScopedRealtimeSection section;
        source.read(0, 0, buffer.data(), 64);
        source.read(1, 4000, buffer.data(), 64);  // wraps the ring
        source.read(0, 90000, buffer.data(), 64); // underrun path
    }
    northlight::rt::setViolationHandler(previous);
    CHECK(streamViolations == 0);
}
#endif
