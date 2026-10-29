#include "core/Command.h"
#include "core/ProjectJson.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

using namespace northlight::core;

namespace
{
// Two tracks (ids 1, 2) and an FX bus (10), the first sending to the bus.
Project mixProject()
{
    Project p;
    for (Id id : {Id{1}, Id{2}})
    {
        Track t;
        t.id = id;
        t.name = "T" + std::to_string(id);
        p.tracks.push_back(t);
    }
    Bus fx;
    fx.id = 10;
    fx.fx = true;
    fx.name = "Reverb";
    p.buses.push_back(fx);
    p.tracks[0].sends.push_back({10, 0.5f, false});
    p.nextId = 100;
    return p;
}

Insert insertOf(Id id, const std::string& type)
{
    Insert i;
    i.id = id;
    i.type = type;
    i.params = {0.25f, 0.75f};
    return i;
}

AutomationLane laneFor(Id laneId, Id insertId)
{
    AutomationLane lane;
    lane.id = laneId;
    lane.target = AutomationTarget::insertParameter;
    lane.insert = insertId;
    lane.points = {{0, 0.1f}, {100, 0.9f}};
    return lane;
}
} // namespace

TEST_CASE("A send's pan and mute are set, undone and redone, and a level change keeps them",
          "[core][command][mixer]")
{
    auto p = mixProject();
    UndoStack stack;
    REQUIRE(stack.execute(p, std::make_unique<SetSendPan>(1, 10, -0.5f)));
    REQUIRE(stack.execute(p, std::make_unique<SetSendMute>(1, 10, true)));
    CHECK(p.tracks[0].sends[0].pan == -0.5f);
    CHECK(p.tracks[0].sends[0].mute);
    // A level ride is a live value; it leaves pan and mute alone.
    REQUIRE(stack.execute(p, std::make_unique<SetSend>(1, 10, 0.25f, false)));
    CHECK(p.tracks[0].sends[0].level == 0.25f);
    CHECK(p.tracks[0].sends[0].pan == -0.5f);
    CHECK(p.tracks[0].sends[0].mute);
    CHECK(SetSendPan(1, 10, 0.0f).changeKind() == Command::ChangeKind::values);
    CHECK(SetSendMute(1, 10, true).liveChannel() == 1);

    REQUIRE(stack.undo(p)); // the level
    REQUIRE(stack.undo(p)); // the mute
    CHECK_FALSE(p.tracks[0].sends[0].mute);
    REQUIRE(stack.undo(p)); // the pan
    CHECK(p.tracks[0].sends[0].pan == 0.0f);
    REQUIRE(stack.redo(p));
    REQUIRE(stack.redo(p));
    CHECK(p.tracks[0].sends[0].pan == -0.5f);
    CHECK(p.tracks[0].sends[0].mute);
    // A send that is not there: the command fails and changes nothing.
    CHECK_FALSE(stack.execute(p, std::make_unique<SetSendPan>(2, 10, 1.0f)));
}

TEST_CASE("A hardware output is set and undone, and choosing a bus leaves it", "[core][command][mixer]")
{
    auto p = mixProject();
    UndoStack stack;
    REQUIRE(stack.execute(p, std::make_unique<SetHardwareOutput>(2, 1)));
    CHECK(p.tracks[1].hardwareOutput == 1);
    REQUIRE(stack.execute(p, std::make_unique<SetOutputBus>(2, 10)));
    CHECK(p.tracks[1].outputBus == 10);
    CHECK(p.tracks[1].hardwareOutput == -1);
    REQUIRE(stack.undo(p));
    CHECK(p.tracks[1].outputBus == invalidId);
    CHECK(p.tracks[1].hardwareOutput == 1);
    REQUIRE(stack.undo(p));
    CHECK(p.tracks[1].hardwareOutput == -1);
    REQUIRE(stack.redo(p));
    CHECK(p.tracks[1].hardwareOutput == 1);
    // The master always plays to the first pair.
    CHECK_FALSE(stack.execute(p, std::make_unique<SetHardwareOutput>(masterId, 2)));
}

TEST_CASE("Solo safe and a hidden strip are set and undone", "[core][command][mixer]")
{
    auto p = mixProject();
    UndoStack stack;
    REQUIRE(stack.execute(p, std::make_unique<SetSoloSafe>(10, true)));
    REQUIRE(stack.execute(p, std::make_unique<SetMixerHidden>(2, true)));
    CHECK(p.buses[0].soloSafe);
    CHECK(p.tracks[1].mixerHidden);
    CHECK(SetMixerHidden(2, true).isCosmetic());
    CHECK(SetMixerHidden(2, true).rebuildsViews());
    REQUIRE(stack.undo(p));
    REQUIRE(stack.undo(p));
    CHECK_FALSE(p.buses[0].soloSafe);
    CHECK_FALSE(p.tracks[1].mixerHidden);
    REQUIRE(stack.redo(p));
    CHECK(p.buses[0].soloSafe);
}

TEST_CASE("An exclusive solo solos one channel alone and a second one clears it", "[core][command][mixer]")
{
    auto p = mixProject();
    p.tracks[1].solo = true;
    p.buses[0].solo = true;
    UndoStack stack;
    REQUIRE(stack.execute(p, std::make_unique<SetExclusiveSolo>(1)));
    CHECK(p.tracks[0].solo);
    CHECK_FALSE(p.tracks[1].solo);
    CHECK_FALSE(p.buses[0].solo);
    // Already the only one: cleared.
    REQUIRE(stack.execute(p, std::make_unique<SetExclusiveSolo>(1)));
    CHECK_FALSE(p.tracks[0].solo);
    REQUIRE(stack.undo(p));
    CHECK(p.tracks[0].solo);
    REQUIRE(stack.undo(p));
    CHECK_FALSE(p.tracks[0].solo);
    CHECK(p.tracks[1].solo);
    CHECK(p.buses[0].solo);
    REQUIRE(stack.redo(p));
    CHECK(p.tracks[0].solo);
    CHECK_FALSE(p.tracks[1].solo);
}

TEST_CASE("Moving an insert to another channel keeps its id and lanes and clears a key it cannot use",
          "[core][command][mixer]")
{
    auto p = mixProject();
    auto moved = insertOf(50, "northlight.compressor");
    moved.sidechain = 2; // keyed from track 2
    p.tracks[0].inserts = {insertOf(49, "northlight.eq"), moved};
    p.tracks[0].automation = {laneFor(60, 50), laneFor(61, 49)};
    UndoStack stack;
    // To track 2: it cannot key from itself.
    REQUIRE(stack.execute(p, std::make_unique<MoveInsertToChannel>(1, 50, 2, 0)));
    REQUIRE(p.tracks[0].inserts.size() == 1);
    REQUIRE(p.tracks[1].inserts.size() == 1);
    CHECK(p.tracks[1].inserts[0].id == 50);
    CHECK(p.tracks[1].inserts[0].sidechain == invalidId);
    REQUIRE(p.tracks[1].automation.size() == 1);
    CHECK(p.tracks[1].automation[0].id == 60);
    REQUIRE(p.tracks[0].automation.size() == 1);
    CHECK(p.tracks[0].automation[0].id == 61);

    REQUIRE(stack.undo(p));
    REQUIRE(p.tracks[0].inserts.size() == 2);
    CHECK(p.tracks[0].inserts[1].sidechain == 2);
    CHECK(p.tracks[1].inserts.empty());
    CHECK(p.tracks[0].automation.size() == 2);
    REQUIRE(stack.redo(p));
    CHECK(p.tracks[1].inserts[0].id == 50);
    // Onto the master: its inserts take no key, and the move is refused to itself.
    CHECK_FALSE(stack.execute(p, std::make_unique<MoveInsertToChannel>(2, 50, 2)));
    REQUIRE(stack.execute(p, std::make_unique<MoveInsertToChannel>(2, 50, masterId)));
    CHECK(p.master.inserts.size() == 1);
}

TEST_CASE("Pasting inserts adds copies with new ids in one step", "[core][command][mixer]")
{
    auto p = mixProject();
    p.tracks[0].inserts = {insertOf(49, "northlight.eq")};
    auto a = insertOf(49, "northlight.eq"); // copies of existing inserts, ids and all
    auto b = insertOf(50, "northlight.reverb");
    b.sidechain = 2;
    b.windowOpen = true;
    UndoStack stack;
    REQUIRE(stack.execute(p, std::make_unique<AddInserts>(1, std::vector{a, b}, 0)));
    REQUIRE(p.tracks[0].inserts.size() == 3);
    const auto first = p.tracks[0].inserts[0].id, second = p.tracks[0].inserts[1].id;
    CHECK(first != 49);
    CHECK(second != 50);
    CHECK(first != second);
    CHECK(p.tracks[0].inserts[1].sidechain == 2); // a track key is fine on a track
    CHECK_FALSE(p.tracks[0].inserts[1].windowOpen);
    CHECK(p.tracks[0].inserts[2].id == 49);
    CHECK(stack.undoName() == "Paste inserts");
    REQUIRE(stack.undo(p));
    REQUIRE(p.tracks[0].inserts.size() == 1);
    CHECK(p.tracks[0].inserts[0].id == 49);
    REQUIRE(stack.redo(p));
    REQUIRE(p.tracks[0].inserts.size() == 3);
    CHECK(p.tracks[0].inserts[0].id == first); // the same ids again
    // Into a bus a track's key stays; into the master none does.
    REQUIRE(stack.execute(p, std::make_unique<AddInserts>(masterId, std::vector{b})));
    CHECK(p.master.inserts[0].sidechain == invalidId);
}

TEST_CASE("Replacing an effect keeps its place, mix and key and drops its lanes", "[core][command][mixer]")
{
    auto p = mixProject();
    auto old = insertOf(50, "northlight.eq");
    old.mix = 0.5f;
    old.sidechain = 2;
    old.bypass = true;
    p.tracks[0].inserts = {insertOf(49, "northlight.gain"), old};
    p.tracks[0].automation = {laneFor(60, 50)};
    Insert replacement;
    replacement.type = "northlight.reverb";
    replacement.params = {1.0f, 2.0f, 3.0f};
    UndoStack stack;
    ReplaceInsert command(1, 50, replacement);
    CHECK(command.capturesInsertState());
    REQUIRE(stack.execute(p, std::make_unique<ReplaceInsert>(1, 50, replacement)));
    const auto& now = p.tracks[0].inserts[1];
    CHECK(now.id == 50);
    CHECK(now.type == "northlight.reverb");
    CHECK(now.params == std::vector<float>{1.0f, 2.0f, 3.0f});
    CHECK(now.mix == 0.5f);
    CHECK(now.sidechain == 2);
    CHECK_FALSE(now.bypass);
    CHECK(p.tracks[0].automation.empty());
    REQUIRE(stack.undo(p));
    CHECK(p.tracks[0].inserts[1].type == "northlight.eq");
    CHECK(p.tracks[0].inserts[1].bypass);
    CHECK(p.tracks[0].automation.size() == 1);
    REQUIRE(stack.redo(p));
    CHECK(p.tracks[0].inserts[1].type == "northlight.reverb");
}

TEST_CASE("A plugin preset is one step that hands the state back on undo and redo", "[core][command][mixer]")
{
    auto p = mixProject();
    auto hosted = insertOf(50, "vst3:abc");
    hosted.state = "before";
    p.tracks[0].inserts = {hosted};
    UndoStack stack;
    REQUIRE(stack.execute(p, std::make_unique<SetPluginState>(1, 50, "before", "after", "Preset Warm")));
    CHECK(p.tracks[0].inserts[0].state == "after");
    CHECK(stack.undoName() == "Preset Warm");
    // The first apply only records: the plugin already has the state.
    CHECK(stack.undoTop()->pushesInsertState() == invalidId);
    REQUIRE(stack.undo(p));
    CHECK(p.tracks[0].inserts[0].state == "before");
    CHECK(stack.redoTop()->pushesInsertState() == 50);
    REQUIRE(stack.redo(p));
    CHECK(p.tracks[0].inserts[0].state == "after");
    CHECK(stack.undoTop()->pushesInsertState() == 50);
}

TEST_CASE("Send pan and mute, hardware output, solo safe, hidden strips and windows round-trip",
          "[core][json][mixer]")
{
    auto p = mixProject();
    p.tracks[0].sends[0].pan = 0.25f;
    p.tracks[0].sends[0].mute = true;
    p.tracks[1].hardwareOutput = 2;
    p.tracks[1].mixerHidden = true;
    p.buses[0].soloSafe = true;
    auto hosted = insertOf(50, "vst3:abc");
    hosted.windowOpen = true;
    hosted.pluginName = "Magic Verb";
    p.buses[0].inserts = {hosted};
    const auto text = projectToJson(p);
    const auto back = projectFromJson(text);
    REQUIRE(back.has_value());
    CHECK(back->tracks[0].sends[0].pan == 0.25f);
    CHECK(back->tracks[0].sends[0].mute);
    CHECK(back->tracks[1].hardwareOutput == 2);
    CHECK(back->tracks[1].mixerHidden);
    CHECK(back->buses[0].soloSafe);
    CHECK(back->buses[0].inserts[0].windowOpen);
    CHECK(back->buses[0].inserts[0].pluginName == "Magic Verb");
    CHECK(back->tracks[0].hardwareOutput == -1);
    CHECK(back->tracks[0].sends == p.tracks[0].sends);
}
