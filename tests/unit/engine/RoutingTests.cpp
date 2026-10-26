#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/Snapshot.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

// Routing beyond buses and the master (screens/mixer.md): hardware outputs, a
// send's pan and mute, solo safe, and reloading an insert's instance.

using namespace northlight;
using Catch::Approx;

namespace
{
const plugins::BuiltInFactory builtIns;

class ConstantLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double rate, std::string&) override
    {
        return std::make_shared<engine::MemoryAudioSource>(
            std::vector<std::vector<float>>{std::vector<float>(100000, 0.5f)}, rate);
    }
};

constexpr int blockSize = 64;
const float centre = std::cos(3.14159265f / 4.0f); // constant-power centre pan

// A four-output device: the master on 1/2, a second pair on 3/4.
struct Harness
{
    ConstantLoader loader;
    engine::AudioEngine engine{loader, builtIns, 0};
    std::vector<std::vector<float>> channels =
        std::vector<std::vector<float>>(4, std::vector<float>(blockSize));
    float* outputs[4] = {channels[0].data(), channels[1].data(), channels[2].data(), channels[3].data()};
    Harness() { engine.prepare(48000.0, blockSize); }
    void play(const core::Project& p, int pairs = 2)
    {
        engine.setOutputPairs(pairs);
        engine.setProject(p);
        engine.play();
        engine.process(outputs, pairs * 2, blockSize);
    }
};

core::Track trackWithClip(core::Id id)
{
    core::Track t;
    t.id = id;
    t.clips.push_back({id * 100, "c", "x.wav", 0, 100000, 0, 1.0f});
    return t;
}
} // namespace

TEST_CASE("A track on a hardware output plays there and not through the master", "[engine][mixer][routing]")
{
    core::Project p;
    p.tracks = {trackWithClip(1), trackWithClip(2)};
    p.tracks[1].hardwareOutput = 1; // outputs 3/4
    p.master.gain = 0.5f;           // the master's fader does not touch it
    Harness h;
    h.play(p);
    CHECK(h.channels[0][10] == Approx(0.5f * centre * 0.5f).margin(1e-6f));
    CHECK(h.channels[2][10] == Approx(0.5f * centre).margin(1e-6f));
    CHECK(h.channels[3][10] == Approx(0.5f * centre).margin(1e-6f));
}

TEST_CASE("A hardware output the device lacks falls back to the track's bus or the master",
          "[engine][mixer][routing]")
{
    core::Project p;
    p.tracks = {trackWithClip(1)};
    p.tracks[0].hardwareOutput = 3; // outputs 7/8: not on a four-output device
    Harness h;
    h.play(p);
    CHECK(h.channels[0][10] == Approx(0.5f * centre).margin(1e-6f));
    CHECK(h.channels[2][10] == 0.0f);
    // Offline (one pair, as an export renders) every hardware output mixes in.
    p.tracks[0].hardwareOutput = 1;
    Harness offline;
    offline.play(p, 1);
    CHECK(offline.channels[0][10] == Approx(0.5f * centre).margin(1e-6f));
}

TEST_CASE("A send's pan balances what reaches the bus and its mute silences it", "[engine][mixer][routing]")
{
    core::Project p;
    core::Bus fx;
    fx.id = 10;
    fx.gain = 1.0f;
    p.buses.push_back(fx);
    auto t = trackWithClip(1);
    t.gain = 0.0f;                             // only the send is heard
    t.sends.push_back({10, 1.0f, true, 1.0f}); // pre-fader, panned hard right
    p.tracks.push_back(t);

    SECTION("panned right: the left side of the send is gone")
    {
        Harness h;
        h.play(p);
        CHECK(h.channels[0][10] == Approx(0.0f).margin(1e-6f));
        CHECK(h.channels[1][10] == Approx(0.5f * centre).margin(1e-6f));
    }
    SECTION("muted: nothing reaches the bus, and the level is kept")
    {
        p.tracks[0].sends[0].mute = true;
        Harness h;
        h.play(p);
        CHECK(h.channels[0][10] == 0.0f);
        CHECK(h.channels[1][10] == 0.0f);
        CHECK(p.tracks[0].sends[0].level == 1.0f);
    }
}

TEST_CASE("A solo-safe channel stays heard while another is soloed", "[engine][mixer][routing]")
{
    core::Project p;
    p.tracks = {trackWithClip(1), trackWithClip(2), trackWithClip(3)};
    p.tracks[0].solo = true;
    p.tracks[1].soloSafe = true;
    Harness h;
    h.play(p);
    // The soloed one and the safe one; the third is silenced.
    CHECK(h.channels[0][10] == Approx(2.0f * 0.5f * centre).margin(1e-5f));
    SECTION("mute still wins over solo safe")
    {
        p.tracks[1].mute = true;
        Harness muted;
        muted.play(p);
        CHECK(muted.channels[0][10] == Approx(0.5f * centre).margin(1e-6f));
    }
}

TEST_CASE("Forgetting an insert makes a fresh instance from the model's state on the next compile",
          "[engine][mixer][routing]")
{
    core::Project p;
    auto t = trackWithClip(1);
    core::Insert gain;
    gain.id = 50;
    gain.type = plugins::types::gain;
    gain.params = plugins::defaultParameters(gain.type);
    t.inserts.push_back(gain);
    p.tracks.push_back(t);
    Harness h;
    h.play(p);
    const auto before = h.engine.insertInstance(50);
    REQUIRE(before != nullptr);
    h.engine.forgetInsert(50);
    CHECK(h.engine.insertInstance(50) == nullptr);
    h.engine.setProject(p);
    const auto after = h.engine.insertInstance(50);
    REQUIRE(after != nullptr);
    CHECK(after != before);
}
