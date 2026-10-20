#include "core/Command.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace aural::core;

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
    stack.execute(p, std::make_unique<SetTrackGain>(bus, 0.7f)); // fader commands work on buses
    stack.execute(p, std::make_unique<SetMasterGain>(0.9f));
    CHECK(p.tracks[0].outputBus == bus);
    REQUIRE(p.tracks[0].sends.size() == 1);
    CHECK(p.tracks[0].sends[0] == Send{bus, 0.25f, false});
    CHECK(p.tracks[0].phaseInvert);
    CHECK(p.tracks[0].colour == 0xff336699u);
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
    CHECK(p.master.gain == 1.0f);

    CHECK_THROWS(SetOutputBus(track, 999).apply(p));
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
    CHECK_THROWS(stack.execute(p, std::make_unique<MoveTrack>(Id{999999}, 0)));
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

    CHECK_THROWS(SetChannelGroup(b, 999).apply(p));
    CHECK_THROWS(SetGroupGain(999, 1.0f).apply(p));
}

TEST_CASE("An insert's wet/dry mix is set, clamped and undone", "[core][command]")
{
    Project p = projectWithTrack();
    UndoStack stack;
    const Id track = p.tracks[0].id;
    Insert insert;
    insert.type = "aural.eq";
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
    insert.type = "aural.compressor";
    auto add = std::make_unique<AddInsert>(track, insert);
    const auto* addPtr = add.get();
    stack.execute(p, std::move(add));
    const Id id = addPtr->insertId();
    CHECK(p.tracks[0].inserts[0].sidechain == invalidId);
    stack.execute(p, std::make_unique<SetInsertSidechain>(track, id, kick));
    CHECK(p.tracks[0].inserts[0].sidechain == kick);
    CHECK_THROWS(SetInsertSidechain(track, id, 999).apply(p));
    // A loop: keying the track from itself, or from a bus it feeds (directly or
    // through another key), is refused.
    CHECK_THROWS(SetInsertSidechain(track, id, track).apply(p));
    Bus bus;
    bus.name = "Drums";
    stack.execute(p, std::make_unique<AddBus>(bus));
    const Id drums = p.buses[0].id;
    stack.execute(p, std::make_unique<SetOutputBus>(track, drums));
    CHECK(p.keyWouldCycle(track, drums));
    CHECK_THROWS(SetInsertSidechain(track, id, drums).apply(p));
    // The kick feeds the track's key, which feeds the bus: keying the kick from
    // the bus would loop; without that key it would not.
    CHECK(p.keyWouldCycle(kick, drums));
    stack.execute(p, std::make_unique<SetInsertSidechain>(track, id, invalidId));
    CHECK_FALSE(p.keyWouldCycle(kick, drums));
    // The kick keyed from the track: now the track must not be keyed from the kick.
    Insert kickInsert;
    kickInsert.type = "aural.compressor";
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
    CHECK_THROWS(SplitClip(t, left, 1000).apply(p)); // not strictly inside

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
    CHECK_THROWS(CrossfadeClips(t, left, copy).apply(p));
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
    CHECK_THROWS(RemoveMarker(999).apply(p));
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
    CHECK_THROWS(MoveMidiClip(track, dupPtr->newClipId(), p.tracks[1].id, 0).apply(p));
    stack.execute(p, std::make_unique<MoveMidiClip>(track, dupPtr->newClipId(), track, 5000));
    CHECK(p.tracks[0].midiClips[1].start == 5000);
    stack.execute(p, std::make_unique<RemoveMidiClip>(track, dupPtr->newClipId()));
    CHECK(p.tracks[0].midiClips.size() == 1);

    while (stack.undo(p))
    {
    }
    CHECK(p.tracks.empty());
}
