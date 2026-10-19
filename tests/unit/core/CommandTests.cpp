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
    CHECK_FALSE(stack.canRedo());
    CHECK(p.tempoBpm == 90.0);
}
