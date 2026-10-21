#include "core/Command.h"
#include "core/ProjectJson.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "plugins/BuiltIns.h"
#include "plugins/FxTracks.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace northlight;
using Catch::Approx;

namespace
{
const plugins::BuiltInFactory builtIns;

// One file: a single sample of 1 at the start, then silence.
class ImpulseLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double rate, std::string&) override
    {
        std::vector<float> samples(48000, 0.0f);
        samples[0] = 1.0f;
        return std::make_shared<engine::MemoryAudioSource>(std::vector<std::vector<float>>{samples}, rate);
    }
};
} // namespace

TEST_CASE("An FX track is a bus with its effect fully wet, added with sends in one step", "[core][fx]")
{
    core::Project p;
    core::Track a, b;
    a.id = p.allocateId();
    b.id = p.allocateId();
    p.tracks = {a, b};
    core::UndoStack undo;
    auto commands = plugins::addFxTrackCommands(p, plugins::types::reverb, "Reverb", {a.id, b.id});
    REQUIRE(commands.size() == 3);
    undo.beginGroup("Add FX track");
    for (auto& c : commands)
        undo.execute(p, std::move(c));
    undo.endGroup();
    REQUIRE(p.buses.size() == 1);
    const auto& fx = p.buses[0];
    CHECK(fx.fx);
    CHECK(fx.name == "Reverb");
    REQUIRE(fx.inserts.size() == 1);
    CHECK(fx.inserts[0].type == plugins::types::reverb);
    CHECK(fx.inserts[0].params[plugins::reverb::mix] == 100.0f);
    CHECK(fx.inserts[0].id != core::invalidId);
    REQUIRE(p.tracks[0].findSend(fx.id) != nullptr);
    CHECK(p.tracks[0].findSend(fx.id)->level == Approx(0.5f));
    CHECK_FALSE(p.tracks[0].findSend(fx.id)->preFader);
    CHECK(p.tracks[1].findSend(fx.id) != nullptr);
    // A second one is numbered; the flag survives a save.
    CHECK(plugins::makeFxTrack(p, plugins::types::reverb, "Reverb").name == "Reverb 2");
    CHECK(plugins::makeFxTrack(p, {}, "Empty").inserts.empty());
    const auto loaded = core::projectFromJson(core::projectToJson(p));
    REQUIRE(loaded.has_value());
    CHECK(loaded->buses[0].fx);
    // One undo removes the track and its sends.
    undo.undo(p);
    CHECK(p.buses.empty());
    CHECK(p.tracks[0].sends.empty());
}

TEST_CASE("A track's send reaches the FX track's delay; its direct output stays dry", "[engine][fx]")
{
    core::Project p;
    core::Track t;
    t.id = 1;
    t.clips.push_back({100, "c", "x.wav", 0, 48000, 0, 1.0f});
    p.tracks.push_back(t);
    p.nextId = 200;
    auto fx = plugins::makeFxTrack(p, plugins::types::delay, "Delay");
    fx.inserts[0].id = 300;
    fx.inserts[0].params[plugins::delay::time] = 10.0f; // 480 samples
    fx.inserts[0].params[plugins::delay::feedback] = 0.0f;
    fx.inserts[0].params[plugins::delay::tone] = 20000.0f;
    p.buses.push_back(fx);
    p.tracks[0].sends.push_back({fx.id, 1.0f, false});

    ImpulseLoader loader;
    engine::AudioEngine engine{loader, builtIns, 0};
    constexpr int block = 64;
    engine.prepare(48000.0, block);
    engine.setProject(p);
    engine.play();
    std::vector<float> l(block), r(block);
    float* outputs[] = {l.data(), r.data()};
    std::vector<float> out;
    for (int i = 0; i < 12; ++i)
    {
        engine.process(outputs, 2, block);
        out.insert(out.end(), l.begin(), l.end());
    }
    // The dry impulse at 0 (the track's own output), the echo at 480 (through the FX track).
    CHECK(std::abs(out[0]) > 0.5f);
    float echo = 0.0f;
    for (int i = 470; i < 500; ++i)
        echo = std::max(echo, std::abs(out[static_cast<size_t>(i)]));
    CHECK(echo > 0.3f);
    float between = 0.0f;
    for (int i = 10; i < 460; ++i)
        between = std::max(between, std::abs(out[static_cast<size_t>(i)]));
    CHECK(between < 0.01f);
}
