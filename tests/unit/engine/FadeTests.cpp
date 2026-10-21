#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace northlight;
using Catch::Approx;

namespace
{
const plugins::BuiltInFactory builtIns;

// Every file is a mono constant of 1.0.
class OnesLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double rate, std::string&) override
    {
        return std::make_shared<engine::MemoryAudioSource>(
            std::vector<std::vector<float>>{std::vector<float>(20000, 1.0f)}, rate);
    }
};

constexpr int blockSize = 64;
const float centre = std::cos(3.14159265f / 4.0f);

struct Harness
{
    OnesLoader loader;
    engine::AudioEngine engine{loader, builtIns, 0};
    std::vector<float> left = std::vector<float>(blockSize), right = std::vector<float>(blockSize);
    float* outputs[2] = {left.data(), right.data()};
    Harness() { engine.prepare(48000.0, blockSize); }
    // Renders [0, blocks * blockSize) and returns the left channel.
    std::vector<float> render(const core::Project& p, int blocks)
    {
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
};
} // namespace

TEST_CASE("A clip's fades ramp linearly over their lengths", "[engine][fades]")
{
    Harness h;
    core::Project p;
    core::Track t;
    t.id = 1;
    core::AudioClip clip{10, "c", "ones.wav", 0, 200, 0, 1.0f};
    clip.fadeIn = 100;
    clip.fadeOut = 50;
    t.clips.push_back(clip);
    p.tracks.push_back(t);
    const auto out = h.render(p, 4); // 256 samples
    CHECK(out[0] == Approx(0.0f));
    CHECK(out[50] == Approx(0.5f * centre));
    CHECK(out[100] == Approx(centre));
    CHECK(out[149] == Approx(centre));
    CHECK(out[175] == Approx(0.5f * centre).margin(0.02f)); // halfway through the fade-out
    CHECK(out[199] == Approx(0.02f * centre).margin(0.02f));
    CHECK(out[200] == Approx(0.0f)); // past the clip
}

TEST_CASE("Overlapping clips with matching fades crossfade at a constant sum", "[engine][fades]")
{
    Harness h;
    core::Project p;
    core::Track t;
    t.id = 1;
    core::AudioClip a{10, "a", "ones.wav", 0, 150, 0, 1.0f};
    core::AudioClip b{11, "b", "ones.wav", 100, 150, 0, 1.0f};
    a.fadeOut = 50;
    b.fadeIn = 50;
    t.clips.push_back(a);
    t.clips.push_back(b);
    p.tracks.push_back(t);
    const auto out = h.render(p, 4);
    for (int i = 0; i < 250; ++i)
        CHECK(out[static_cast<size_t>(i)] == Approx(centre).margin(0.03f));
    CHECK(out[250] == Approx(0.0f));
}
