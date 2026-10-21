#include "core/Command.h"
#include "core/Model.h"
#include "core/ProjectJson.h"
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

class ConstantLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double rate, std::string&) override
    {
        return std::make_shared<engine::MemoryAudioSource>(
            std::vector<std::vector<float>>{std::vector<float>(200000, 0.5f)}, rate);
    }
};

constexpr int blockSize = 64;
const float centre = std::cos(3.14159265f / 4.0f);

struct Harness
{
    ConstantLoader loader;
    engine::AudioEngine engine{loader, builtIns, 0};
    std::vector<float> left = std::vector<float>(blockSize), right = std::vector<float>(blockSize);
    float* outputs[2] = {left.data(), right.data()};
    Harness() { engine.prepare(48000.0, blockSize); }
    // Plays until the transport has rendered `samples` (a multiple of the block).
    void playTo(core::SamplePosition samples)
    {
        for (core::SamplePosition done = 0; done < samples; done += blockSize)
            engine.process(outputs, 2, blockSize);
    }
    float last() const { return left[blockSize - 1]; }
};

core::Project projectWithTrack()
{
    core::Project p;
    core::Track t;
    t.id = 1;
    t.clips.push_back({100, "c", "x.wav", 0, 200000, 0, 1.0f});
    p.tracks.push_back(t);
    p.nextId = 1000;
    return p;
}

core::AutomationLane gainLane(std::vector<core::AutomationPoint> points)
{
    core::AutomationLane lane;
    lane.id = 50;
    lane.target = core::AutomationTarget::gain;
    lane.points = std::move(points);
    return lane;
}
} // namespace

TEST_CASE("Automation interpolates between points and holds outside them", "[core][automation]")
{
    const std::vector<core::AutomationPoint> points{{100, -1.0f}, {200, 1.0f}};
    CHECK(core::automationValueAt(points.data(), 2, core::AutomationTarget::pan, 0) == -1.0f);
    CHECK(core::automationValueAt(points.data(), 2, core::AutomationTarget::pan, 150) == Approx(0.0f));
    CHECK(core::automationValueAt(points.data(), 2, core::AutomationTarget::pan, 175) == Approx(0.5f));
    CHECK(core::automationValueAt(points.data(), 2, core::AutomationTarget::pan, 1000) == 1.0f);
    // Gain moves evenly in dB: half way from 0 dB to -12 dB is -6 dB.
    const std::vector<core::AutomationPoint> fade{{0, 1.0f}, {100, 0.25f}};
    CHECK(core::automationValueAt(fade.data(), 2, core::AutomationTarget::gain, 50) == Approx(0.5f));
    // A fade to silence is linear (no log of zero).
    const std::vector<core::AutomationPoint> out{{0, 1.0f}, {100, 0.0f}};
    CHECK(core::automationValueAt(out.data(), 2, core::AutomationTarget::gain, 50) == Approx(0.5f));
    CHECK(core::automationValueAt(points.data(), 0, core::AutomationTarget::pan, 50) == 0.0f);
    // Many points: the search finds the right segment.
    std::vector<core::AutomationPoint> ramp;
    for (int i = 0; i < 100; ++i)
        ramp.push_back({i * 10, static_cast<float>(i)});
    CHECK(core::automationValueAt(ramp.data(), ramp.size(), core::AutomationTarget::pan, 425) ==
          Approx(42.5f));
}

TEST_CASE("Automation lanes, points and modes are undoable commands and survive a save", "[core][automation]")
{
    core::Project p = projectWithTrack();
    core::UndoStack undo;
    core::AutomationLane lane;
    lane.target = core::AutomationTarget::pan;
    lane.points = {{200, 1.0f}, {0, -1.0f}}; // sorted on the way in
    auto add = std::make_unique<core::AddAutomationLane>(1, lane);
    auto* raw = add.get();
    undo.execute(p, std::move(add));
    const auto laneId = raw->laneId();
    REQUIRE(p.tracks[0].findLane(laneId) != nullptr);
    CHECK(p.tracks[0].findLane(laneId)->points.front().position == 0);
    CHECK(p.tracks[0].findLane(core::AutomationTarget::pan) != nullptr);
    // One lane per target.
    CHECK_THROWS(undo.execute(p, std::make_unique<core::AddAutomationLane>(1, lane)));

    undo.execute(p,
                 std::make_unique<core::SetAutomationPoints>(
                     1, laneId, std::vector<core::AutomationPoint>{{50, 0.5f}}));
    CHECK(p.tracks[0].findLane(laneId)->points.size() == 1);
    undo.execute(p, std::make_unique<core::SetAutomationMode>(1, core::AutomationMode::latch));
    CHECK(p.tracks[0].automationMode == core::AutomationMode::latch);

    // A parameter lane and the file.
    core::AutomationLane param;
    param.target = core::AutomationTarget::insertParameter;
    param.insert = 77;
    param.parameter = 3;
    param.points = {{10, 0.25f}};
    undo.execute(p, std::make_unique<core::AddAutomationLane>(1, param));
    const auto loaded = core::projectFromJson(core::projectToJson(p));
    REQUIRE(loaded.has_value());
    CHECK(loaded->tracks[0].automation == p.tracks[0].automation);
    CHECK(loaded->tracks[0].automationMode == core::AutomationMode::latch);
    CHECK(loaded->tracks[0].findLane(core::AutomationTarget::insertParameter, 77, 3) != nullptr);
    CHECK(loaded->tracks[0].findLane(core::AutomationTarget::insertParameter, 77, 4) == nullptr);

    undo.undo(p); // the parameter lane
    undo.undo(p); // the mode
    CHECK(p.tracks[0].automationMode == core::AutomationMode::read);
    undo.undo(p); // the points
    CHECK(p.tracks[0].findLane(laneId)->points.size() == 2);
    undo.execute(p, std::make_unique<core::RemoveAutomationLane>(1, laneId));
    CHECK(p.tracks[0].automation.empty());
    undo.undo(p);
    CHECK(p.tracks[0].findLane(laneId) != nullptr);
}

TEST_CASE("A gain lane plays: the fader follows it, evenly in dB", "[engine][automation]")
{
    auto p = projectWithTrack();
    p.tracks[0].automation.push_back(gainLane({{0, 1.0f}, {48000, 0.25f}}));
    Harness h;
    h.engine.setProject(p);
    h.engine.play();
    h.playTo(24000);
    CHECK(h.last() == Approx(0.5f * centre * 0.5f).margin(1e-4f)); // -6 dB half way
    const auto shown = h.engine.automatedValues(1);
    CHECK(shown.gain);
    CHECK_FALSE(shown.pan);
    CHECK(shown.gainValue == Approx(0.5f).margin(1e-3f));
    h.playTo(48000);
    CHECK(h.last() == Approx(0.5f * centre * 0.25f).margin(1e-4f));
}

TEST_CASE("A touched lane is not played, and mode off ignores the lanes", "[engine][automation]")
{
    auto p = projectWithTrack();
    p.tracks[0].gain = 1.0f;
    p.tracks[0].automation.push_back(gainLane({{0, 0.25f}}));
    Harness h;
    h.engine.setProject(p);
    h.engine.play();
    h.playTo(640);
    CHECK(h.last() == Approx(0.5f * centre * 0.25f).margin(1e-4f));
    // While touched the live value wins (the session pushes what the user plays).
    h.engine.setAutomationTouched(50, true);
    h.engine.setLiveGain(1, 0.5f);
    h.playTo(640);
    CHECK(h.last() == Approx(0.5f * centre * 0.5f).margin(1e-4f));
    CHECK_FALSE(h.engine.automatedValues(1).gain);
    h.engine.setAutomationTouched(50, false);
    h.playTo(640);
    CHECK(h.last() == Approx(0.5f * centre * 0.25f).margin(1e-4f));

    p.tracks[0].automationMode = core::AutomationMode::off;
    h.engine.setProject(p);
    h.playTo(640);
    CHECK(h.last() == Approx(0.5f * centre * 1.0f).margin(1e-4f));
    CHECK_FALSE(h.engine.automatedValues(1).gain);
}

TEST_CASE("A pan lane and an insert parameter lane play", "[engine][automation]")
{
    auto p = projectWithTrack();
    core::Insert gain;
    gain.id = 7;
    gain.type = plugins::types::gain;
    gain.params = {0.0f};
    p.tracks[0].inserts.push_back(gain);
    core::AutomationLane param;
    param.id = 51;
    param.target = core::AutomationTarget::insertParameter;
    param.insert = 7;
    param.parameter = 0;
    param.points = {{0, -6.0206f}}; // half
    p.tracks[0].automation.push_back(param);
    core::AutomationLane pan;
    pan.id = 52;
    pan.target = core::AutomationTarget::pan;
    pan.points = {{0, -1.0f}}; // hard left
    p.tracks[0].automation.push_back(pan);
    Harness h;
    h.engine.setProject(p);
    h.engine.play();
    h.playTo(6400);
    CHECK(h.last() == Approx(0.5f * 0.5f).margin(1e-3f));
    CHECK(h.right[blockSize - 1] == Approx(0.0f).margin(1e-4f));
    CHECK(h.engine.insertInstance(7)->parameter(0) == Approx(-6.0206f).margin(1e-3f));
}
