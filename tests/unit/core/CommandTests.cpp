#include "core/Command.h"
#include "core/ProjectJson.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace northlight::core;

namespace
{
Project projectWithTrack()
{
    Project p;
    Track t;
    t.name = "Guitar";
    UndoStack stack;
    stack.execute(p, std::make_unique<AddTrack>(t));
    return p;
}
} // namespace

TEST_CASE("Adding a track allocates an id and undo removes it", "[core][command]")
{
    Project p;
    UndoStack stack;
    Track t;
    t.name = "Vocal";
    stack.execute(p, std::make_unique<AddTrack>(t));
    REQUIRE(p.tracks.size() == 1);
    CHECK(p.tracks[0].id == 1);
    CHECK(p.nextId == 2);
    CHECK(stack.undoName() == "Add track");

    CHECK(stack.undo(p));
    CHECK(p.tracks.empty());
    CHECK_FALSE(stack.canUndo());
    CHECK(stack.canRedo());

    CHECK(stack.redo(p));
    REQUIRE(p.tracks.size() == 1);
    CHECK(p.tracks[0].id == 1); // the same id, not a new one
}

TEST_CASE("Removing a track restores it at the same index on undo", "[core][command]")
{
    Project p;
    UndoStack stack;
    for (const char* name : {"A", "B", "C"})
    {
        Track t;
        t.name = name;
        stack.execute(p, std::make_unique<AddTrack>(t));
    }
    const Id middle = p.tracks[1].id;
    stack.execute(p, std::make_unique<RemoveTrack>(middle));
    REQUIRE(p.tracks.size() == 2);
    CHECK(p.tracks[1].name == "C");

    stack.undo(p);
    REQUIRE(p.tracks.size() == 3);
    CHECK(p.tracks[1].name == "B");
    CHECK(p.tracks[1].id == middle);
}

TEST_CASE("Track property commands revert to the previous value", "[core][command]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    const Id id = p.tracks[0].id;

    stack.execute(p, std::make_unique<SetTrackGain>(id, 0.5f));
    stack.execute(p, std::make_unique<SetTrackPan>(id, -0.25f));
    stack.execute(p, std::make_unique<SetTrackMute>(id, true));
    stack.execute(p, std::make_unique<SetTrackSolo>(id, true));
    stack.execute(p, std::make_unique<SetTrackName>(id, "Bass"));
    CHECK(p.tracks[0].gain == 0.5f);
    CHECK(p.tracks[0].pan == -0.25f);
    CHECK(p.tracks[0].mute);
    CHECK(p.tracks[0].solo);
    CHECK(p.tracks[0].name == "Bass");

    while (stack.undo(p))
    {
    }
    CHECK(p.tracks[0].gain == 1.0f);
    CHECK(p.tracks[0].pan == 0.0f);
    CHECK_FALSE(p.tracks[0].mute);
    CHECK_FALSE(p.tracks[0].solo);
    CHECK(p.tracks[0].name == "Guitar");
}

TEST_CASE("Track input, arm and monitor commands revert", "[core][command]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    const Id id = p.tracks[0].id;
    CHECK(p.tracks[0].inputChannel == -1);
    CHECK(p.tracks[0].monitor == MonitorMode::automatic);

    stack.execute(p, std::make_unique<SetTrackInput>(id, 3));
    stack.execute(p, std::make_unique<SetTrackArmed>(id, true));
    stack.execute(p, std::make_unique<SetTrackMonitor>(id, MonitorMode::on));
    CHECK(p.tracks[0].inputChannel == 3);
    CHECK(p.tracks[0].armed);
    CHECK(p.tracks[0].monitor == MonitorMode::on);

    while (stack.undo(p))
    {
    }
    CHECK(p.tracks[0].inputChannel == -1);
    CHECK_FALSE(p.tracks[0].armed);
    CHECK(p.tracks[0].monitor == MonitorMode::automatic);
}

TEST_CASE("Clips can be added, moved between tracks, trimmed and removed with undo", "[core][command]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    Track second;
    second.name = "Second";
    stack.execute(p, std::make_unique<AddTrack>(second));
    const Id t1 = p.tracks[0].id;
    const Id t2 = p.tracks[1].id;

    AudioClip clip;
    clip.name = "take";
    clip.mediaPath = "take.wav";
    clip.start = 1000;
    clip.length = 500;
    auto add = std::make_unique<AddClip>(t1, clip);
    auto* addPtr = add.get();
    stack.execute(p, std::move(add));
    const Id clipId = addPtr->clipId();
    REQUIRE(p.tracks[0].clips.size() == 1);
    CHECK(clipId != invalidId);

    stack.execute(p, std::make_unique<MoveClip>(t1, clipId, t2, 2000));
    CHECK(p.tracks[0].clips.empty());
    REQUIRE(p.tracks[1].clips.size() == 1);
    CHECK(p.tracks[1].clips[0].start == 2000);

    stack.execute(p, std::make_unique<TrimClip>(t2, clipId, 2100, 300, 100));
    CHECK(p.tracks[1].clips[0].start == 2100);
    CHECK(p.tracks[1].clips[0].length == 300);
    CHECK(p.tracks[1].clips[0].sourceOffset == 100);

    stack.execute(p, std::make_unique<SetClipGain>(t2, clipId, 0.25f));
    CHECK(p.tracks[1].clips[0].gain == 0.25f);

    stack.execute(p, std::make_unique<RemoveClip>(t2, clipId));
    CHECK(p.tracks[1].clips.empty());

    stack.undo(p); // remove
    REQUIRE(p.tracks[1].clips.size() == 1);
    stack.undo(p); // gain
    CHECK(p.tracks[1].clips[0].gain == 1.0f);
    stack.undo(p); // trim
    CHECK(p.tracks[1].clips[0].start == 2000);
    CHECK(p.tracks[1].clips[0].length == 500);
    stack.undo(p); // move
    REQUIRE(p.tracks[0].clips.size() == 1);
    CHECK(p.tracks[0].clips[0].start == 1000);
    CHECK(p.tracks[1].clips.empty());
    stack.undo(p); // add clip
    CHECK(p.tracks[0].clips.empty());
}

TEST_CASE("Buses, routing and sends are added, changed and undone", "[core][command]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    const Id track = p.tracks[0].id;

    Bus reverb;
    reverb.name = "Reverb";
    auto add = std::make_unique<AddBus>(reverb);
    const auto* addPtr = add.get();
    stack.execute(p, std::move(add));
    const Id bus = addPtr->busId();
    REQUIRE(p.buses.size() == 1);
    CHECK(p.findBus(bus) != nullptr);
    CHECK(p.findChannel(bus) == p.findBus(bus));
    CHECK(p.findChannel(track) == p.findTrack(track));

    stack.execute(p, std::make_unique<SetOutputBus>(track, bus));
    stack.execute(p, std::make_unique<SetSend>(track, bus, 0.5f, true));
    stack.execute(p, std::make_unique<SetSend>(track, bus, 0.25f, false)); // update
    stack.execute(p, std::make_unique<SetPhaseInvert>(track, true));
    stack.execute(p, std::make_unique<SetColour>(track, 0xff336699u));
    stack.execute(p, std::make_unique<SetChannelHeight>(track, 120));
    stack.execute(p, std::make_unique<SetChannelHeight>(bus, 40)); // FX tracks have rows too
    stack.execute(p, std::make_unique<SetTrackGain>(bus, 0.7f));   // fader commands work on buses
    stack.execute(p, std::make_unique<SetMasterGain>(0.9f));
    CHECK(p.tracks[0].outputBus == bus);
    REQUIRE(p.tracks[0].sends.size() == 1);
    CHECK(p.tracks[0].sends[0] == Send{bus, 0.25f, false});
    CHECK(p.tracks[0].phaseInvert);
    CHECK(p.tracks[0].colour == 0xff336699u);
    CHECK(p.tracks[0].height == 120);
    CHECK(p.buses[0].height == 40);
    CHECK(p.buses[0].gain == 0.7f);
    CHECK(p.master.gain == 0.9f);

    // Removing the bus detaches the routing and the send; undo restores both.
    stack.execute(p, std::make_unique<RemoveBus>(bus));
    CHECK(p.buses.empty());
    CHECK(p.tracks[0].outputBus == invalidId);
    CHECK(p.tracks[0].sends.empty());
    stack.undo(p);
    REQUIRE(p.buses.size() == 1);
    CHECK(p.tracks[0].outputBus == bus);
    REQUIRE(p.tracks[0].sends.size() == 1);

    stack.execute(p, std::make_unique<RemoveSend>(track, bus));
    CHECK(p.tracks[0].sends.empty());
    stack.undo(p);
    CHECK(p.tracks[0].sends.size() == 1);

    while (stack.undo(p))
    {
    }
    CHECK(p.buses.empty());
    CHECK(p.tracks[0].outputBus == invalidId);
    CHECK(p.tracks[0].sends.empty());
    CHECK_FALSE(p.tracks[0].phaseInvert);
    CHECK(p.tracks[0].colour == 0);
    CHECK(p.tracks[0].height == 0);
    CHECK(p.master.gain == 1.0f);

    CHECK_FALSE(SetOutputBus(track, 999).apply(p));
}

TEST_CASE("A group of commands undoes as one step", "[core][command]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    const Id id = p.tracks[0].id;

    stack.beginGroup("Edit track");
    stack.execute(p, std::make_unique<SetTrackGain>(id, 0.5f));
    stack.execute(p, std::make_unique<SetTrackPan>(id, 1.0f));
    stack.endGroup();
    CHECK(stack.undoName() == "Edit track");

    CHECK(stack.undo(p));
    CHECK(p.tracks[0].gain == 1.0f);
    CHECK(p.tracks[0].pan == 0.0f);
    CHECK_FALSE(stack.canUndo());

    CHECK(stack.redo(p));
    CHECK(p.tracks[0].gain == 0.5f);
    CHECK(p.tracks[0].pan == 1.0f);
}

namespace
{
// Adds one to the project's tempo on apply; on revert takes it off, or fails when
// asked to (a command whose target has gone).
class Counting final : public Command
{
public:
    explicit Counting(bool failRevert = false) : failRevert_(failRevert) {}
    std::string name() const override { return "Count"; }
    bool apply(Project& p) override
    {
        p.tempoBpm += 1.0;
        return true;
    }
    bool revert(Project& p) override
    {
        if (failRevert_)
            return false;
        p.tempoBpm -= 1.0;
        return true;
    }

private:
    bool failRevert_;
};
} // namespace

TEST_CASE("An undo that fails part way leaves the project and the history as they were",
          "[core][command][undo]")
{
    Project p;
    p.tempoBpm = 100.0;
    UndoStack undo;
    undo.beginGroup("Both");
    undo.execute(p, std::make_unique<Counting>(true)); // reverted last: fails
    undo.execute(p, std::make_unique<Counting>());     // reverted first
    undo.endGroup();
    REQUIRE(p.tempoBpm == 102.0);

    CHECK_FALSE(undo.undo(p));
    CHECK(p.tempoBpm == 102.0); // the member already reverted was applied again
    CHECK(undo.undoName() == "Both");
    CHECK_FALSE(undo.canRedo());
}

TEST_CASE("Undoing or redoing back to the save point leaves nothing unsaved", "[core][command][undo]")
{
    Project p;
    UndoStack undo;
    CHECK(undo.atSavePoint());
    undo.execute(p, std::make_unique<Counting>());
    CHECK_FALSE(undo.atSavePoint());
    undo.markSaved();
    CHECK(undo.atSavePoint());
    undo.execute(p, std::make_unique<Counting>());
    CHECK_FALSE(undo.atSavePoint());
    undo.undo(p);
    CHECK(undo.atSavePoint());
    undo.undo(p);
    CHECK_FALSE(undo.atSavePoint());
    undo.redo(p);
    CHECK(undo.atSavePoint());

    // A group in progress is a change.
    undo.beginGroup("Drag");
    undo.execute(p, std::make_unique<Counting>());
    CHECK_FALSE(undo.atSavePoint());
    undo.endGroup();
    undo.undo(p);
    CHECK(undo.atSavePoint());

    // Undo past the save point, then a new command: the saved state is gone.
    undo.undo(p);
    undo.execute(p, std::make_unique<Counting>());
    CHECK_FALSE(undo.atSavePoint());
    undo.undo(p);
    CHECK_FALSE(undo.atSavePoint());
}

TEST_CASE("Names, colours and markers need no engine work but rebuild the views", "[core][command]")
{
    const std::vector<std::unique_ptr<Command>> commands = []
    {
        std::vector<std::unique_ptr<Command>> list;
        list.push_back(std::make_unique<SetTrackName>(1, "Bass"));
        list.push_back(std::make_unique<SetColour>(1, 0xff336699u));
        list.push_back(std::make_unique<AddMarker>(Marker{}));
        list.push_back(std::make_unique<SetMarker>(1, 0, 0, "Verse"));
        list.push_back(std::make_unique<RemoveMarker>(1));
        return list;
    }();
    for (const auto& c : commands)
    {
        INFO(c->name());
        CHECK(c->changeKind() == Command::ChangeKind::cosmetic);
        CHECK(c->rebuildsViews());
    }
    // A row height only redraws.
    CHECK_FALSE(SetChannelHeight(1, 80).rebuildsViews());
    // A structural change always rebuilds.
    CHECK(AddTrack(Track{}).rebuildsViews());
}

TEST_CASE("A fader's commands name the one channel whose live values they change", "[core][command]")
{
    CHECK(SetTrackGain(5, 0.5f).liveChannel() == 5);
    CHECK(SetTrackPan(5, 0.2f).liveChannel() == 5);
    CHECK(SetMasterGain(0.5f).liveChannel() == masterId);
    CHECK(AddTrack(Track{}).liveChannel() == invalidId);

    // A gesture over one channel names it; over two, none.
    Project p;
    Track a, b;
    a.id = 1;
    b.id = 2;
    p.tracks = {a, b};
    UndoStack undo;
    undo.beginGroup("Fader");
    undo.execute(p, std::make_unique<SetTrackGain>(1, 0.5f));
    undo.execute(p, std::make_unique<SetTrackGain>(1, 0.4f));
    undo.endGroup();
    CHECK(undo.undoTop()->liveChannel() == 1);
    undo.beginGroup("Both");
    undo.execute(p, std::make_unique<SetTrackGain>(1, 0.5f));
    undo.execute(p, std::make_unique<SetTrackGain>(2, 0.5f));
    undo.endGroup();
    CHECK(undo.undoTop()->liveChannel() == invalidId);
}

TEST_CASE("A new command after undo discards the redo history", "[core][command]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    const Id id = p.tracks[0].id;
    stack.execute(p, std::make_unique<SetTrackGain>(id, 0.5f));
    stack.undo(p);
    CHECK(stack.canRedo());
    stack.execute(p, std::make_unique<SetTempo>(90.0));
    stack.execute(p, std::make_unique<SetTimeSignature>(7, 8));
    CHECK(p.timeSignature.numerator == 7);
    CHECK(p.timeSignature.denominator == 8);
    stack.undo(p);
    CHECK(p.timeSignature.numerator == 4);
    stack.redo(p);
    CHECK(p.timeSignature.denominator == 8);
    CHECK_FALSE(stack.canRedo());
    CHECK(p.tempoBpm == 90.0);
}

TEST_CASE("Moving a track reorders the list and undoes back", "[core][command]")
{
    Project p = projectWithTrack();
    Track second, third;
    second.name = "Second";
    third.name = "Third";
    p.tracks.push_back(second);
    p.tracks.push_back(third);
    p.tracks[1].id = p.allocateId();
    p.tracks[2].id = p.allocateId();
    const Id first = p.tracks[0].id;
    UndoStack stack;
    stack.execute(p, std::make_unique<MoveTrack>(first, 2));
    CHECK(p.tracks[2].id == first);
    CHECK(p.tracks[0].name == "Second");
    stack.undo(p);
    CHECK(p.tracks[0].id == first);
    CHECK(p.tracks[2].name == "Third");
    stack.redo(p);
    CHECK(p.trackIndex(first) == 2);
    CHECK_FALSE(stack.execute(p, std::make_unique<MoveTrack>(Id{999999}, 0)));
}

TEST_CASE("A fader group moves its members together, keeping their balance", "[core][command][group]")
{
    Project p = projectWithTrack();
    Track second;
    second.name = "Second";
    second.gain = 0.5f;
    p.tracks.push_back(second);
    p.tracks.back().id = p.allocateId();
    Bus bus;
    bus.gain = 0.25f;
    p.buses.push_back(bus);
    p.buses.back().id = p.allocateId();
    UndoStack stack;
    const Id a = p.tracks[0].id, b = p.tracks[1].id, c = p.buses[0].id;

    auto add = std::make_unique<AddGroup>(Group{invalidId, "Rhythm", 0}, std::vector<Id>{a});
    const auto* addPtr = add.get();
    stack.execute(p, std::move(add));
    const Id group = addPtr->groupId();
    REQUIRE(p.findGroup(group) != nullptr);
    CHECK(p.tracks[0].group == group); // joined in the same step
    stack.execute(p, std::make_unique<SetChannelGroup>(c, group));
    CHECK(p.groupMembers(group).size() == 2); // b stays out

    stack.execute(p, std::make_unique<SetGroupGain>(group, 0.5f));
    CHECK(p.tracks[0].gain == 0.5f);
    CHECK(p.tracks[1].gain == 0.5f); // untouched
    CHECK(p.buses[0].gain == 0.125f);
    stack.execute(p, std::make_unique<SetGroupGain>(group, 100.0f)); // clamps at the fader's top
    CHECK(p.tracks[0].gain == maxChannelGain);
    CHECK(p.buses[0].gain == maxChannelGain);
    stack.undo(p);
    CHECK(p.tracks[0].gain == 0.5f);
    CHECK(p.buses[0].gain == 0.125f);

    stack.execute(p, std::make_unique<SetGroupMute>(group, true));
    stack.execute(p, std::make_unique<SetGroupSolo>(group, true));
    CHECK(p.tracks[0].mute);
    CHECK(p.buses[0].solo);
    CHECK_FALSE(p.tracks[1].mute);
    stack.undo(p);
    stack.undo(p);
    CHECK_FALSE(p.tracks[0].mute);
    CHECK_FALSE(p.buses[0].solo);

    // Removing the group releases the members; undo puts them back.
    stack.execute(p, std::make_unique<RemoveGroup>(group));
    CHECK(p.groups.empty());
    CHECK(p.tracks[0].group == invalidId);
    CHECK(p.buses[0].group == invalidId);
    stack.undo(p);
    REQUIRE(p.groups.size() == 1);
    CHECK(p.tracks[0].group == group);
    CHECK(p.buses[0].group == group);

    while (stack.undo(p))
    {
    }
    CHECK(p.groups.empty());
    CHECK(p.tracks[0].group == invalidId);

    CHECK_FALSE(SetChannelGroup(b, 999).apply(p));
    CHECK_FALSE(SetGroupGain(999, 1.0f).apply(p));
}

TEST_CASE("An insert added between two others lands there as one undo step", "[core][command]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    const Id track = p.tracks[0].id;
    for (const char* type : {"northlight.eq", "northlight.limiter"})
    {
        Insert insert;
        insert.type = type;
        stack.execute(p, std::make_unique<AddInsert>(track, insert));
    }
    const auto types = [&]
    {
        std::vector<std::string> out;
        for (const auto& insert : p.tracks[0].inserts)
            out.push_back(insert.type);
        return out;
    };
    const std::vector<std::string> before{"northlight.eq", "northlight.limiter"};
    const std::vector<std::string> after{"northlight.eq", "northlight.compressor", "northlight.limiter"};
    Insert comp;
    comp.type = "northlight.compressor";
    auto add = std::make_unique<AddInsert>(track, comp, 1);
    const auto* addPtr = add.get();
    stack.execute(p, std::move(add));
    CHECK(types() == after);
    const Id id = addPtr->insertId();
    CHECK(p.tracks[0].insertIndex(id) == 1);
    stack.undo(p);
    CHECK(types() == before);
    stack.redo(p);
    CHECK(types() == after);
    CHECK(p.tracks[0].insertIndex(id) == 1); // the same insert, back in its place
    // Past the end it goes last.
    Insert gain;
    gain.type = "northlight.gain";
    stack.execute(p, std::make_unique<AddInsert>(track, gain, 9));
    CHECK(p.tracks[0].inserts.back().type == "northlight.gain");
}

TEST_CASE("An insert's wet/dry mix is set, clamped and undone", "[core][command]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    const Id track = p.tracks[0].id;
    Insert insert;
    insert.type = "northlight.eq";
    auto add = std::make_unique<AddInsert>(track, insert);
    const auto* addPtr = add.get();
    stack.execute(p, std::move(add));
    const Id id = addPtr->insertId();
    CHECK(p.tracks[0].inserts[0].mix == 1.0f);
    stack.execute(p, std::make_unique<SetInsertMix>(track, id, 0.25f));
    CHECK(p.tracks[0].inserts[0].mix == 0.25f);
    stack.execute(p, std::make_unique<SetInsertMix>(track, id, 7.0f));
    CHECK(p.tracks[0].inserts[0].mix == 1.0f); // clamped
    stack.undo(p);
    CHECK(p.tracks[0].inserts[0].mix == 0.25f);
    stack.undo(p);
    CHECK(p.tracks[0].inserts[0].mix == 1.0f);
}

TEST_CASE("An insert's sidechain source is set to an existing channel and undone", "[core][command]")
{
    Project p = projectWithTrack();
    Track other;
    other.name = "Kick";
    UndoStack stack;
    stack.execute(p, std::make_unique<AddTrack>(other));
    const Id track = p.tracks[0].id, kick = p.tracks[1].id;
    Insert insert;
    insert.type = "northlight.compressor";
    auto add = std::make_unique<AddInsert>(track, insert);
    const auto* addPtr = add.get();
    stack.execute(p, std::move(add));
    const Id id = addPtr->insertId();
    CHECK(p.tracks[0].inserts[0].sidechain == invalidId);
    stack.execute(p, std::make_unique<SetInsertSidechain>(track, id, kick));
    CHECK(p.tracks[0].inserts[0].sidechain == kick);
    CHECK_FALSE(p.tracks[0].inserts[0].sidechainPostFader);
    // The tap: after the kick's fader, then undone back to before it.
    stack.execute(p, std::make_unique<SetInsertSidechain>(track, id, kick, true));
    CHECK(p.tracks[0].inserts[0].sidechainPostFader);
    stack.undo(p);
    CHECK(p.tracks[0].inserts[0].sidechain == kick);
    CHECK_FALSE(p.tracks[0].inserts[0].sidechainPostFader);
    stack.redo(p);
    CHECK(p.tracks[0].inserts[0].sidechainPostFader);
    // No source: no tap either.
    SetInsertSidechain clear(track, id, invalidId, true);
    clear.apply(p);
    CHECK_FALSE(p.tracks[0].inserts[0].sidechainPostFader);
    clear.revert(p);
    CHECK(p.tracks[0].inserts[0].sidechain == kick);
    CHECK(p.tracks[0].inserts[0].sidechainPostFader);
    CHECK_FALSE(SetInsertSidechain(track, id, 999).apply(p));
    // A loop: keying the track from itself, or from a bus it feeds (directly or
    // through another key), is refused.
    CHECK_FALSE(SetInsertSidechain(track, id, track).apply(p));
    Bus bus;
    bus.name = "Drums";
    stack.execute(p, std::make_unique<AddBus>(bus));
    const Id drums = p.buses[0].id;
    stack.execute(p, std::make_unique<SetOutputBus>(track, drums));
    CHECK(p.keyWouldCycle(track, drums));
    CHECK_FALSE(SetInsertSidechain(track, id, drums).apply(p));
    // The kick feeds the track's key, which feeds the bus: keying the kick from
    // the bus would loop; without that key it would not.
    CHECK(p.keyWouldCycle(kick, drums));
    stack.execute(p, std::make_unique<SetInsertSidechain>(track, id, invalidId));
    CHECK_FALSE(p.keyWouldCycle(kick, drums));
    // The kick keyed from the track: now the track must not be keyed from the kick.
    Insert kickInsert;
    kickInsert.type = "northlight.compressor";
    stack.execute(p, std::make_unique<AddInsert>(kick, kickInsert));
    const Id kickInsertId = p.tracks[1].inserts[0].id;
    stack.execute(p, std::make_unique<SetInsertSidechain>(kick, kickInsertId, track));
    CHECK(p.channelReaches(track, kick)); // through the key edge
    CHECK(p.keyWouldCycle(track, kick));
    stack.undo(p);
    CHECK(p.tracks[0].inserts[0].sidechain == invalidId);
}

TEST_CASE("Clips split, join, duplicate, fade and crossfade with undo", "[core][command][edit]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    const Id t = p.tracks[0].id;
    AudioClip clip;
    clip.name = "take";
    clip.mediaPath = "take.wav";
    clip.start = 1000;
    clip.length = 600;
    clip.sourceOffset = 50;
    clip.fadeIn = 20;
    clip.fadeOut = 30;
    auto add = std::make_unique<AddClip>(t, clip);
    const Id id = add->clipId() == invalidId ? invalidId : add->clipId();
    stack.execute(p, std::move(add));
    const Id left = p.tracks[0].clips[0].id;
    (void)id;

    // Split: the left keeps the id and the fade-in; the right takes the fade-out.
    auto split = std::make_unique<SplitClip>(t, left, 1400);
    const auto* splitPtr = split.get();
    stack.execute(p, std::move(split));
    const Id right = splitPtr->rightClipId();
    REQUIRE(p.tracks[0].clips.size() == 2);
    CHECK(p.tracks[0].clips[0].id == left);
    CHECK(p.tracks[0].clips[0].length == 400);
    CHECK(p.tracks[0].clips[0].fadeIn == 20);
    CHECK(p.tracks[0].clips[0].fadeOut == 0);
    CHECK(p.tracks[0].clips[1].id == right);
    CHECK(p.tracks[0].clips[1].start == 1400);
    CHECK(p.tracks[0].clips[1].length == 200);
    CHECK(p.tracks[0].clips[1].sourceOffset == 450);
    CHECK(p.tracks[0].clips[1].fadeIn == 0);
    CHECK(p.tracks[0].clips[1].fadeOut == 30);
    CHECK_FALSE(SplitClip(t, left, 1000).apply(p)); // not strictly inside

    // Join puts it back together.
    CHECK(JoinClips::canJoin(p.tracks[0].clips[0], p.tracks[0].clips[1]));
    stack.execute(p, std::make_unique<JoinClips>(t, left, right));
    REQUIRE(p.tracks[0].clips.size() == 1);
    CHECK(p.tracks[0].clips[0].length == 600);
    CHECK(p.tracks[0].clips[0].fadeOut == 30);
    stack.undo(p); // join
    REQUIRE(p.tracks[0].clips.size() == 2);
    stack.undo(p); // split
    REQUIRE(p.tracks[0].clips.size() == 1);
    CHECK(p.tracks[0].clips[0].length == 600);
    CHECK(p.tracks[0].clips[0].fadeOut == 30);

    // Duplicate lands right after the original.
    auto dup = std::make_unique<DuplicateClip>(t, left);
    const auto* dupPtr = dup.get();
    stack.execute(p, std::move(dup));
    const Id copy = dupPtr->newClipId();
    REQUIRE(p.tracks[0].clips.size() == 2);
    CHECK(p.tracks[0].clips[1].id == copy);
    CHECK(p.tracks[0].clips[1].start == 1600);
    CHECK(p.tracks[0].clips[1].mediaPath == "take.wav");
    CHECK_FALSE(
        JoinClips::canJoin(p.tracks[0].clips[0], p.tracks[0].clips[1])); // the source does not continue

    // Fades clamp to the clip; a crossfade needs an overlap.
    stack.execute(p, std::make_unique<SetClipFades>(t, copy, 5000, 10));
    CHECK(p.tracks[0].clips[1].fadeIn == 600);
    CHECK(p.tracks[0].clips[1].fadeOut == 10);
    CHECK_FALSE(CrossfadeClips(t, left, copy).apply(p));
    stack.execute(p, std::make_unique<MoveClip>(t, copy, t, 1500)); // now overlapping by 100
    stack.execute(p, std::make_unique<CrossfadeClips>(t, left, copy));
    CHECK(p.tracks[0].clips[0].fadeOut == 100);
    CHECK(p.tracks[0].clips[1].fadeIn == 100);
    stack.undo(p);
    CHECK(p.tracks[0].clips[0].fadeOut == 30);
    CHECK(p.tracks[0].clips[1].fadeIn == 600);

    while (stack.undo(p))
    {
    }
    CHECK(p.tracks[0].clips.empty());
    CHECK(stack.redo(p));
    CHECK(p.tracks[0].clips.size() == 1);
}

TEST_CASE("Shifting clips after a position moves the later ones and pins at zero", "[core][command][edit]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    const Id t = p.tracks[0].id;
    for (const SamplePosition start : {100, 500, 900})
    {
        AudioClip c;
        c.mediaPath = "x.wav";
        c.start = start;
        c.length = 100;
        stack.execute(p, std::make_unique<AddClip>(t, c));
    }
    stack.execute(p, std::make_unique<ShiftClips>(t, 500, -300));
    CHECK(p.tracks[0].clips[0].start == 100);
    CHECK(p.tracks[0].clips[1].start == 200);
    CHECK(p.tracks[0].clips[2].start == 600);
    stack.execute(p, std::make_unique<ShiftClips>(t, 0, -150));
    CHECK(p.tracks[0].clips[0].start == 0); // pinned
    CHECK(p.tracks[0].clips[1].start == 50);
    stack.undo(p);
    CHECK(p.tracks[0].clips[0].start == 100);
    stack.undo(p);
    CHECK(p.tracks[0].clips[1].start == 500);
    CHECK(p.tracks[0].clips[2].start == 900);
}

TEST_CASE("Markers and regions are added, moved, renamed, removed and kept in order",
          "[core][command][markers]")
{
    Project p;
    UndoStack stack;
    auto add = std::make_unique<AddMarker>(Marker{invalidId, "Verse", 48000, 0});
    const auto* addPtr = add.get();
    stack.execute(p, std::move(add));
    const Id verse = addPtr->markerId();
    stack.execute(p, std::make_unique<AddMarker>(Marker{invalidId, "Intro", 0, 24000}));
    REQUIRE(p.markers.size() == 2);
    CHECK(p.markers[0].name == "Intro"); // sorted by position
    CHECK(p.markers[0].isRegion());
    CHECK(p.markers[0].end() == 24000);
    CHECK_FALSE(p.markers[1].isRegion());

    stack.execute(p, std::make_unique<SetMarker>(verse, -5, 0, "Verse 1")); // clamps at zero, re-sorts
    CHECK(p.markers[1].id == verse); // equal positions keep their order: Intro stays first
    CHECK(p.markers[1].name == "Verse 1");
    CHECK(p.markers[1].position == 0);
    stack.undo(p);
    CHECK(p.markers[1].id == verse);
    CHECK(p.markers[1].position == 48000);

    CHECK(p.markerBefore(48000)->name == "Intro");
    CHECK(p.markerAfter(0)->id == verse);
    CHECK(p.markerAfter(48000) == nullptr);
    CHECK(p.markerBefore(0) == nullptr);

    stack.execute(p, std::make_unique<RemoveMarker>(verse));
    CHECK(p.markers.size() == 1);
    stack.undo(p);
    REQUIRE(p.markers.size() == 2);
    CHECK(p.findMarker(verse) != nullptr);
    CHECK_FALSE(RemoveMarker(999).apply(p));
    while (stack.undo(p))
    {
    }
    CHECK(p.markers.empty());
}

TEST_CASE("MIDI clips and notes are added, moved, trimmed, split, edited, quantised and undone",
          "[core][command][midi]")
{
    Project p;
    Track t;
    t.name = "Keys";
    t.kind = TrackKind::midi;
    UndoStack stack;
    stack.execute(p, std::make_unique<AddTrack>(t));
    const Id track = p.tracks[0].id;
    CHECK(p.tracks[0].isMidi());
    stack.execute(p, std::make_unique<SetTrackMidiInput>(track, "Keystation"));
    CHECK(p.tracks[0].midiInput == "Keystation");

    MidiClip clip;
    clip.name = "riff";
    clip.start = 1000;
    clip.length = 2000;
    clip.notes.push_back({invalidId, 60, 100, 500, 400});
    clip.notes.push_back({invalidId, 64, 90, 0, 300});
    auto add = std::make_unique<AddMidiClip>(track, clip);
    const auto* addPtr = add.get();
    stack.execute(p, std::move(add));
    const Id clipId = addPtr->clipId();
    REQUIRE(p.tracks[0].midiClips.size() == 1);
    const auto& stored = p.tracks[0].midiClips[0];
    REQUIRE(stored.notes.size() == 2);
    CHECK(stored.notes[0].note == 64); // sorted by start
    CHECK(stored.notes[0].id != invalidId);
    CHECK(stored.notes[1].id != stored.notes[0].id);
    CHECK(p.length() == 3000);
    const Id c4 = stored.notes[1].id;

    // Notes: edit, add, remove, quantise.
    stack.execute(p, std::make_unique<SetNote>(track, clipId, MidiNote{c4, 200, 0, 50, 0}));
    CHECK(p.tracks[0].midiClips[0].notes[1].id == c4);         // re-sorted: 0 then 50
    CHECK(p.tracks[0].midiClips[0].findNote(c4)->note == 127); // clamped
    CHECK(p.tracks[0].midiClips[0].findNote(c4)->velocity == 1);
    CHECK(p.tracks[0].midiClips[0].findNote(c4)->length == 1);
    stack.undo(p);
    CHECK(p.tracks[0].midiClips[0].findNote(c4)->start == 500);
    auto addNote = std::make_unique<AddNote>(track, clipId, MidiNote{invalidId, 67, 80, 1010, 100});
    const auto* addNotePtr = addNote.get();
    stack.execute(p, std::move(addNote));
    const Id g4 = addNotePtr->noteId();
    REQUIRE(p.tracks[0].midiClips[0].notes.size() == 3);
    // Quantise to a 500-sample grid: 1000 + 1010 = 2010 -> 2000 -> clip-relative 1000.
    stack.execute(p, std::make_unique<QuantiseNotes>(track, clipId, std::vector<Id>{g4}, 500));
    CHECK(p.tracks[0].midiClips[0].findNote(g4)->start == 1000);
    stack.undo(p);
    CHECK(p.tracks[0].midiClips[0].findNote(g4)->start == 1010);
    stack.execute(
        p, std::make_unique<QuantiseNotes>(track, clipId, std::vector<Id>{}, 500, 0.5f)); // all, halfway
    CHECK(p.tracks[0].midiClips[0].findNote(g4)->start == 1005);
    stack.undo(p);
    stack.execute(p, std::make_unique<RemoveNote>(track, clipId, g4));
    CHECK(p.tracks[0].midiClips[0].notes.size() == 2);
    stack.undo(p);
    CHECK(p.tracks[0].midiClips[0].notes.size() == 3);

    // Trim keeps the notes on the timeline.
    stack.execute(p, std::make_unique<TrimMidiClip>(track, clipId, 1200, 1500));
    CHECK(p.tracks[0].midiClips[0].start == 1200);
    CHECK(p.tracks[0].midiClips[0].findNote(c4)->start == 300); // was 500 from 1000
    stack.undo(p);
    CHECK(p.tracks[0].midiClips[0].findNote(c4)->start == 500);

    // Split at 1400: the 64 (0..300) stays left; the 60 (500..900) and 67 (1010..) go right.
    auto split = std::make_unique<SplitMidiClip>(track, clipId, 1400);
    const auto* splitPtr = split.get();
    stack.execute(p, std::move(split));
    REQUIRE(p.tracks[0].midiClips.size() == 2);
    CHECK(p.tracks[0].midiClips[0].length == 400);
    CHECK(p.tracks[0].midiClips[0].notes.size() == 1);
    CHECK(p.tracks[0].midiClips[1].id == splitPtr->rightClipId());
    CHECK(p.tracks[0].midiClips[1].start == 1400);
    REQUIRE(p.tracks[0].midiClips[1].notes.size() == 2);
    CHECK(p.tracks[0].midiClips[1].notes[0].start == 100); // 500 - 400
    stack.undo(p);
    REQUIRE(p.tracks[0].midiClips.size() == 1);
    CHECK(p.tracks[0].midiClips[0].notes.size() == 3);

    // A note across a split is cut at it.
    stack.execute(p, std::make_unique<SplitMidiClip>(track, clipId, 1700)); // inside the 60 (1500..1900)
    CHECK(p.tracks[0].midiClips[0].findNote(c4)->length == 200);
    stack.undo(p);

    // Duplicate and move; a MIDI clip cannot land on an audio track.
    auto dup = std::make_unique<DuplicateMidiClip>(track, clipId);
    const auto* dupPtr = dup.get();
    stack.execute(p, std::move(dup));
    REQUIRE(p.tracks[0].midiClips.size() == 2);
    CHECK(p.tracks[0].midiClips[1].start == 3000);
    CHECK(p.tracks[0].midiClips[1].notes[0].id != p.tracks[0].midiClips[0].notes[0].id);
    Track audio;
    audio.name = "Audio";
    stack.execute(p, std::make_unique<AddTrack>(audio));
    CHECK_FALSE(MoveMidiClip(track, dupPtr->newClipId(), p.tracks[1].id, 0).apply(p));
    stack.execute(p, std::make_unique<MoveMidiClip>(track, dupPtr->newClipId(), track, 5000));
    CHECK(p.tracks[0].midiClips[1].start == 5000);
    stack.execute(p, std::make_unique<RemoveMidiClip>(track, dupPtr->newClipId()));
    CHECK(p.tracks[0].midiClips.size() == 1);

    while (stack.undo(p))
    {
    }
    CHECK(p.tracks.empty());
}

TEST_CASE("A MIDI track's MIDI output setting is undoable and saved", "[core][midi]")
{
    Project p;
    Track t;
    t.id = p.allocateId();
    t.kind = TrackKind::midi;
    p.tracks.push_back(t);
    UndoStack undo;
    undo.execute(p, std::make_unique<SetTrackMidiOutput>(t.id, true, 20)); // clamped
    CHECK(p.tracks[0].midiOut);
    CHECK(p.tracks[0].midiOutChannel == 16);
    const auto loaded = projectFromJson(projectToJson(p));
    REQUIRE(loaded.has_value());
    CHECK(loaded->tracks[0].midiOut);
    CHECK(loaded->tracks[0].midiOutChannel == 16);
    undo.undo(p);
    CHECK_FALSE(p.tracks[0].midiOut);
    CHECK(p.tracks[0].midiOutChannel == 1);
}

TEST_CASE("An instrument has its own slot, first in the chain, and keeps its id when replaced",
          "[core][command][instrument]")
{
    Project p;
    UndoStack stack;
    Track t;
    t.kind = TrackKind::instrument;
    Insert fx;
    fx.type = "northlight.eq";
    t.inserts.push_back(fx);
    stack.execute(p, std::make_unique<AddTrack>(t));
    const Id trackId = p.tracks[0].id;

    Insert synth;
    synth.type = "northlight.synth";
    synth.params = {1.0f, 2.0f};
    stack.execute(p, std::make_unique<SetInstrument>(trackId, synth));
    auto* track = p.findTrack(trackId);
    REQUIRE(track->hasInstrument());
    const Id slot = track->instrument.id;
    CHECK(slot != invalidId);
    CHECK(track->findInsert(slot) == &track->instrument);
    const auto chain = track->processingChain();
    REQUIRE(!chain.empty());
    CHECK(chain.front() == &track->instrument); // the instrument plays first

    // A lane on the instrument goes when another instrument replaces it.
    AutomationLane lane;
    lane.id = p.allocateId();
    lane.target = AutomationTarget::insertParameter;
    lane.insert = slot;
    track->automation.push_back(lane);
    Insert other;
    other.type = "northlight.aurora";
    stack.execute(p, std::make_unique<SetInstrument>(trackId, other));
    track = p.findTrack(trackId);
    CHECK(track->instrument.type == "northlight.aurora");
    CHECK(track->instrument.id == slot); // output tracks stay connected
    CHECK(track->automation.empty());

    CHECK(stack.undo(p));
    track = p.findTrack(trackId);
    CHECK(track->instrument.type == "northlight.synth");
    CHECK(track->automation.size() == 1);

    // Removing empties the slot; undo brings it back.
    stack.execute(p, std::make_unique<SetInstrument>(trackId, Insert{}));
    CHECK_FALSE(p.findTrack(trackId)->hasInstrument());
    CHECK(p.findTrack(trackId)->findInsert(slot) == nullptr);
    CHECK(stack.undo(p));
    CHECK(p.findTrack(trackId)->instrument.id == slot);
}

TEST_CASE("An instrument track is a MIDI track over its instrument channel, added and undone as one",
          "[core][command][instrument]")
{
    Project p;
    UndoStack stack;
    Track channel;
    channel.name = "Aurora";
    channel.instrument.type = "northlight.aurora";
    Track midi;
    midi.name = "MIDI 1";
    auto add = std::make_unique<AddInstrumentTrack>(channel, midi);
    const auto* raw = add.get();
    stack.execute(p, std::move(add));
    const Id channelId = raw->channelId(), midiId = raw->midiTrackId();
    REQUIRE(p.tracks.size() == 2);
    CHECK(p.tracks[0].id == midiId); // the MIDI track above its channel
    CHECK(p.tracks[0].isMidi());
    CHECK(p.tracks[0].midiTarget == channelId);
    CHECK(p.tracks[1].isInstrument());
    CHECK(p.tracks[1].instrument.id != invalidId);
    REQUIRE(p.midiFeeds(channelId).size() == 1);

    // A second MIDI track aimed at the same instrument, on channel 2.
    Track second;
    second.kind = TrackKind::midi;
    stack.execute(p, std::make_unique<AddTrack>(second));
    const Id secondId = p.tracks.back().id;
    stack.execute(p, std::make_unique<SetMidiTarget>(secondId, channelId, 2));
    CHECK(p.findTrack(secondId)->midiChannel == 2);
    CHECK(p.midiFeeds(channelId).size() == 2);
    CHECK(stack.undo(p));
    CHECK(p.findTrack(secondId)->midiTarget == invalidId);
    CHECK(stack.undo(p));
    CHECK(stack.undo(p));
    CHECK(p.tracks.empty());
    CHECK(stack.redo(p));
    REQUIRE(p.tracks.size() == 2);
    CHECK(p.tracks[0].midiTarget == p.tracks[1].id); // the same ids come back
}

TEST_CASE("Undo groups nest: an inner group joins the open one and only the outer end commits",
          "[core][undo]")
{
    Project p;
    UndoStack stack;
    stack.beginGroup("Fader"); // a gesture
    stack.execute(p, std::make_unique<AddBus>(Bus{}));
    stack.beginGroup("Record"); // e.g. a take finishing mid-gesture
    stack.execute(p, std::make_unique<AddBus>(Bus{}));
    stack.endGroup();
    CHECK(stack.groupOpen()); // the gesture is still open
    CHECK_FALSE(stack.canUndo());
    stack.execute(p, std::make_unique<AddBus>(Bus{}));
    stack.endGroup();
    CHECK_FALSE(stack.groupOpen());
    CHECK(p.buses.size() == 3);
    CHECK(stack.undoName() == "Fader");
    CHECK(stack.undo(p));
    CHECK(p.buses.empty()); // one step
    CHECK_FALSE(stack.canUndo());
}

TEST_CASE("Removing a channel detaches what pointed at it, and undo restores every reference",
          "[core][undo][midi]")
{
    Project p;
    UndoStack stack;
    Track channel;
    channel.kind = TrackKind::instrument;
    channel.instrument.type = "northlight.pulsar";
    Track midi;
    midi.kind = TrackKind::midi;
    stack.execute(p, std::make_unique<AddInstrumentTrack>(channel, midi));
    const Id midiId = p.tracks[0].id, channelId = p.tracks[1].id;
    const Id instrumentId = p.tracks[1].instrument.id;
    Track aux;
    aux.kind = TrackKind::aux;
    aux.auxTrack = channelId;
    aux.auxInsert = instrumentId;
    aux.auxOutput = 1;
    stack.execute(p, std::make_unique<AddTrack>(aux));
    const Id auxId = p.tracks.back().id;
    Track audio;
    Insert comp;
    comp.type = "northlight.compressor";
    comp.sidechain = channelId; // keyed from the drums
    audio.inserts.push_back(comp);
    stack.execute(p, std::make_unique<AddTrack>(audio));
    const Id audioId = p.tracks.back().id;
    const Id compId = p.tracks.back().inserts[0].id;
    Bus bus;
    stack.execute(p, std::make_unique<AddBus>(bus));
    const Id busId = p.buses[0].id;
    Insert busKeyed;
    busKeyed.type = "northlight.compressor";
    busKeyed.sidechain = busId;
    stack.execute(p, std::make_unique<AddBus>(Bus{}));
    const Id otherBus = p.buses[1].id;
    stack.execute(p, std::make_unique<AddInsert>(otherBus, busKeyed));
    const Id busKeyedId = p.buses[1].inserts[0].id;

    stack.execute(p, std::make_unique<RemoveTrack>(channelId));
    CHECK(p.findTrack(midiId)->midiTarget == invalidId);
    CHECK(p.findTrack(auxId)->auxTrack == invalidId);
    CHECK(p.findTrack(audioId)->findInsert(compId)->sidechain == invalidId);
    stack.undo(p);
    CHECK(p.findTrack(midiId)->midiTarget == channelId);
    CHECK(p.findTrack(auxId)->auxTrack == channelId);
    CHECK(p.findTrack(audioId)->findInsert(compId)->sidechain == channelId);

    stack.execute(p, std::make_unique<RemoveBus>(busId));
    CHECK(p.findBus(otherBus)->findInsert(busKeyedId)->sidechain == invalidId);
    stack.undo(p);
    CHECK(p.findBus(otherBus)->findInsert(busKeyedId)->sidechain == busId);
}

TEST_CASE("A row height is cosmetic, alone or grouped; other edits are not", "[core][undo]")
{
    Project p;
    UndoStack stack;
    stack.execute(p, std::make_unique<AddTrack>(Track{}));
    const Id id = p.tracks[0].id;
    CHECK_FALSE(stack.undoTop()->isCosmetic());
    stack.execute(p, std::make_unique<SetChannelHeight>(id, 90));
    CHECK(stack.undoTop()->isCosmetic());
    stack.beginGroup("Resize");
    stack.execute(p, std::make_unique<SetChannelHeight>(id, 100));
    stack.endGroup();
    CHECK(stack.undoTop()->isCosmetic());
    stack.beginGroup("Mixed");
    stack.execute(p, std::make_unique<SetChannelHeight>(id, 110));
    stack.execute(p, std::make_unique<SetTrackGain>(id, 0.5f));
    stack.endGroup();
    CHECK_FALSE(stack.undoTop()->isCosmetic());
}

namespace
{
// A project with something for every command to act on.
Project everyCommandFixture()
{
    Project p;
    Track audio;
    audio.id = 1;
    audio.name = "Audio";
    audio.group = 6;
    audio.clips.push_back({10, "a", "a.wav", 0, 1000, 0, 1.0f});
    audio.clips.push_back({11, "b", "a.wav", 1000, 1000, 1000, 1.0f});
    Insert gain;
    gain.id = 20;
    gain.type = "northlight.gain";
    gain.params = {0.5f};
    Insert gain2 = gain;
    gain2.id = 22;
    audio.inserts = {gain, gain2};
    audio.sends.push_back({5, 0.5f, false});
    AutomationLane lane;
    lane.id = 30;
    lane.points = {{0, 1.0f}, {1000, 0.5f}};
    audio.automation.push_back(lane);
    Track midi;
    midi.id = 2;
    midi.name = "MIDI 1";
    midi.kind = TrackKind::midi;
    midi.midiTarget = 3;
    MidiClip clip;
    clip.id = 40;
    clip.start = 0;
    clip.length = 4000;
    clip.notes = {{41, 60, 100, 10, 500}, {42, 64, 90, 1000, 500}};
    midi.midiClips.push_back(clip);
    MidiClip later; // a second clip, after a gap, for the join
    later.id = 43;
    later.start = 6000;
    later.length = 1000;
    later.notes = {{44, 67, 80, 0, 200}};
    midi.midiClips.push_back(later);
    Track channel;
    channel.id = 3;
    channel.name = "Keys";
    channel.kind = TrackKind::instrument;
    channel.instrument.id = 21;
    channel.instrument.type = "northlight.aurora";
    p.tracks = {audio, midi, channel};
    Bus hall;
    hall.id = 5;
    hall.name = "Hall";
    hall.fx = true;
    p.buses.push_back(hall);
    p.groups.push_back(Group{6, "Group", 0});
    p.markers.push_back(Marker{7, "Intro", 0, 0});
    p.nextId = 100;
    return p;
}

using MakeCommand = std::function<std::unique_ptr<Command>()>;

template <typename C, typename... Args>
std::pair<std::string, MakeCommand> make(std::string name, Args... args)
{
    return {std::move(name), [=] { return std::make_unique<C>(args...); }};
}
} // namespace

TEST_CASE("Every command undoes to the exact project it started from and redoes to the same result",
          "[core][undo]")
{
    Track instrumentChannel;
    instrumentChannel.kind = TrackKind::instrument;
    instrumentChannel.instrument.type = "northlight.pulsar";
    Track midiTrack;
    midiTrack.kind = TrackKind::midi;
    MidiClip newClip;
    newClip.start = 5000;
    newClip.length = 100;
    newClip.notes = {{invalidId, 70, 100, 0, 50}};
    Insert eq;
    eq.type = "northlight.eq";
    Insert starbit;
    starbit.type = "northlight.starbit";
    AutomationLane panLane;
    panLane.target = AutomationTarget::pan;
    panLane.points = {{0, 0.5f}};
    const std::vector<std::pair<std::string, std::string>> renames{{"a.wav", "b.wav"}};

    const std::vector<std::pair<std::string, MakeCommand>> commands = {
        make<AddTrack>("AddTrack", Track{}),
        make<AddInstrumentTrack>("AddInstrumentTrack", instrumentChannel, midiTrack),
        make<SetMidiTarget>("SetMidiTarget", Id{2}, invalidId, 3),
        make<RemoveTrack>("RemoveTrack audio", Id{1}),
        make<RemoveTrack>("RemoveTrack instrument", Id{3}),
        make<MoveTrack>("MoveTrack", Id{1}, 2),
        make<AddBus>("AddBus", Bus{}),
        make<RemoveBus>("RemoveBus", Id{5}),
        make<RemoveGroup>("RemoveGroup", Id{6}),
        make<SetChannelGroup>("SetChannelGroup", Id{3}, Id{6}),
        make<SetGroupGain>("SetGroupGain", Id{6}, 0.5f),
        make<SetGroupMute>("SetGroupMute", Id{6}, true),
        make<SetGroupSolo>("SetGroupSolo", Id{6}, true),
        make<SetTrackMidiInput>("SetTrackMidiInput", Id{2}, std::string("Keystation")),
        make<SetTrackMidiOutput>("SetTrackMidiOutput", Id{2}, true, 3),
        make<AddMidiClip>("AddMidiClip", Id{2}, newClip),
        make<RemoveMidiClip>("RemoveMidiClip", Id{2}, Id{40}),
        make<MoveMidiClip>("MoveMidiClip", Id{2}, Id{40}, Id{2}, SamplePosition{500}),
        make<TrimMidiClip>("TrimMidiClip", Id{2}, Id{40}, SamplePosition{0}, SamplePosition{2000}),
        make<SplitMidiClip>("SplitMidiClip", Id{2}, Id{40}, SamplePosition{800}),
        make<DuplicateMidiClip>("DuplicateMidiClip", Id{2}, Id{40}, SamplePosition{-1}),
        make<JoinMidiClips>("JoinMidiClips", Id{2}, Id{40}, Id{43}),
        make<SetClipMute>("SetClipMute MIDI", Id{2}, Id{40}, true, true),
        make<AddNote>("AddNote", Id{2}, Id{40}, MidiNote{invalidId, 67, 100, 0, 100}),
        make<RemoveNote>("RemoveNote", Id{2}, Id{40}, Id{41}),
        make<SetNote>("SetNote", Id{2}, Id{40}, MidiNote{41, 61, 90, 20, 100}),
        make<QuantiseNotes>(
            "QuantiseNotes", Id{2}, Id{40}, std::vector<Id>{41, 42}, SamplePosition{480}, 1.0f),
        make<AddMarker>("AddMarker", Marker{invalidId, "B", 500, 0}),
        make<RemoveMarker>("RemoveMarker", Id{7}),
        make<SetMarker>("SetMarker", Id{7}, SamplePosition{100}, SamplePosition{0}, std::string("X")),
        make<SetOutputBus>("SetOutputBus", Id{1}, Id{5}),
        make<SetSend>("SetSend", Id{3}, Id{5}, 0.5f, true),
        make<RemoveSend>("RemoveSend", Id{1}, Id{5}),
        make<SetPhaseInvert>("SetPhaseInvert", Id{1}, true),
        make<SetColour>("SetColour", Id{1}, std::uint32_t{0xff112233u}),
        make<SetChannelHeight>("SetChannelHeight", Id{1}, 90),
        make<SetMasterGain>("SetMasterGain", 0.5f),
        make<AddInsert>("AddInsert", Id{1}, eq, 0),
        make<RemoveInsert>("RemoveInsert", Id{1}, Id{20}),
        make<SetInstrument>("SetInstrument", Id{3}, starbit),
        make<MoveInsert>("MoveInsert", Id{1}, Id{20}, 1),
        make<SetInsertParameter>("SetInsertParameter", Id{1}, Id{20}, 0, 0.9f),
        make<SetInsertMix>("SetInsertMix", Id{1}, Id{20}, 0.5f),
        make<SetInsertSidechain>("SetInsertSidechain", Id{1}, Id{20}, Id{3}),
        make<SetInsertBypass>("SetInsertBypass", Id{1}, Id{20}, true),
        make<SetStripPosition>("SetStripPosition", Id{1}, true),
        make<AddAutomationLane>("AddAutomationLane", Id{1}, panLane),
        make<RemoveAutomationLane>("RemoveAutomationLane", Id{1}, Id{30}),
        make<SetAutomationPoints>("SetAutomationPoints",
                                  Id{1},
                                  Id{30},
                                  std::vector<AutomationPoint>{{5, 0.2f}},
                                  std::string("Edit automation")),
        make<SetAutomationMode>("SetAutomationMode", Id{1}, AutomationMode::touch),
        make<SetAutomationOpen>("SetAutomationOpen", Id{1}, true),
        make<SetAutomationLaneView>("SetAutomationLaneView",
                                    Id{1},
                                    Id{30},
                                    AutomationLaneView{72, true, false},
                                    std::string("Resize automation lane")),
        make<SetAutomationLaneTarget>(
            "SetAutomationLaneTarget", Id{1}, Id{30}, AutomationTarget::insertParameter, Id{20}, 0),
        make<SetTrackName>("SetTrackName", Id{1}, std::string("Renamed")),
        make<SetTrackGain>("SetTrackGain", Id{1}, 0.3f),
        make<SetTrackPan>("SetTrackPan", Id{1}, -0.5f),
        make<SetTrackMute>("SetTrackMute", Id{1}, true),
        make<SetTrackInput>("SetTrackInput", Id{1}, 2),
        make<SetTrackArmed>("SetTrackArmed", Id{1}, true),
        make<SetTrackMonitor>("SetTrackMonitor", Id{1}, MonitorMode::on),
        make<SetTrackSolo>("SetTrackSolo", Id{1}, true),
        make<AddClip>("AddClip", Id{1}, AudioClip{invalidId, "c", "c.wav", 5000, 100, 0, 1.0f}),
        make<RemoveClip>("RemoveClip", Id{1}, Id{10}),
        make<MoveClip>("MoveClip", Id{1}, Id{10}, Id{1}, SamplePosition{5000}),
        make<TrimClip>(
            "TrimClip", Id{1}, Id{10}, SamplePosition{100}, SamplePosition{500}, SamplePosition{100}),
        make<SetClipFades>("SetClipFades", Id{1}, Id{10}, SamplePosition{50}, SamplePosition{50}),
        make<SplitClip>("SplitClip", Id{1}, Id{10}, SamplePosition{500}),
        make<JoinClips>("JoinClips", Id{1}, Id{10}, Id{11}),
        make<DuplicateClip>("DuplicateClip", Id{1}, Id{10}, SamplePosition{-1}),
        make<ShiftClips>("ShiftClips", Id{1}, SamplePosition{0}, SamplePosition{100}),
        make<SetClipGain>("SetClipGain", Id{1}, Id{10}, 0.5f),
        make<SetClipMute>("SetClipMute audio", Id{1}, Id{10}, false, true),
        make<SetMediaPaths>("SetMediaPaths", renames),
        make<SetTimeSignature>("SetTimeSignature", 3, 4),
        make<SetTempo>("SetTempo", 100.0),
    };
    for (const auto& [name, makeCommand] : commands)
    {
        INFO(name);
        const Project before = everyCommandFixture();
        Project p = before;
        UndoStack stack;
        REQUIRE_NOTHROW(stack.execute(p, makeCommand()));
        const Project after = p;
        CHECK_FALSE(after == before); // it did something
        // Undo keeps nextId: an id handed out is never handed out again, so a
        // redo (or anything after it) cannot collide with one the history holds.
        const auto sameAsBefore = [&]
        {
            Project q = p;
            CHECK(q.nextId >= before.nextId);
            q.nextId = before.nextId;
            return q == before;
        };
        REQUIRE(stack.undo(p));
        CHECK(sameAsBefore());
        REQUIRE(stack.redo(p));
        CHECK(p == after); // the same ids come back
        REQUIRE(stack.undo(p));
        CHECK(sameAsBefore());
    }
}

TEST_CASE("A send's level is a live value; a new send or a pre/post switch changes the graph", "[core][undo]")
{
    Project p;
    Track t;
    t.id = 1;
    p.tracks.push_back(t);
    Bus fx;
    fx.id = 2;
    fx.fx = true;
    p.buses.push_back(fx);
    p.nextId = 10;
    SetSend added(1, 2, 0.5f, false);
    added.apply(p);
    CHECK(added.changeKind() == Command::ChangeKind::structure);
    SetSend level(1, 2, 0.25f, false);
    level.apply(p);
    CHECK(level.changeKind() == Command::ChangeKind::values);
    SetSend tap(1, 2, 0.25f, true);
    tap.apply(p);
    CHECK(tap.changeKind() == Command::ChangeKind::structure);
}

TEST_CASE("Controller events are edited in bulk, carried by trim, split and duplicate, and undone",
          "[core][command][midi]")
{
    Project p;
    Track t;
    t.kind = TrackKind::midi;
    UndoStack stack;
    stack.execute(p, std::make_unique<AddTrack>(t));
    const Id track = p.tracks[0].id;
    MidiClip clip;
    clip.start = 1000;
    clip.length = 2000;
    clip.controllers.push_back({invalidId, MidiControllerType::controller, 1, 200, 500}); // clamped to 127
    clip.controllers.push_back({invalidId, MidiControllerType::pitchBend, 0, 4000, 100});
    auto add = std::make_unique<AddMidiClip>(track, clip);
    const auto* addPtr = add.get();
    stack.execute(p, std::move(add));
    const Id clipId = addPtr->clipId();
    const auto& stored = [&]() -> const MidiClip& { return *p.tracks[0].findMidiClip(clipId); };
    REQUIRE(stored().controllers.size() == 2);
    CHECK(stored().controllers[0].type == MidiControllerType::pitchBend); // sorted by time
    CHECK(stored().controllers[1].value == 127);
    CHECK(stored().controllers[0].id != invalidId);
    const Id bend = stored().controllers[0].id;
    const Id mod = stored().controllers[1].id;

    // One step: move the bend, add two points, delete the mod wheel point.
    std::vector<MidiControllerEvent> events;
    events.push_back({bend, MidiControllerType::pitchBend, 0, -9000, 150});
    events.push_back({invalidId, MidiControllerType::controller, 11, 90, 1200});
    events.push_back({invalidId, MidiControllerType::channelPressure, 0, 30, 50});
    auto edit = std::make_unique<EditMidiControllers>(track, clipId, events, std::vector<Id>{mod}, "Draw");
    const auto* editPtr = edit.get();
    stack.execute(p, std::move(edit));
    CHECK(editPtr->name() == "Draw");
    REQUIRE(stored().controllers.size() == 3);
    CHECK(stored().controllers[0].type == MidiControllerType::channelPressure);
    CHECK(stored().controllers[1].id == bend);
    CHECK(stored().controllers[1].value == -8192); // clamped
    CHECK(stored().controllers[1].time == 150);
    CHECK(stored().findController(mod) == nullptr);
    const Id added = editPtr->events()[1].id;
    CHECK(added != invalidId);
    stack.undo(p);
    REQUIRE(stored().controllers.size() == 2);
    CHECK(stored().findController(bend)->value == 4000);
    stack.redo(p);
    CHECK(stored().findController(added) != nullptr); // the same id on redo
    stack.undo(p);

    // Trim keeps the events on the timeline (an event before the new start keeps
    // its place, before the clip).
    stack.execute(p, std::make_unique<TrimMidiClip>(track, clipId, 1300, 1700));
    CHECK(stored().findController(mod)->time == 200);
    CHECK(stored().findController(bend)->time == -200);
    stack.undo(p);
    CHECK(stored().findController(bend)->time == 100);

    // Split at 1300: the bend (at 100) stays left; the mod wheel (at 500) goes right
    // at 200; the right part opens with the bend's value.
    auto split = std::make_unique<SplitMidiClip>(track, clipId, 1300);
    const auto* splitPtr = split.get();
    stack.execute(p, std::move(split));
    const auto* right = p.tracks[0].findMidiClip(splitPtr->rightClipId());
    REQUIRE(right != nullptr);
    REQUIRE(stored().controllers.size() == 1);
    CHECK(stored().controllers[0].id == bend);
    REQUIRE(right->controllers.size() == 2);
    CHECK(right->controllers[0].type == MidiControllerType::pitchBend);
    CHECK(right->controllers[0].time == 0);
    CHECK(right->controllers[0].value == 4000);
    CHECK(right->controllers[0].id != bend);
    const Id opening = right->controllers[0].id;
    CHECK(right->controllers[1].id == mod);
    CHECK(right->controllers[1].time == 200);
    stack.undo(p);
    CHECK(stored().controllers.size() == 2);
    stack.redo(p);
    CHECK(p.tracks[0].findMidiClip(splitPtr->rightClipId())->controllers[0].id == opening); // stable ids
    stack.undo(p);

    // Duplicate copies them with fresh ids.
    auto dup = std::make_unique<DuplicateMidiClip>(track, clipId);
    const auto* dupPtr = dup.get();
    stack.execute(p, std::move(dup));
    const auto* copy = p.tracks[0].findMidiClip(dupPtr->newClipId());
    REQUIRE(copy->controllers.size() == 2);
    CHECK(copy->controllers[0].id != bend);
    CHECK(copy->controllers[0].value == 4000);
    while (stack.undo(p))
    {
    }
    CHECK(p.tracks.empty());
}

TEST_CASE("A stereo input bus replaces a track's input channel; undo and redo restore each",
          "[core][command]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    const Id id = p.tracks[0].id;
    CHECK_FALSE(p.tracks[0].hasAudioInput());

    stack.execute(p, std::make_unique<SetTrackInput>(id, 2));
    stack.execute(p, std::make_unique<SetTrackInput>(id, 5, "Vocal pair")); // the channel is dropped
    CHECK(p.tracks[0].inputBus == "Vocal pair");
    CHECK(p.tracks[0].inputChannel == -1);
    CHECK(p.tracks[0].hasAudioInput());

    REQUIRE(stack.undo(p));
    CHECK(p.tracks[0].inputBus.empty());
    CHECK(p.tracks[0].inputChannel == 2);
    REQUIRE(stack.redo(p));
    CHECK(p.tracks[0].inputBus == "Vocal pair");
    CHECK(p.tracks[0].inputChannel == -1);

    // Back to a single input clears the bus.
    stack.execute(p, std::make_unique<SetTrackInput>(id, 1));
    CHECK(p.tracks[0].inputBus.empty());
    CHECK(p.tracks[0].inputChannel == 1);
    REQUIRE(stack.undo(p));
    CHECK(p.tracks[0].inputBus == "Vocal pair");
}

TEST_CASE("The master takes inserts like any channel: add, move and remove undo and redo",
          "[core][command][master]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    REQUIRE(p.findChannel(masterId) == &p.master);
    Insert a;
    a.type = "northlight.glue";
    Insert b;
    b.type = "northlight.limiter";
    stack.execute(p, std::make_unique<AddInsert>(masterId, a));
    stack.execute(p, std::make_unique<AddInsert>(masterId, b));
    REQUIRE(p.master.inserts.size() == 2);
    const Id glue = p.master.inserts[0].id;
    CHECK(glue != invalidId);
    CHECK(glue != masterId);

    stack.execute(p, std::make_unique<MoveInsert>(masterId, glue, 1));
    CHECK(p.master.inserts[1].id == glue);
    stack.execute(p, std::make_unique<RemoveInsert>(masterId, glue));
    CHECK(p.master.inserts.size() == 1);

    REQUIRE(stack.undo(p));
    CHECK(p.master.inserts.size() == 2);
    CHECK(p.master.inserts[1].id == glue);
    REQUIRE(stack.undo(p));
    CHECK(p.master.inserts[0].id == glue);
    REQUIRE(stack.redo(p));
    CHECK(p.master.inserts[1].id == glue);
    while (stack.undo(p))
    {
    }
    CHECK(p.master.inserts.empty());
}

TEST_CASE("A freeze renders without the master's inserts", "[core][master][freeze]")
{
    Project p = projectWithTrack();
    Insert glue;
    glue.id = 99;
    glue.type = "northlight.glue";
    p.master.inserts.push_back(glue);
    const auto copy = freezeRenderProject(p, p.tracks[0].id);
    CHECK(copy.master.inserts.empty());
    CHECK(p.master.inserts.size() == 1);
}

TEST_CASE("Adding an automation lane opens the channel's lanes, and undo closes them again",
          "[core][command][automation]")
{
    Project p;
    Track t;
    t.id = 1;
    p.tracks.push_back(t);
    p.nextId = 10;
    UndoStack stack;
    AutomationLane pan;
    pan.target = AutomationTarget::pan;
    stack.execute(p, std::make_unique<AddAutomationLane>(Id{1}, pan));
    REQUIRE(p.tracks[0].automation.size() == 1);
    CHECK(p.tracks[0].automation[0].points.empty()); // a lane may have no points
    CHECK(p.tracks[0].automationOpen);
    REQUIRE(stack.undo(p));
    CHECK(p.tracks[0].automation.empty());
    CHECK_FALSE(p.tracks[0].automationOpen);
    REQUIRE(stack.redo(p));
    CHECK(p.tracks[0].automationOpen);
}

TEST_CASE("Opening lanes and a lane's view are cosmetic: undoable, but no engine work",
          "[core][command][automation]")
{
    Project p;
    Track t;
    t.id = 1;
    AutomationLane lane;
    lane.id = 5;
    lane.points = {{0, 1.0f}};
    t.automation.push_back(lane);
    p.tracks.push_back(t);
    UndoStack stack;

    auto open = std::make_unique<SetAutomationOpen>(Id{1}, true);
    CHECK(open->changeKind() == Command::ChangeKind::cosmetic);
    stack.execute(p, std::move(open));
    CHECK(p.tracks[0].automationOpen);

    auto hide = std::make_unique<SetAutomationLaneView>(Id{1}, Id{5}, AutomationLaneView{60, false, true});
    CHECK(hide->changeKind() == Command::ChangeKind::cosmetic);
    stack.execute(p, std::move(hide));
    CHECK(p.tracks[0].automation[0].view == AutomationLaneView{60, false, true});
    CHECK(p.tracks[0].automation[0].points.size() == 1); // hiding keeps the automation

    REQUIRE(stack.undo(p));
    CHECK(p.tracks[0].automation[0].view == AutomationLaneView{});
    REQUIRE(stack.undo(p));
    CHECK_FALSE(p.tracks[0].automationOpen);
    REQUIRE(stack.redo(p));
    REQUIRE(stack.redo(p));
    CHECK(p.tracks[0].automation[0].view.hidden);
}

TEST_CASE("A lane can be pointed at another parameter, but not at one that already has a lane",
          "[core][command][automation]")
{
    Project p;
    Track t;
    t.id = 1;
    Insert eq;
    eq.id = 7;
    eq.type = "northlight.eq";
    t.inserts.push_back(eq);
    AutomationLane volume;
    volume.id = 5;
    AutomationLane pan;
    pan.id = 6;
    pan.target = AutomationTarget::pan;
    t.automation = {volume, pan};
    p.tracks.push_back(t);
    UndoStack stack;

    stack.execute(
        p,
        std::make_unique<SetAutomationLaneTarget>(Id{1}, Id{5}, AutomationTarget::insertParameter, Id{7}, 3));
    CHECK(p.tracks[0].automation[0].target == AutomationTarget::insertParameter);
    CHECK(p.tracks[0].automation[0].insert == 7);
    CHECK(p.tracks[0].automation[0].parameter == 3);
    REQUIRE(stack.undo(p));
    CHECK(p.tracks[0].automation[0].target == AutomationTarget::gain);
    CHECK(p.tracks[0].automation[0].insert == invalidId);

    SetAutomationLaneTarget clash(Id{1}, Id{5}, AutomationTarget::pan, invalidId, 0);
    CHECK_FALSE(clash.apply(p));
}

TEST_CASE("Muting a clip keeps it on the timeline; undo and redo restore the state", "[core][command]")
{
    Project p;
    Track audio;
    audio.id = 1;
    audio.clips.push_back({10, "a", "a.wav", 0, 1000, 0, 1.0f});
    Track midi;
    midi.id = 2;
    midi.kind = TrackKind::midi;
    MidiClip clip;
    clip.id = 20;
    clip.length = 1000;
    midi.midiClips.push_back(clip);
    p.tracks = {audio, midi};
    p.nextId = 30;
    UndoStack stack;

    auto mute = std::make_unique<SetClipMute>(1, 10, false, true);
    CHECK(mute->name() == "Mute clip");
    stack.execute(p, std::move(mute));
    CHECK(p.tracks[0].clips[0].muted);
    CHECK(p.tracks[0].clips.size() == 1);
    stack.execute(p, std::make_unique<SetClipMute>(2, 20, true, true));
    CHECK(p.tracks[1].midiClips[0].muted);

    stack.undo(p);
    CHECK_FALSE(p.tracks[1].midiClips[0].muted);
    stack.undo(p);
    CHECK_FALSE(p.tracks[0].clips[0].muted);
    stack.redo(p);
    CHECK(p.tracks[0].clips[0].muted);
    CHECK(SetClipMute(1, 10, false, false).name() == "Unmute clip");
    CHECK_FALSE(SetClipMute(1, 20, false, true).apply(p)); // 20 is a MIDI clip, not an audio one
}

TEST_CASE("Joining MIDI clips moves the later clip's notes and events into the earlier, across the gap",
          "[core][command][midi]")
{
    Project p;
    Track midi;
    midi.id = 1;
    midi.kind = TrackKind::midi;
    MidiClip left;
    left.id = 10;
    left.start = 1000;
    left.length = 1000;
    left.notes = {{11, 60, 100, 100, 200}};
    MidiClip right;
    right.id = 20;
    right.start = 3000;
    right.length = 500;
    right.notes = {{21, 64, 90, 50, 100}};
    right.controllers = {{22, MidiControllerType::controller, 1, 64, 10}};
    midi.midiClips = {left, right};
    p.tracks = {midi};
    p.nextId = 30;
    UndoStack stack;

    REQUIRE(JoinMidiClips::canJoin(left, right));
    CHECK_FALSE(JoinMidiClips::canJoin(right, left)); // the right must not start first
    stack.execute(p, std::make_unique<JoinMidiClips>(1, 10, 20));
    REQUIRE(p.tracks[0].midiClips.size() == 1);
    const auto& joined = p.tracks[0].midiClips[0];
    CHECK(joined.id == 10);
    CHECK(joined.start == 1000);
    CHECK(joined.length == 2500); // to the right clip's end, the gap included
    REQUIRE(joined.notes.size() == 2);
    CHECK(joined.notes[1].id == 21);
    CHECK(joined.notes[1].start == 2050); // 3000 + 50, now relative to 1000
    REQUIRE(joined.controllers.size() == 1);
    CHECK(joined.controllers[0].time == 2010);

    stack.undo(p);
    REQUIRE(p.tracks[0].midiClips.size() == 2);
    CHECK(p.tracks[0].midiClips[0] == left);
    CHECK(p.tracks[0].midiClips[1] == right);
    stack.redo(p);
    CHECK(p.tracks[0].midiClips.size() == 1);

    // A muted clip joins only another muted clip, so a join never changes what plays.
    right.muted = true;
    CHECK_FALSE(JoinMidiClips::canJoin(left, right));
    AudioClip a{1, "a", "a.wav", 0, 100, 0, 1.0f};
    AudioClip b{2, "b", "a.wav", 100, 100, 100, 1.0f};
    CHECK(JoinClips::canJoin(a, b));
    b.muted = true;
    CHECK_FALSE(JoinClips::canJoin(a, b));
}

TEST_CASE("Fade curves are set with their lengths, kept when not given, and undone", "[core][command]")
{
    Project p;
    Track audio;
    audio.id = 1;
    audio.clips.push_back({10, "a", "a.wav", 0, 1000, 0, 1.0f});
    p.tracks = {audio};
    p.nextId = 20;
    UndoStack stack;
    stack.execute(p, std::make_unique<SetClipFades>(1, 10, 100, 200, 0.5f, -2.0f));
    const auto& c = p.tracks[0].clips[0];
    CHECK(c.fadeIn == 100);
    CHECK(c.fadeInCurve == 0.5f);
    CHECK(c.fadeOutCurve == -1.0f);                                    // clamped
    stack.execute(p, std::make_unique<SetClipFades>(1, 10, 300, 200)); // lengths only
    CHECK(p.tracks[0].clips[0].fadeIn == 300);
    CHECK(p.tracks[0].clips[0].fadeInCurve == 0.5f);
    stack.undo(p);
    stack.undo(p);
    CHECK(p.tracks[0].clips[0].fadeInCurve == 0.0f);
    CHECK(p.tracks[0].clips[0].fadeOutCurve == 0.0f);
    stack.redo(p);
    CHECK(p.tracks[0].clips[0].fadeInCurve == 0.5f);

    // A split gives the right part the fade-out and its curve; a join takes them back.
    auto split = std::make_unique<SplitClip>(1, 10, 500);
    auto* splitPtr = split.get();
    stack.execute(p, std::move(split));
    CHECK(p.tracks[0].findClip(splitPtr->rightClipId())->fadeOutCurve == -1.0f);
    stack.execute(p, std::make_unique<JoinClips>(1, 10, splitPtr->rightClipId()));
    CHECK(p.tracks[0].clips[0].fadeOutCurve == -1.0f);
}

TEST_CASE("A setter merges the next setter of the same field and target", "[core][command][undo]")
{
    Project p = projectWithTrack();
    const Id id = p.tracks[0].id;
    SetTrackGain first(id, 0.5f);
    SetTrackGain second(id, 0.25f);
    first.apply(p);
    second.apply(p);
    CHECK(first.mergeWith(second));
    CHECK(first.value() == 0.25f);
    first.revert(p);
    CHECK(p.tracks[0].gain == 1.0f); // the first previous value
    first.apply(p);
    CHECK(p.tracks[0].gain == 0.25f); // the last value

    SetTrackGain other(masterId, 0.5f);
    SetMasterGain master(0.5f);
    SetTrackPan pan(id, 0.5f);
    CHECK_FALSE(first.mergeWith(other));  // another target
    CHECK_FALSE(other.mergeWith(master)); // another command, same field
    CHECK_FALSE(first.mergeWith(pan));    // another field

    SetInsertParameter a(id, 20, 1, 0.5f);
    SetInsertParameter b(id, 20, 1, 0.7f);
    SetInsertParameter c(id, 20, 2, 0.7f);
    CHECK(a.mergeWith(b));
    CHECK(a.value() == 0.7f);
    CHECK_FALSE(a.mergeWith(c));
}

TEST_CASE("A merged drag undoes and redoes like the ticks it replaced", "[core][command][undo]")
{
    Project p = projectWithTrack();
    const Id id = p.tracks[0].id;
    p.tracks[0].automation.push_back({7, AutomationTarget::gain, invalidId, 0, {{0, 1.0f}}});
    const Project before = p;
    UndoStack stack;

    stack.beginGroup("Drag");
    for (int tick = 1; tick <= 600; ++tick)
        stack.execute(p, std::make_unique<SetTrackGain>(id, 1.0f - static_cast<float>(tick) / 1000.0f));
    for (int tick = 1; tick <= 600; ++tick)
        stack.execute(p,
                      std::make_unique<SetAutomationPoints>(
                          id, 7, std::vector<AutomationPoint>{{0, 1.0f}, {tick, 0.5f}}));
    stack.execute(p, std::make_unique<SetTrackPan>(id, -0.5f));
    stack.execute(p, std::make_unique<SetTrackGain>(id, 0.1f)); // after a pan: its own member
    stack.endGroup();
    const Project after = p;
    CHECK(p.tracks[0].gain == 0.1f);
    CHECK(p.tracks[0].pan == -0.5f);
    CHECK(p.tracks[0].automation[0].points.back().position == 600);

    REQUIRE(stack.undo(p));
    CHECK(p == before);
    REQUIRE(stack.redo(p));
    CHECK(p == after);
    REQUIRE(stack.undo(p));
    CHECK(p == before);
}

TEST_CASE("A stale command fails and leaves the project as it was", "[core][command][undo]")
{
    Project p = projectWithTrack();
    const Id track = p.tracks[0].id;
    AudioClip clip;
    clip.length = 100;
    UndoStack stack;
    REQUIRE(stack.execute(p, std::make_unique<AddClip>(track, clip)));
    const Id clipId = p.tracks[0].clips[0].id;
    const Project before = p;

    // The destination is gone: the clip stays where it is.
    CHECK_FALSE(stack.execute(p, std::make_unique<MoveClip>(track, clipId, Id{999}, 50)));
    CHECK_FALSE(stack.execute(p, std::make_unique<SetTrackGain>(Id{999}, 0.5f)));
    CHECK_FALSE(stack.execute(p, std::make_unique<SetClipGain>(track, Id{999}, 0.5f)));
    CHECK(p == before);
    CHECK(stack.undoName() == "Add clip");
}

namespace
{
// Adds one to the tempo, and says it keeps `bytes` for undo.
class Sized final : public Command
{
public:
    explicit Sized(std::size_t bytes) : bytes_(bytes) {}
    std::string name() const override { return "Sized"; }
    bool apply(Project& p) override
    {
        p.tempoBpm += 1.0;
        return true;
    }
    bool revert(Project& p) override
    {
        p.tempoBpm -= 1.0;
        return true;
    }
    std::size_t footprint() const noexcept override { return bytes_; }

private:
    std::size_t bytes_;
};
} // namespace

TEST_CASE("The undo history drops its oldest steps past its step limit", "[core][command][undo]")
{
    Project p;
    p.tempoBpm = 100.0;
    UndoStack undo;
    undo.setLimits(3, UndoStack::defaultMaxBytes);
    for (int i = 0; i < 5; ++i)
        undo.execute(p, std::make_unique<Sized>(10));
    CHECK(undo.undoCount() == 3);
    CHECK(undo.historyBytes() == 30);
    while (undo.undo(p))
        ;
    CHECK(p.tempoBpm == 102.0); // the two oldest can no longer be undone
    CHECK(undo.canRedo());
}

TEST_CASE("The undo history drops its oldest steps past its memory limit, never the newest",
          "[core][command][undo]")
{
    Project p;
    UndoStack undo;
    undo.setLimits(100, 250);
    undo.execute(p, std::make_unique<Sized>(100));
    undo.execute(p, std::make_unique<Sized>(100));
    CHECK(undo.undoCount() == 2);
    undo.execute(p, std::make_unique<Sized>(100));
    CHECK(undo.undoCount() == 2);
    CHECK(undo.historyBytes() == 200);
    undo.execute(p, std::make_unique<Sized>(1000)); // alone past the limit: kept
    CHECK(undo.undoCount() == 1);
    CHECK(undo.historyBytes() == 1000);

    // Undone steps are freed when a new command replaces them.
    undo.setLimits(100, 10000);
    undo.execute(p, std::make_unique<Sized>(100));
    REQUIRE(undo.undo(p));
    undo.execute(p, std::make_unique<Sized>(50));
    CHECK(undo.historyBytes() == 1050);
}

TEST_CASE("A save point dropped from the history can no longer be reached", "[core][command][undo]")
{
    Project p;
    UndoStack undo;
    undo.setLimits(3, UndoStack::defaultMaxBytes);
    undo.execute(p, std::make_unique<Sized>(10));
    undo.execute(p, std::make_unique<Sized>(10));
    undo.markSaved(); // after two steps
    undo.execute(p, std::make_unique<Sized>(10));
    undo.execute(p, std::make_unique<Sized>(10)); // drops the first: the save point is one deep
    REQUIRE(undo.undoCount() == 3);
    CHECK_FALSE(undo.atSavePoint());
    undo.undo(p);
    undo.undo(p);
    CHECK(undo.atSavePoint());
    undo.redo(p);
    undo.redo(p);

    undo.execute(p, std::make_unique<Sized>(10)); // the save point is the oldest state left
    undo.execute(p, std::make_unique<Sized>(10)); // and now it is gone
    while (undo.canUndo())
    {
        CHECK_FALSE(undo.atSavePoint());
        undo.undo(p);
    }
    CHECK_FALSE(undo.atSavePoint());
}

TEST_CASE("Commands that copy tracks or clips report what they keep", "[core][command][undo]")
{
    Project p = projectWithTrack();
    const Id id = p.tracks[0].id;
    AudioClip clip;
    clip.length = 10;
    clip.vocal.notes.resize(1000);
    p.tracks[0].clips.push_back(clip);
    RemoveTrack remove(id);
    const auto before = remove.footprint();
    REQUIRE(remove.apply(p));
    CHECK(remove.footprint() >= before + 1000 * sizeof(VocalNote));
    CHECK(SetTrackGain(id, 0.5f).footprint() < 1024);
}
