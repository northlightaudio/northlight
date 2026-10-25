#include "core/Command.h"
#include "core/Model.h"
#include "core/ProjectJson.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight::core;

namespace
{
Project threeTracks()
{
    Project p;
    for (const char* name : {"Kick", "Snare", "Bass"})
    {
        Track t;
        t.id = p.allocateId();
        t.name = name;
        p.tracks.push_back(t);
    }
    return p;
}
} // namespace

TEST_CASE("A folder track gathers its tracks after it and undoes cleanly", "[core][folder]")
{
    auto p = threeTracks();
    const auto kick = p.tracks[0].id, snare = p.tracks[1].id, bass = p.tracks[2].id;
    const auto original = p;
    UndoStack stack;
    auto add = std::make_unique<AddFolderTrack>("Drums", std::vector<Id>{snare, kick});
    auto* command = add.get();
    stack.execute(p, std::move(add));
    const auto folder = command->folderId();
    REQUIRE(p.tracks.size() == 4);
    CHECK(p.tracks[0].id == folder);
    CHECK(p.tracks[0].isFolder());
    CHECK_FALSE(p.tracks[0].hasAudio());
    CHECK(p.tracks[1].id == kick); // members keep their order
    CHECK(p.tracks[2].id == snare);
    CHECK(p.tracks[3].id == bass);
    CHECK(p.tracks[1].folder == folder);
    CHECK(p.folderDepth(p.tracks[1]) == 1);
    CHECK(p.folderOf(p.tracks[3]) == nullptr);
    stack.undo(p);
    CHECK(p.tracks == original.tracks); // (undo never hands an allocated id back)
    stack.redo(p);
    CHECK(p.tracks[0].id == folder);
}

TEST_CASE("Folders nest, hide, mute and solo what they hold", "[core][folder]")
{
    auto p = threeTracks();
    UndoStack stack;
    auto outer = std::make_unique<AddFolderTrack>("All", std::vector<Id>{p.tracks[0].id, p.tracks[1].id});
    auto* outerCommand = outer.get();
    stack.execute(p, std::move(outer));
    const auto outerId = outerCommand->folderId();
    auto inner = std::make_unique<AddFolderTrack>("Kick only", std::vector<Id>{p.tracks[1].id});
    auto* innerCommand = inner.get();
    stack.execute(p, std::move(inner));
    const auto innerId = innerCommand->folderId();
    const auto* kick = p.findTrack(p.tracks[2].id);
    REQUIRE(kick != nullptr);
    CHECK(p.findTrack(innerId)->folder == outerId); // nested where its member was
    CHECK(p.folderDepth(*kick) == 2);
    CHECK(p.insideFolder(kick->id, outerId));
    CHECK_FALSE(p.hiddenByFolder(*kick));
    stack.execute(p, std::make_unique<SetFolderOpen>(outerId, false));
    CHECK(p.hiddenByFolder(*p.findTrack(kick->id)));
    p.findTrack(outerId)->mute = true;
    CHECK(p.mutedByFolder(*p.findTrack(kick->id)));
    p.findTrack(outerId)->mute = false;
    p.findTrack(innerId)->solo = true;
    CHECK(p.soloedByFolder(*p.findTrack(kick->id)));
    // A folder cannot go inside itself or a folder it holds.
    CHECK_FALSE(SetTrackFolder(outerId, innerId).apply(p));
    CHECK_FALSE(SetTrackFolder(outerId, outerId).apply(p));
    // Moving out of a folder.
    stack.execute(p, std::make_unique<SetTrackFolder>(kick->id, invalidId));
    CHECK(p.folderOf(*p.findTrack(kick->id)) == nullptr);
}

TEST_CASE("Freezing a track is a command; only tracks with audio freeze", "[core][freeze]")
{
    auto p = threeTracks();
    Track midi;
    midi.id = p.allocateId();
    midi.kind = TrackKind::midi;
    p.tracks.push_back(midi);
    UndoStack stack;
    stack.execute(p, std::make_unique<SetTrackFrozen>(p.tracks[0].id, "C:/media/Kick-freeze.wav"));
    CHECK(p.tracks[0].isFrozen());
    stack.undo(p);
    CHECK_FALSE(p.tracks[0].isFrozen());
    CHECK_FALSE(SetTrackFrozen(midi.id, "x.wav").apply(p));
}

TEST_CASE("The freeze render hears only the frozen track before its fader", "[core][freeze]")
{
    Project p;
    Bus reverb;
    reverb.id = p.allocateId();
    reverb.fx = true;
    reverb.gain = 0.5f;
    p.buses.push_back(reverb);
    Track keys;
    keys.id = p.allocateId();
    keys.kind = TrackKind::instrument;
    keys.gain = 0.25f;
    keys.pan = -0.5f;
    keys.mute = true;
    keys.outputBus = reverb.id;
    keys.sends.push_back({reverb.id, 0.7f, false});
    AutomationLane fade;
    fade.id = p.allocateId();
    fade.target = AutomationTarget::gain;
    fade.points = {{0, 1.0f}, {1000, 0.0f}};
    keys.automation.push_back(fade);
    Track notes;
    notes.id = p.allocateId();
    notes.kind = TrackKind::midi;
    notes.midiTarget = keys.id;
    Track other;
    other.id = p.allocateId();
    other.solo = true;
    p.tracks = {keys, notes, other};
    p.master.gain = 0.3f;

    const auto copy = freezeRenderProject(p, keys.id);
    const auto& k = *copy.findTrack(keys.id);
    CHECK(k.gain == 1.0f);
    CHECK(k.pan == 0.0f);
    CHECK_FALSE(k.mute);
    CHECK(k.sends.empty());
    CHECK(k.outputBus == invalidId);
    CHECK(k.automation.empty());
    CHECK(copy.findTrack(notes.id)->midiTarget == keys.id); // its notes still play it
    CHECK(copy.findTrack(other.id)->gain == 0.0f);
    CHECK_FALSE(copy.findTrack(other.id)->solo);
    CHECK(copy.buses[0].gain == 0.0f);
    CHECK(copy.master.gain == 1.0f);
}

TEST_CASE("Folders and frozen tracks survive a save and load", "[core][folder][json]")
{
    auto p = threeTracks();
    UndoStack stack;
    stack.execute(p, std::make_unique<AddFolderTrack>("Drums", std::vector<Id>{p.tracks[0].id}));
    p.tracks[0].folderOpen = false;
    p.tracks[2].frozenMedia = "Snare-freeze.wav";
    const auto loaded = projectFromJson(projectToJson(p));
    REQUIRE(loaded.has_value());
    CHECK(loaded->tracks[0].isFolder());
    CHECK_FALSE(loaded->tracks[0].folderOpen);
    CHECK(loaded->tracks[1].folder == p.tracks[0].id);
    CHECK(loaded->tracks[2].frozenMedia == "Snare-freeze.wav");
}
