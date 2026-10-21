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

// "dc:<value>:<n>": a mono constant; "pair:<n>": stereo, left 0.5, right -0.25.
class ConstantLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource>
    load(const std::string& path, double rate, std::string& error) override
    {
        if (path.rfind("dc:", 0) == 0)
        {
            const auto second = path.find(':', 3);
            const float value = std::stof(path.substr(3, second - 3));
            const auto n = static_cast<size_t>(std::stoi(path.substr(second + 1)));
            return std::make_shared<engine::MemoryAudioSource>(
                std::vector<std::vector<float>>{std::vector<float>(n, value)}, rate);
        }
        if (path.rfind("pair:", 0) == 0)
        {
            const auto n = static_cast<size_t>(std::stoi(path.substr(5)));
            return std::make_shared<engine::MemoryAudioSource>(
                std::vector<std::vector<float>>{std::vector<float>(n, 0.5f), std::vector<float>(n, -0.25f)},
                rate);
        }
        error = "no such file";
        return nullptr;
    }
};

struct Rig
{
    ConstantLoader loader;
    engine::AudioEngine engine{loader, builtIns, 0};
    std::vector<float> left = std::vector<float>(256), right = std::vector<float>(256);
    Rig() { engine.prepare(48000.0, 256); }
    // Two blocks from the start (the second after any fader ramp); the left and
    // right 100 samples into the second.
    std::pair<float, float> play(const core::Project& p)
    {
        engine.setProject(p);
        engine.locate(0);
        engine.play();
        float* out[2] = {left.data(), right.data()};
        engine.process(out, 2, 256);
        engine.process(out, 2, 256);
        engine.stop();
        return {left[100], right[100]};
    }
};

core::Track clipTrack(core::Id id, const std::string& media)
{
    core::Track t;
    t.id = id;
    t.clips.push_back({id + 100, "clip", media, 0, 10000, 0, 1.0f});
    return t;
}
} // namespace

TEST_CASE("A folder's mute and solo act on the tracks it holds", "[engine][folder]")
{
    Rig rig;
    core::Project p;
    core::Track folder;
    folder.id = 1;
    folder.kind = core::TrackKind::folder;
    auto inside = clipTrack(2, "dc:0.5:10000");
    inside.folder = 1;
    auto outside = clipTrack(3, "dc:0.25:10000");
    p.tracks = {folder, inside, outside};
    p.nextId = 10;
    const float centre = std::cos(3.14159265f / 4.0f);

    // Both play.
    CHECK(rig.play(p).first == Approx((0.5f + 0.25f) * centre).margin(1e-4));
    // The folder muted: only the track outside it.
    p.tracks[0].mute = true;
    CHECK(rig.play(p).first == Approx(0.25f * centre).margin(1e-4));
    // The folder soloed: only the track inside it.
    p.tracks[0].mute = false;
    p.tracks[0].solo = true;
    CHECK(rig.play(p).first == Approx(0.5f * centre).margin(1e-4));
}

TEST_CASE("A frozen track plays its render instead of its clips and chain", "[engine][freeze]")
{
    Rig rig;
    core::Project p;
    auto track = clipTrack(1, "dc:0.1:10000");
    // A gain insert at +12 dB that the frozen track must not run again.
    core::Insert gain;
    gain.id = 50;
    gain.type = "northlight.gain";
    gain.params = {12.0f};
    track.inserts.push_back(gain);
    p.tracks = {track};
    p.nextId = 60;
    const auto live = rig.play(p);
    CHECK(std::abs(live.first) > 0.1f); // the clip through the gain

    p.tracks[0].frozenMedia = "pair:20000";
    const auto frozen = rig.play(p);
    // The stereo render at the centre balance (unity), the chain not applied again.
    CHECK(frozen.first == Approx(0.5f).margin(1e-4));
    CHECK(frozen.second == Approx(-0.25f).margin(1e-4));
    // The fader still applies.
    p.tracks[0].gain = 0.5f;
    CHECK(rig.play(p).first == Approx(0.25f).margin(1e-3));
}
