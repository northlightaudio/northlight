#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/rt/RealtimeGuard.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

using namespace aural;
using Catch::Approx;

namespace
{

// A loader that serves synthetic "files": "ramp:N" is a mono ramp 0,1,2..N-1 (scaled
// by 1/N so it stays in range), "stereo:N" has left = +i/N and right = -i/N, and
// anything else fails.
class SyntheticLoader final : public engine::MediaLoader
{
public:
    int loads = 0;

    std::shared_ptr<engine::AudioSource>
    load(const std::string& path, double sampleRate, std::string& error) override
    {
        ++loads;
        const auto colon = path.find(':');
        if (colon == std::string::npos)
        {
            error = "no such file";
            return nullptr;
        }
        const auto kind = path.substr(0, colon);
        const auto n = static_cast<size_t>(std::stoi(path.substr(colon + 1)));
        std::vector<float> ramp(n);
        for (size_t i = 0; i < n; ++i)
            ramp[i] = static_cast<float>(i) / static_cast<float>(n);

        if (kind == "ramp")
            return std::make_shared<engine::MemoryAudioSource>(std::vector<std::vector<float>>{ramp},
                                                               sampleRate);
        if (kind == "stereo")
        {
            auto right = ramp;
            for (auto& s : right)
                s = -s;
            return std::make_shared<engine::MemoryAudioSource>(std::vector<std::vector<float>>{ramp, right},
                                                               sampleRate);
        }
        error = "unknown kind";
        return nullptr;
    }
};

constexpr int blockSize = 64;

struct Harness
{
    SyntheticLoader loader;
    engine::AudioEngine engine;
    std::vector<float> left = std::vector<float>(blockSize), right = std::vector<float>(blockSize);
    float* outputs[2] = {left.data(), right.data()};

    explicit Harness(int workers = 0) : engine(loader, workers) { engine.prepare(48000.0, blockSize); }

    // Renders one block and returns the timeline position it started at.
    core::SamplePosition block()
    {
        const auto start = engine.position();
        engine.process(outputs, 2, blockSize);
        return start;
    }

    // Renders blocks until the position reaches at least `until`, calling `check`
    // with (timelinePosition, left, right) for every sample.
    template <typename F> void renderUntil(core::SamplePosition until, F&& check)
    {
        while (engine.position() < until)
        {
            const auto start = block();
            for (int i = 0; i < blockSize; ++i)
                check(start + i, left[static_cast<size_t>(i)], right[static_cast<size_t>(i)]);
        }
    }
};

core::Project oneClipProject(const std::string& media,
                             core::SamplePosition start,
                             core::SamplePosition length,
                             core::SamplePosition sourceOffset = 0)
{
    core::Project p;
    core::Track t;
    t.id = 1;
    t.clips.push_back({2, "clip", media, start, length, sourceOffset, 1.0f});
    p.tracks.push_back(t);
    p.nextId = 3;
    return p;
}

} // namespace

TEST_CASE("Stopped transport renders silence and holds position", "[engine]")
{
    Harness h;
    h.engine.setProject(oneClipProject("ramp:1000", 0, 1000));
    h.left.assign(blockSize, 1.0f);
    h.block();
    for (float s : h.left)
        CHECK(s == 0.0f);
    CHECK(h.engine.position() == 0);
    CHECK_FALSE(h.engine.isPlaying());
}

TEST_CASE("A mono clip plays at its timeline position with constant-power centre pan", "[engine]")
{
    Harness h;
    // Clip of 500 samples at timeline 1000, starting 10 samples into the source.
    h.engine.setProject(oneClipProject("ramp:1000", 1000, 500, 10));
    h.engine.play();

    const float centre = std::cos(3.14159265f / 4.0f);
    h.renderUntil(1600,
                  [&](core::SamplePosition t, float l, float r)
                  {
                      if (t >= 1000 && t < 1500)
                      {
                          const float expected = static_cast<float>(t - 1000 + 10) / 1000.0f * centre;
                          CHECK(l == Approx(expected).margin(1e-6f));
                          CHECK(r == Approx(expected).margin(1e-6f));
                      }
                      else
                      {
                          CHECK(l == 0.0f);
                          CHECK(r == 0.0f);
                      }
                  });
    CHECK(h.engine.isPlaying());
}

TEST_CASE("Reads past the end of the source are silent", "[engine]")
{
    Harness h;
    // Clip claims 300 samples but the source only has 100 after the offset.
    h.engine.setProject(oneClipProject("ramp:200", 0, 300, 100));
    h.engine.play();
    h.renderUntil(320,
                  [&](core::SamplePosition t, float l, float)
                  {
                      if (t < 100)
                          CHECK((l > 0.0f || t == 0));
                      else
                          CHECK(l == 0.0f);
                  });
}

TEST_CASE("Track gain, clip gain, mute and solo are applied", "[engine]")
{
    Harness h;
    core::Project p;
    core::Track a;
    a.id = 1;
    a.gain = 0.5f;
    a.clips.push_back({10, "", "ramp:100", 0, 100, 0, 0.5f});
    core::Track b;
    b.id = 2;
    b.clips.push_back({11, "", "ramp:100", 0, 100, 0, 1.0f});
    p.tracks = {a, b};

    const float centre = std::cos(3.14159265f / 4.0f);
    const auto sampleAt = [&](core::SamplePosition t) { return static_cast<float>(t) / 100.0f * centre; };

    SECTION("both tracks sum")
    {
        h.engine.setProject(p);
        h.engine.play();
        h.block();
        CHECK(h.left[50] == Approx(sampleAt(50) * 0.25f + sampleAt(50)).margin(1e-6f));
    }
    SECTION("mute removes a track")
    {
        p.tracks[1].mute = true;
        h.engine.setProject(p);
        h.engine.play();
        h.block();
        CHECK(h.left[50] == Approx(sampleAt(50) * 0.25f).margin(1e-6f));
    }
    SECTION("solo keeps only soloed tracks")
    {
        p.tracks[1].solo = true;
        h.engine.setProject(p);
        h.engine.play();
        h.block();
        CHECK(h.left[50] == Approx(sampleAt(50)).margin(1e-6f));
    }
    SECTION("a muted soloed track is silent")
    {
        p.tracks[1].solo = true;
        p.tracks[1].mute = true;
        h.engine.setProject(p);
        h.engine.play();
        h.block();
        CHECK(h.left[50] == 0.0f);
    }
}

TEST_CASE("Stereo sources keep their channels and pan as balance", "[engine]")
{
    Harness h;
    auto p = oneClipProject("stereo:100", 0, 100);
    p.tracks[0].pan = 0.5f; // right: left channel attenuated to 0.5, right untouched
    h.engine.setProject(p);
    h.engine.play();
    h.block();
    CHECK(h.left[40] == Approx(0.40f * 0.5f).margin(1e-6f));
    CHECK(h.right[40] == Approx(-0.40f).margin(1e-6f));
}

TEST_CASE("Pan laws", "[engine]")
{
    float l, r;
    engine::Renderer::monoPanGains(0.0f, l, r);
    CHECK(l == Approx(r));
    CHECK(l * l + r * r == Approx(1.0f));
    engine::Renderer::monoPanGains(-1.0f, l, r);
    CHECK(l == Approx(1.0f));
    CHECK(r == Approx(0.0f).margin(1e-6f));
    engine::Renderer::stereoPanGains(0.0f, l, r);
    CHECK(l == 1.0f);
    CHECK(r == 1.0f);
    engine::Renderer::stereoPanGains(-0.25f, l, r);
    CHECK(l == 1.0f);
    CHECK(r == Approx(0.75f));
}

TEST_CASE("Locate jumps the position and loop wraps sample-accurately", "[engine]")
{
    Harness h;
    h.engine.setProject(oneClipProject("ramp:1000", 0, 1000));
    h.engine.locate(500);
    h.block();                         // applies the command; this block renders from 500
    CHECK(h.engine.position() == 500); // stopped: no advance

    h.engine.setLoop(100, 228, true); // 128 samples: two blocks exactly
    h.engine.locate(90);
    h.engine.play();

    // Block 1: 90..154 (runs into the loop normally). The locate is applied inside
    // this block, so the position is checked after it and the content at its start.
    h.block();
    CHECK(h.engine.position() == 154);
    CHECK(h.left[0] == Approx(0.090f * std::cos(3.14159265f / 4.0f)).margin(1e-6f));

    // Block 2: 154..218.
    h.block();
    CHECK(h.engine.position() == 218);

    // Block 3: 218..228 then wraps to 100 and plays 100..154.
    h.block();
    CHECK(h.engine.position() == 154);
    const float centre = std::cos(3.14159265f / 4.0f);
    CHECK(h.left[9] == Approx(0.227f * centre).margin(1e-6f));
    CHECK(h.left[10] == Approx(0.100f * centre).margin(1e-6f));
    CHECK(h.left[63] == Approx(0.153f * centre).margin(1e-6f));

    // Disabling the loop lets it run through.
    h.engine.setLoop(100, 228, false);
    h.block();
    h.block();
    h.block();
    CHECK(h.engine.position() == 154 + 3 * 64);
}

TEST_CASE("A loop just longer than a block wraps mid-block", "[engine]")
{
    Harness h;
    h.engine.setProject(oneClipProject("ramp:1000", 0, 1000));
    h.engine.setLoop(10, 80, true); // 70 samples
    h.engine.locate(10);
    h.engine.play();
    h.block(); // 10..74
    h.block(); // 74..80 then wraps: 10..68
    CHECK(h.engine.position() == 68);
    const float centre = std::cos(3.14159265f / 4.0f);
    for (int i = 0; i < 6; ++i)
        CHECK(h.left[static_cast<size_t>(i)] == Approx((74 + i) / 1000.0f * centre).margin(1e-6f));
    for (int i = 6; i < 64; ++i)
        CHECK(h.left[static_cast<size_t>(i)] == Approx((10 + i - 6) / 1000.0f * centre).margin(1e-6f));
}

TEST_CASE("Loops shorter than the minimum or with bad bounds are refused", "[engine]")
{
    Harness h;
    h.engine.setProject(oneClipProject("ramp:1000", 0, 1000));
    h.engine.setLoop(10, 20, true); // too short: ignored
    h.engine.locate(0);
    h.engine.play();
    h.block();
    h.block();
    CHECK(h.engine.position() == 128);

    h.engine.setLoop(-500, 300, true); // negative start clamps to 0
    h.block();
    h.block();
    h.block();
    CHECK(h.engine.position() == (128 + 3 * 64) % 300);

    h.engine.setLoop(400, 100, true); // inverted: ignored
    h.engine.locate(1000);
    h.block();
    CHECK(h.engine.position() == 1064);
}

TEST_CASE("Missing media leaves the clip silent and is reported", "[engine]")
{
    Harness h;
    h.engine.setProject(oneClipProject("missing.wav", 0, 100));
    REQUIRE(h.engine.mediaErrors().contains("missing.wav"));
    h.engine.play();
    h.block();
    for (float s : h.left)
        CHECK(s == 0.0f);
}

TEST_CASE("Media is cached across republishes and pruned when unused", "[engine]")
{
    Harness h;
    auto p = oneClipProject("ramp:100", 0, 100);
    h.engine.setProject(p);
    CHECK(h.loader.loads == 1);
    p.tracks[0].gain = 0.5f;
    h.engine.setProject(p);
    CHECK(h.loader.loads == 1); // cached

    p.tracks[0].clips[0].mediaPath = "ramp:50";
    h.engine.setProject(p);
    CHECK(h.loader.loads == 2);
    h.block();                 // RT moves to the latest snapshot
    h.engine.collectGarbage(); // old snapshots freed, "ramp:100" now unreferenced and pruned

    p.tracks[0].clips[0].mediaPath = "ramp:100";
    h.engine.setProject(p);
    CHECK(h.loader.loads == 3); // reloaded after pruning
}

TEST_CASE("Parallel rendering is bit-identical to serial rendering", "[engine][pool]")
{
    core::Project p;
    for (core::Id i = 1; i <= 24; ++i)
    {
        core::Track t;
        t.id = i;
        t.gain = 0.5f + static_cast<float>(i) / 48.0f;
        t.pan = static_cast<float>(i % 5) / 2.0f - 1.0f;
        t.clips.push_back({100 + i,
                           "",
                           i % 2 ? "ramp:3000" : "stereo:3000",
                           static_cast<core::SamplePosition>(i * 7),
                           3000,
                           0,
                           0.8f});
        t.clips.push_back({200 + i, "", "ramp:3000", 4000, 3000, 5, 1.0f});
        p.tracks.push_back(t);
    }
    p.tracks[3].mute = true;

    Harness serial(0), parallel(3);
    CHECK(parallel.engine.workerThreads() == 3);
    serial.engine.setProject(p);
    parallel.engine.setProject(p);
    serial.engine.setLoop(0, 7100, true);
    parallel.engine.setLoop(0, 7100, true);
    serial.engine.play();
    parallel.engine.play();

    bool identical = true;
    for (int block = 0; block < 300; ++block)
    {
        serial.block();
        parallel.block();
        for (size_t i = 0; i < blockSize; ++i)
            identical =
                identical && serial.left[i] == parallel.left[i] && serial.right[i] == parallel.right[i];
    }
    CHECK(identical);
    CHECK(serial.engine.position() == parallel.engine.position());
}

TEST_CASE("Device blocks larger than the renderer maximum are rendered in pieces", "[engine]")
{
    SyntheticLoader loader;
    engine::AudioEngine engine(loader, 0);
    engine.prepare(48000.0, 20000); // capped at Snapshot::maxBlockSamples internally
    engine.setProject(oneClipProject("ramp:1000", 0, 1000));
    engine.play();

    std::vector<float> left(20000), right(20000);
    float* outs[] = {left.data(), right.data()};
    engine.process(outs, 2, 20000);
    CHECK(engine.position() == 20000);
    const float centre = std::cos(3.14159265f / 4.0f);
    CHECK(left[500] == Approx(0.5f * centre).margin(1e-6f));
    CHECK(left[8192 + 10] == 0.0f);
}

#if defined(AURAL_RT_CHECKS)
namespace
{
std::atomic<int> engineViolations{0};
void countEngineViolation(const rt::Violation&) noexcept
{
    engineViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Processing under the real-time guard does not allocate, even across snapshot swaps",
          "[engine][rt]")
{
    Harness h(2);
    core::Project p;
    for (core::Id i = 1; i <= 32; ++i)
    {
        core::Track t;
        t.id = i;
        t.pan = static_cast<float>(i % 3) - 1.0f;
        t.clips.push_back({100 + i, "", i % 2 ? "ramp:4000" : "stereo:4000", 0, 4000, 0, 1.0f});
        t.clips.push_back({200 + i, "", "ramp:4000", 5000, 4000, 0, 1.0f});
        p.tracks.push_back(t);
    }
    h.engine.setProject(p);
    h.engine.setLoop(0, 9000, true);
    h.engine.play();

    const auto previous = rt::setViolationHandler(&countEngineViolation);
    engineViolations = 0;
    for (int block = 0; block < 400; ++block)
    {
        if (block % 50 == 25)
        {
            p.tracks[0].gain = 0.9f;
            h.engine.setProject(p); // controller thread, allowed to allocate
            h.engine.collectGarbage();
        }
        rt::ScopedRealtimeSection section;
        h.engine.process(h.outputs, 2, blockSize);
    }
    rt::setViolationHandler(previous);
    CHECK(engineViolations == 0);
    CHECK(h.engine.droppedCommands() == 0);
}
#endif
