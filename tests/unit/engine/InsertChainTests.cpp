#include "core/Command.h"
#include "core/Model.h"
#include "core/ProjectJson.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;

namespace
{
const plugins::BuiltInFactory builtIns;

// Every file is a mono constant of 0.5.
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
const float centre = std::cos(3.14159265f / 4.0f);

struct Harness
{
    ConstantLoader loader;
    engine::AudioEngine engine{loader, builtIns, 0};
    std::vector<float> left = std::vector<float>(blockSize), right = std::vector<float>(blockSize);
    float* outputs[2] = {left.data(), right.data()};
    Harness() { engine.prepare(48000.0, blockSize); }
    void play(const core::Project& p, int blocks = 1)
    {
        engine.setProject(p);
        engine.play();
        for (int i = 0; i < blocks; ++i)
            engine.process(outputs, 2, blockSize);
    }
    void more(int blocks)
    {
        for (int i = 0; i < blocks; ++i)
            engine.process(outputs, 2, blockSize);
    }
};

core::Track trackWithClip(core::Id id)
{
    core::Track t;
    t.id = id;
    t.clips.push_back({id * 100, "c", "x.wav", 0, 100000, 0, 1.0f});
    return t;
}

core::Insert gainInsert(core::Id id, float db)
{
    core::Insert i;
    i.id = id;
    i.type = plugins::types::gain;
    i.params = {db};
    return i;
}
} // namespace

TEST_CASE("Inserts run in chain order and the instances survive a recompile", "[engine][inserts]")
{
    Harness h;
    core::Project p;
    auto t = trackWithClip(1);
    t.inserts.push_back(gainInsert(10, -6.0f));
    t.inserts.push_back(gainInsert(11, -6.0f));
    p.tracks.push_back(t);
    h.play(p, 60); // the gain ramps settle (20 ms each)
    const float twice = 0.5f * centre * std::pow(10.0f, -12.0f / 20.0f);
    CHECK(h.left[blockSize - 1] == Approx(twice).margin(0.002f));

    const auto first = h.engine.insertInstance(10);
    const auto second = h.engine.insertInstance(11);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    // A fader move recompiles the snapshot; the same instances carry on.
    p.tracks[0].gain = 0.5f;
    h.engine.setProject(p);
    h.more(60);
    CHECK(h.engine.insertInstance(10) == first);
    CHECK(h.engine.insertInstance(11) == second);
    CHECK(h.left[blockSize - 1] == Approx(twice * 0.5f).margin(0.002f));

    // Reordering is a different chain but the same instances.
    core::UndoStack stack;
    stack.execute(p, std::make_unique<core::MoveInsert>(1, 11, 0));
    h.engine.setProject(p);
    CHECK(h.engine.insertInstance(10) == first);
    CHECK(h.engine.snapshotForTest()->tracks[0].inserts[0] == second.get());
}

TEST_CASE("A bus runs its inserts on the sum of what feeds it", "[engine][inserts]")
{
    Harness h;
    core::Project p;
    auto a = trackWithClip(1);
    auto b = trackWithClip(2);
    a.outputBus = b.outputBus = 50;
    p.tracks.push_back(a);
    p.tracks.push_back(b);
    core::Bus bus;
    bus.id = 50;
    bus.inserts.push_back(gainInsert(51, -6.0f));
    p.buses.push_back(bus);
    h.play(p, 60);
    CHECK(h.left[blockSize - 1] ==
          Approx(2.0f * 0.5f * centre * std::pow(10.0f, -6.0f / 20.0f)).margin(0.003f));
}

TEST_CASE("Bypass crossfades and keeps the processor running", "[engine][inserts][bypass]")
{
    Harness h;
    core::Project p;
    auto t = trackWithClip(1);
    t.inserts.push_back(gainInsert(10, -20.0f));
    p.tracks.push_back(t);
    h.play(p, 60);
    const float wet = 0.5f * centre * std::pow(10.0f, -20.0f / 20.0f);
    CHECK(h.left[blockSize - 1] == Approx(wet).margin(0.002f));
    p.tracks[0].inserts[0].bypass = true;
    h.engine.setProject(p);
    h.more(1);
    // One block (1.3 ms) into a 10 ms crossfade: between wet and dry, monotonic.
    const float dry = 0.5f * centre;
    CHECK(h.left[blockSize - 1] > wet);
    CHECK(h.left[blockSize - 1] < dry);
    CHECK(h.left[blockSize - 1] > h.left[0]);
    h.more(20);
    CHECK(h.left[blockSize - 1] == Approx(dry).margin(0.002f)); // settled
    p.tracks[0].inserts[0].bypass = false;
    h.engine.setProject(p);
    h.more(20);
    CHECK(h.left[blockSize - 1] == Approx(wet).margin(0.002f));
}

TEST_CASE("Fader, pan and master moves ramp across a block instead of stepping", "[engine][smoothing]")
{
    Harness h;
    core::Project p;
    p.tracks.push_back(trackWithClip(1));
    h.play(p, 2);
    CHECK(h.left[blockSize - 1] == Approx(0.5f * centre));
    // The fader drops to -inf: the block glides down, sample by sample.
    p.tracks[0].gain = 0.0f;
    h.engine.applyLiveValues(p); // the live path: no recompile
    h.more(1);
    CHECK(h.left[0] == Approx(0.5f * centre * (1.0f - 1.0f / blockSize)).margin(0.002f));
    CHECK(h.left[blockSize / 2] == Approx(0.5f * centre * 0.5f).margin(0.01f));
    CHECK(h.left[blockSize - 1] == Approx(0.0f).margin(1.0e-4f));
    for (int i = 1; i < blockSize; ++i)
        CHECK(h.left[static_cast<size_t>(i)] <= h.left[static_cast<size_t>(i - 1)] + 1.0e-6f);
    // Pan hard left: the right channel glides to zero.
    p.tracks[0].gain = 1.0f;
    p.tracks[0].pan = -1.0f;
    h.engine.applyLiveValues(p);
    h.more(1);
    CHECK(h.right[blockSize - 1] == Approx(0.0f).margin(1.0e-4f));
    CHECK(h.right[0] > 0.0f);
    CHECK(h.left[blockSize - 1] == Approx(0.5f).margin(1.0e-3f));
    // Master to half: ramps too.
    p.master.gain = 0.5f;
    h.engine.applyLiveValues(p);
    h.more(1);
    CHECK(h.left[0] > h.left[blockSize - 1]);
    CHECK(h.left[blockSize - 1] == Approx(0.25f).margin(1.0e-3f));
}

TEST_CASE("Soloing a send-fed bus keeps the sends and mutes the dry outputs", "[engine][mixer][solo]")
{
    Harness h;
    core::Project p;
    auto a = trackWithClip(1);
    auto b = trackWithClip(2);
    a.sends.push_back({50, 1.0f, false});
    b.sends.push_back({50, 1.0f, false});
    p.tracks.push_back(a);
    p.tracks.push_back(b);
    core::Bus reverb;
    reverb.id = 50;
    reverb.solo = true;
    p.buses.push_back(reverb);
    h.play(p, 2);
    // Only the bus reaches the master: the two sends summed, nothing dry.
    CHECK(h.left[blockSize - 1] == Approx(2.0f * 0.5f * centre).margin(0.002f));
    const auto* snapshot = h.engine.snapshotForTest();
    CHECK_FALSE(snapshot->tracks[0].audible);
    CHECK(snapshot->tracks[0].sendsAudible);
    CHECK(snapshot->buses[0].audible);

    // Soloing a track that feeds the bus by output lights the bus and the track.
    p.buses[0].solo = false;
    p.tracks[0].solo = true;
    p.tracks[0].sends.clear();
    p.tracks[0].outputBus = 50;
    h.engine.setProject(p);
    h.more(2);
    CHECK(h.engine.snapshotForTest()->tracks[0].audible);
    CHECK(h.engine.snapshotForTest()->buses[0].audible);
    CHECK_FALSE(h.engine.snapshotForTest()->tracks[1].audible);
    CHECK_FALSE(h.engine.snapshotForTest()->tracks[1].sendsAudible);
}

TEST_CASE("Routing and send cycles are refused at the command layer", "[core][command][routing]")
{
    core::Project p;
    core::Bus a, b, c;
    a.id = 1;
    b.id = 2;
    c.id = 3;
    p.buses = {a, b, c};
    p.nextId = 10;
    core::UndoStack stack;
    stack.execute(p, std::make_unique<core::SetOutputBus>(1, 2));         // A -> B
    stack.execute(p, std::make_unique<core::SetSend>(2, 3, 1.0f, false)); // B sends to C
    CHECK(p.busReaches(1, 3));
    CHECK_FALSE(p.busReaches(3, 1));
    CHECK(p.wouldCycle(3, 1)); // C -> A would close the loop
    CHECK(p.wouldCycle(2, 2)); // a bus to itself
    CHECK(p.wouldCycle(3, 2)); // C -> B closes B -> C -> B
    CHECK_THROWS(core::SetOutputBus(3, 1).apply(p));
    CHECK_THROWS(core::SetSend(3, 1, 1.0f, false).apply(p));
    CHECK_THROWS(core::SetSend(2, 2, 1.0f, false).apply(p));
    CHECK_THROWS(core::SetOutputBus(1, 1).apply(p));
    // Undo leaves the routing exactly as it was.
    stack.undo(p);
    stack.undo(p);
    CHECK(p.buses[0].outputBus == core::invalidId);
    CHECK(p.buses[1].sends.empty());
}

TEST_CASE("Insert commands undo exactly and round-trip through JSON", "[core][command][inserts]")
{
    core::Project p;
    p.tracks.push_back(trackWithClip(1));
    p.nextId = 1000;
    core::UndoStack stack;
    auto add = std::make_unique<core::AddInsert>(1, gainInsert(core::invalidId, -3.0f));
    const auto* addPtr = add.get();
    stack.execute(p, std::move(add));
    const core::Id id = addPtr->insertId();
    stack.execute(p, std::make_unique<core::SetInsertParameter>(1, id, 2, 0.5f)); // grows the vector
    CHECK(p.tracks[0].inserts[0].params.size() == 3);
    stack.undo(p);
    CHECK(p.tracks[0].inserts[0].params.size() == 1); // shrunk back
    stack.execute(p, std::make_unique<core::SetInsertBypass>(1, id, true));
    stack.execute(p, std::make_unique<core::SetInsertMix>(1, id, 0.3f));
    stack.execute(p, std::make_unique<core::RemoveInsert>(1, id));
    CHECK(p.tracks[0].inserts.empty());
    stack.undo(p);
    REQUIRE(p.tracks[0].inserts.size() == 1);
    CHECK(p.tracks[0].inserts[0].bypass);
    CHECK(p.tracks[0].inserts[0].mix == 0.3f);
    const auto text = core::projectToJson(p);
    const auto back = core::projectFromJson(text);
    REQUIRE(back.has_value());
    CHECK(*back == p);
    // A used id is refused; a duplicated id in a file is repaired on load.
    CHECK_THROWS(core::AddInsert(1, gainInsert(id, 0.0f)).apply(p));
    auto dup = p;
    dup.tracks[0].inserts.push_back(gainInsert(id, 0.0f));
    const auto repaired = core::projectFromJson(core::projectToJson(dup));
    REQUIRE(repaired.has_value());
    REQUIRE(repaired->tracks[0].inserts.size() == 2);
    CHECK(repaired->tracks[0].inserts[0].id != repaired->tracks[0].inserts[1].id);
}

TEST_CASE("Removing a bus and undoing it keeps the order of the sends", "[core][command][routing]")
{
    core::Project p;
    auto t = trackWithClip(1);
    t.sends = {{50, 0.1f, false}, {60, 0.2f, false}, {70, 0.3f, false}};
    p.tracks.push_back(t);
    for (const core::Id id : {50, 60, 70})
    {
        core::Bus b;
        b.id = id;
        p.buses.push_back(b);
    }
    p.nextId = 100;
    core::UndoStack stack;
    stack.execute(p, std::make_unique<core::RemoveBus>(60));
    REQUIRE(p.tracks[0].sends.size() == 2);
    stack.undo(p);
    REQUIRE(p.tracks[0].sends.size() == 3);
    CHECK(p.tracks[0].sends[1].bus == 60); // back in the middle, not at the end
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> chainViolations{0};
void countChainViolation(const rt::Violation&) noexcept
{
    chainViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Rendering a chain of every built-in with ramps and bypass allocates nothing",
          "[engine][inserts][rt]")
{
    Harness h;
    core::Project p;
    auto t = trackWithClip(1);
    int id = 10;
    for (const auto& type : builtIns.typeNames())
    {
        core::Insert i;
        i.id = id++;
        i.type = type;
        i.params = plugins::defaultParameters(type);
        t.inserts.push_back(i);
    }
    p.tracks.push_back(t);
    h.engine.setProject(p);
    h.engine.play();
    const auto previous = rt::setViolationHandler(&countChainViolation);
    chainViolations = 0;
    for (int block = 0; block < 50; ++block)
    {
        if (block == 10)
        {
            p.tracks[0].gain = 0.3f;
            p.tracks[0].inserts[1].bypass = true;
            h.engine.applyLiveValues(p);
        }
        h.engine.process(h.outputs, 2, blockSize);
    }
    rt::setViolationHandler(previous);
    CHECK(chainViolations == 0);
}
#endif
