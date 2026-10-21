#include "engine/AudioSource.h"
#include "engine/DiskThread.h"
#include "engine/Snapshot.h"
#include "engine/StreamingAudioSource.h"
#include "engine/WorkerPool.h"
#include "engine/rt/SnapshotPublisher.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

using namespace northlight;

namespace
{
struct Counters
{
    std::vector<std::atomic<int>> hits;
    explicit Counters(int n) : hits(static_cast<size_t>(n)) {}
};

void countJob(void* context, int index) noexcept
{
    auto& c = *static_cast<Counters*>(context);
    c.hits[static_cast<size_t>(index)].fetch_add(1, std::memory_order_relaxed);
    volatile int sink = 0;
    for (int i = 0; i < (index % 7) * 300; ++i)
        sink = sink + i;
}
} // namespace

TEST_CASE("Worker pool batches of changing size never run a job twice or against a stale batch",
          "[engine][pool][stress]")
{
    engine::WorkerPool pool(3);
    for (int round = 0; round < 3000; ++round)
    {
        const int count = 1 + (round * 7919) % 70;
        Counters counters(count);
        pool.run(count, &countJob, &counters);
        for (const auto& h : counters.hits)
            REQUIRE(h.load() == 1);
        // `counters` dies here; a straggler running against it would crash or
        // corrupt the next round's counts.
    }
}

TEST_CASE("Snapshot publisher survives concurrent publish, collect and acquire", "[rt][snapshot][stress]")
{
    struct Snap
    {
        std::uint64_t epoch = 0;
        int value = 0;
        int check = 0;
    };
    rt::SnapshotPublisher<Snap> pub;
    std::atomic<bool> stop{false};
    std::atomic<int> corrupt{0};
    std::atomic<std::uint64_t> reads{0};

    std::thread rt(
        [&]
        {
            while (!stop.load(std::memory_order_acquire))
            {
                if (const Snap* s = pub.acquire())
                {
                    // Use the snapshot for a little while, as a block would.
                    volatile int sink = 0;
                    for (int i = 0; i < 200; ++i)
                        sink = sink + s->value;
                    if (s->check != s->value * 7 + 1)
                        corrupt.fetch_add(1);
                }
                reads.fetch_add(1, std::memory_order_relaxed);
            }
        });

    for (int i = 0; i < 20000; ++i)
    {
        auto s = std::make_unique<Snap>();
        s->value = i;
        s->check = i * 7 + 1;
        pub.publish(std::move(s));
        pub.collect(true);
    }
    stop.store(true, std::memory_order_release);
    rt.join();
    pub.collect(false);
    CHECK(corrupt == 0);
    CHECK(reads > 0);
    CHECK(pub.retiredCount() == 0);
}

namespace
{
class RampReader final : public engine::StreamReader
{
public:
    explicit RampReader(std::int64_t length) : length_(length) {}
    int numChannels() const noexcept override { return 1; }
    std::int64_t lengthSamples() const noexcept override { return length_; }
    double sampleRate() const noexcept override { return 48000.0; }
    bool read(std::int64_t start, int numSamples, float* const* dest) override
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto s = start + i;
            dest[0][i] = s >= 0 && s < length_ ? static_cast<float>(s) : 0.0f;
        }
        return true;
    }

private:
    std::int64_t length_;
};

engine::StreamingAudioSource::Options smallOptions()
{
    engine::StreamingAudioSource::Options o;
    o.ringSamples = 4096;
    o.readAheadSamples = 2048;
    o.chunkSamples = 1024;
    o.guardSamples = 256;
    o.seekGraceMilliseconds = 1;
    return o;
}

// A loader that streams one synthetic file with per-clip instances.
class StreamingLoader final : public engine::MediaLoader
{
public:
    engine::DiskThread disk{1};
    std::vector<std::shared_ptr<engine::StreamingAudioSource>> instances;

    std::shared_ptr<engine::AudioSource> load(const std::string&, double, std::string&) override
    {
        auto source = std::make_shared<engine::StreamingAudioSource>(std::make_unique<RampReader>(200000),
                                                                     smallOptions());
        source->setInstanceFactory([] { return std::make_unique<RampReader>(200000); },
                                   [this](const std::shared_ptr<engine::StreamingAudioSource>& instance)
                                   {
                                       instances.push_back(instance);
                                       disk.add(instance);
                                       disk.poke();
                                   });
        return source;
    }
};
} // namespace

TEST_CASE("Two clips of one streamed file get their own windows and play without underruns",
          "[engine][stream][stress]")
{
    StreamingLoader loader;
    engine::MediaCache media(loader);
    const plugins::BuiltInFactory builtIns;
    engine::InsertCache insertCache(builtIns);
    core::Project p;
    core::Track t;
    t.id = 1;
    t.clips.push_back({10, "a", "big.wav", 0, 100000, 0, 1.0f});      // reads source 0..
    t.clips.push_back({11, "b", "big.wav", 0, 100000, 150000, 1.0f}); // reads source 150000..
    p.tracks.push_back(t);

    auto snapshot = engine::compileSnapshot(p, media, insertCache);
    REQUIRE(loader.instances.size() == 2);
    CHECK(snapshot->tracks[0].clips[0].source != snapshot->tracks[0].clips[1].source);

    // A recompile keeps the same instances (windows survive edits).
    auto again = engine::compileSnapshot(p, media, insertCache);
    CHECK(loader.instances.size() == 2);
    CHECK(again->tracks[0].clips[0].source == snapshot->tracks[0].clips[0].source);

    // Let the windows fill from each clip's start, then read both in lock step.
    loader.instances[0]->prefetch(0);
    loader.instances[1]->prefetch(150000);
    loader.disk.poke();
    for (int i = 0;
         i < 500 && (loader.instances[0]->windowEnd() < 2048 || loader.instances[1]->windowEnd() < 152048);
         ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    std::vector<float> a(64), b(64);
    bool correct = true;
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < 400; ++block)
    {
        const std::int64_t pos = block * 64;
        loader.instances[0]->read(0, pos, a.data(), 64);
        loader.instances[1]->read(0, 150000 + pos, b.data(), 64);
        for (int i = 0; i < 64; ++i)
            correct = correct && a[static_cast<size_t>(i)] == static_cast<float>(pos + i) &&
                      b[static_cast<size_t>(i)] == static_cast<float>(150000 + pos + i);
        std::this_thread::sleep_until(start + std::chrono::microseconds(1333) * (block + 1));
    }
    CHECK(correct);
    CHECK(loader.instances[0]->underruns() == 0);
    CHECK(loader.instances[1]->underruns() == 0);
}

TEST_CASE("A loop wrap across a streamed window yields silence or the right samples, never garbage",
          "[engine][stream][stress]")
{
    auto source =
        std::make_shared<engine::StreamingAudioSource>(std::make_unique<RampReader>(200000), smallOptions());
    engine::DiskThread disk(1);
    disk.add(source);
    source->prefetch(0);
    disk.poke();
    for (int i = 0; i < 500 && source->windowEnd() < 2048; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Loop 0..10000 (longer than the 4096 window): every wrap forces a seek.
    std::vector<float> buffer(64);
    bool clean = true;
    int underrunsSeen = 0;
    std::int64_t pos = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < 1500; ++block)
    {
        source->read(0, pos, buffer.data(), 64);
        bool allZero = true;
        for (int i = 0; i < 64; ++i)
            allZero = allZero && buffer[static_cast<size_t>(i)] == 0.0f;
        if (allZero)
            ++underrunsSeen;
        else
            for (int i = 0; i < 64; ++i)
                clean = clean && buffer[static_cast<size_t>(i)] == static_cast<float>(pos + i);
        pos += 64;
        if (pos >= 10000)
        {
            pos = 0;
            disk.poke();
        }
        std::this_thread::sleep_until(start + std::chrono::microseconds(1333) * (block + 1));
    }
    CHECK(clean);
    CHECK(underrunsSeen < 1500); // some silence at the wraps is expected, not all
}
