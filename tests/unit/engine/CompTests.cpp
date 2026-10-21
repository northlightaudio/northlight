#include "core/Comping.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

// The comp through the offline engine: what a take group plays is its comp
// clips, switching takes at the exact boundary sample, crossfading at constant
// sum around it, and reading each take's media at the right offset.

using namespace northlight;
using Catch::Approx;

namespace
{
const plugins::BuiltInFactory builtIns;

// "one.wav" is a constant 1.0, "half.wav" a constant 0.5 and "ramp.wav" counts
// its own sample index (in units of 1e-5), so a read offset shows.
class TakeLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string& path, double rate, std::string&) override
    {
        std::vector<float> samples(20000, path == "half.wav" ? 0.5f : 1.0f);
        if (path == "ramp.wav")
            for (size_t i = 0; i < samples.size(); ++i)
                samples[i] = static_cast<float>(i) * 1.0e-5f;
        return std::make_shared<engine::MemoryAudioSource>(std::vector<std::vector<float>>{samples}, rate);
    }
};

constexpr int blockSize = 64;
const float centre = std::cos(3.14159265f / 4.0f);

std::vector<float> render(const core::Project& p, int blocks)
{
    TakeLoader loader;
    engine::AudioEngine engine{loader, builtIns, 0};
    engine.prepare(48000.0, blockSize);
    std::vector<float> left(blockSize), right(blockSize);
    float* outputs[2] = {left.data(), right.data()};
    engine.setProject(p);
    engine.play();
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b)
    {
        engine.process(outputs, 2, blockSize);
        out.insert(out.end(), left.begin(), left.end());
    }
    return out;
}

core::AudioClip pass(const char* media, core::SamplePosition start, core::SamplePosition length)
{
    core::AudioClip c;
    c.mediaPath = media;
    c.start = start;
    c.length = length;
    return c;
}

// Two takes over [0, 2000): "one.wav" then `second`, the second playing from 1000 on.
core::Project twoTakeComp(const char* second, core::SamplePosition crossfade)
{
    core::Project p;
    p.sampleRate = 48000.0;
    p.nextId = 2;
    core::Track t;
    t.id = 1;
    p.tracks.push_back(t);
    core::UndoStack stack;
    stack.execute(p,
                  std::make_unique<core::RecordTakes>(
                      1, std::vector<core::AudioClip>{pass("one.wav", 0, 2000), pass(second, 0, 2000)}));
    auto& g = p.tracks[0].takeGroups.at(0);
    g.crossfade = crossfade;
    stack.execute(p, std::make_unique<core::SelectTake>(1, g.id, g.takes[0].id(), 0, 1000));
    return p;
}
} // namespace

TEST_CASE("A comp switches takes at the exact boundary sample without a crossfade", "[engine][comping]")
{
    const auto out = render(twoTakeComp("half.wav", 0), 40);
    for (int i = 0; i < 1000; ++i)
        REQUIRE(out[static_cast<size_t>(i)] == Approx(centre));
    for (int i = 1000; i < 2000; ++i)
        REQUIRE(out[static_cast<size_t>(i)] == Approx(0.5f * centre));
    CHECK(out[2000] == 0.0f);
}

TEST_CASE("A comp crossfades at constant sum around the boundary", "[engine][comping]")
{
    const auto out = render(twoTakeComp("half.wav", 100), 40);
    // The crossfade runs over [950, 1050): take 1 fades out as take 2 fades in.
    for (int i = 0; i < 950; ++i)
        REQUIRE(out[static_cast<size_t>(i)] == Approx(centre));
    for (int i = 950; i < 1050; ++i)
    {
        const float t = static_cast<float>(i - 950) / 100.0f;
        REQUIRE(out[static_cast<size_t>(i)] == Approx((1.0f - t + 0.5f * t) * centre).margin(1.0e-5));
    }
    for (int i = 1050; i < 2000; ++i)
        REQUIRE(out[static_cast<size_t>(i)] == Approx(0.5f * centre));

    // Two takes of the same level stay level through the crossfade.
    const auto level = render(twoTakeComp("one.wav", 100), 40);
    for (int i = 0; i < 2000; ++i)
        REQUIRE(level[static_cast<size_t>(i)] == Approx(centre).margin(1.0e-5));
}

TEST_CASE("A comp segment reads its take's media at the take's offset", "[engine][comping]")
{
    core::Project p;
    p.sampleRate = 48000.0;
    p.nextId = 2;
    core::Track t;
    t.id = 1;
    p.tracks.push_back(t);
    core::UndoStack stack;
    auto ramp = pass("ramp.wav", 500, 1500);
    ramp.sourceOffset = 300; // the take's clip starts 300 samples into its file
    stack.execute(
        p,
        std::make_unique<core::RecordTakes>(1, std::vector<core::AudioClip>{pass("one.wav", 0, 2000), ramp}));
    p.tracks[0].takeGroups.at(0).crossfade = 0;
    const auto groupId = p.tracks[0].takeGroups[0].id;
    const auto first = p.tracks[0].takeGroups[0].takes[0].id(),
               second = p.tracks[0].takeGroups[0].takes[1].id();
    // Take 1 everywhere but [1200, 1600).
    stack.execute(p, std::make_unique<core::SelectTake>(1, groupId, first));
    stack.execute(p, std::make_unique<core::SelectTake>(1, groupId, second, 1200, 1600));
    const auto out = render(p, 40);
    CHECK(out[1199] == Approx(centre));
    for (int i = 1200; i < 1600; ++i)
    {
        const float expected = static_cast<float>(i - 500 + 300) * 1.0e-5f * centre;
        REQUIRE(out[static_cast<size_t>(i)] == Approx(expected).margin(1.0e-6));
    }
    CHECK(out[1600] == Approx(centre));
}
