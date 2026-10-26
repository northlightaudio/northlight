#include "core/ArrangeCommands.h"
#include "core/ProjectIds.h"
#include "core/ProjectJson.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace northlight::core;

namespace
{
// Two audio tracks in a folder, a third outside it, a MIDI track and two buses.
Project arrangeProject()
{
    Project p;
    Track folder;
    folder.id = 1;
    folder.name = "Folder";
    folder.kind = TrackKind::folder;
    Track a;
    a.id = 2;
    a.name = "A";
    a.folder = 1;
    a.clips.push_back({20, "a1", "a.wav", 0, 1000, 0, 1.0f});
    a.clips.push_back({21, "a2", "a.wav", 2000, 1000, 2000, 1.0f, 100, 200});
    Insert eq;
    eq.id = 22;
    eq.type = "northlight.eq";
    eq.params = {0.5f};
    a.inserts.push_back(eq);
    AutomationLane volume;
    volume.id = 23;
    volume.points = {{0, 0.5f}, {1500, 1.0f}, {2500, 0.25f}};
    a.automation.push_back(volume);
    AutomationLane param;
    param.id = 24;
    param.target = AutomationTarget::insertParameter;
    param.insert = 22;
    a.automation.push_back(param);
    a.sends.push_back({90, 0.5f, false});
    Track b;
    b.id = 3;
    b.name = "B";
    b.folder = 1;
    Track c;
    c.id = 4;
    c.name = "C";
    Track midi;
    midi.id = 5;
    midi.name = "Keys";
    midi.kind = TrackKind::midi;
    MidiClip clip;
    clip.id = 50;
    clip.start = 1000;
    clip.length = 4000;
    clip.notes = {{51, 60, 100, 0, 500}, {52, 62, 100, 1500, 500}, {53, 64, 100, 3000, 900}};
    clip.controllers = {{54, MidiControllerType::controller, 1, 10, 100},
                        {55, MidiControllerType::controller, 1, 90, 1200},
                        {56, MidiControllerType::controller, 1, 40, 2500}};
    midi.midiClips.push_back(clip);
    p.tracks = {folder, a, b, c, midi};
    Bus fx;
    fx.id = 90;
    fx.name = "Reverb";
    fx.fx = true;
    Bus group;
    group.id = 91;
    group.name = "Group";
    p.buses = {fx, group};
    p.nextId = 100;
    return p;
}

std::vector<Id> order(const Project& p)
{
    std::vector<Id> ids;
    for (const auto& t : p.tracks)
        ids.push_back(t.id);
    return ids;
}
} // namespace

TEST_CASE("A new track goes after the selected one, in its folder", "[core][arrange]")
{
    const auto p = arrangeProject();
    const auto inFolder = placeAfter(p, 2);
    CHECK(inFolder.index == 2);
    CHECK(inFolder.folder == 1);
    const auto afterFolder = placeAfter(p, 1); // a folder takes it at the end of what it holds
    CHECK(afterFolder.index == 3);
    CHECK(afterFolder.folder == 1);
    const auto outside = placeAfter(p, 4);
    CHECK(outside.index == 4);
    CHECK(outside.folder == invalidId);
    CHECK(placeAfter(p, 90).index == -1); // a bus: the end
    CHECK(withFolderContents(p, {1}) == std::vector<Id>{1, 2, 3});
}

TEST_CASE("Duplicate track copies clips, inserts, sends and automation with new ids", "[core][arrange]")
{
    auto p = arrangeProject();
    UndoStack stack;
    auto command = std::make_unique<DuplicateTrack>(2);
    const auto* raw = command.get();
    REQUIRE(stack.execute(p, std::move(command)));
    const auto copyId = raw->copyId();
    REQUIRE(p.tracks.size() == 6);
    const auto& copy = p.tracks[2]; // right after the original, in its folder
    CHECK(copy.id == copyId);
    CHECK(copy.name == "A copy");
    CHECK(copy.folder == 1);
    REQUIRE(copy.clips.size() == 2);
    CHECK(copy.clips[1].start == 2000);
    CHECK(copy.clips[1].fadeIn == 100);
    REQUIRE(copy.inserts.size() == 1);
    CHECK(copy.inserts[0].id != 22);
    CHECK(copy.inserts[0].params == std::vector<float>{0.5f});
    REQUIRE(copy.automation.size() == 2);
    CHECK(copy.automation[0].points == p.tracks[1].automation[0].points);
    CHECK(copy.automation[1].insert == copy.inserts[0].id); // the lane follows its insert
    CHECK(copy.sends == p.tracks[1].sends);
    // Every id in the project is still unique.
    std::multiset<Id> ids;
    forEachId(p, [&](Id id) { ids.insert(id); });
    for (const auto id : ids)
        CHECK(ids.count(id) == 1);

    REQUIRE(stack.undo(p));
    CHECK(order(p) == std::vector<Id>{1, 2, 3, 4, 5});
    REQUIRE(stack.redo(p));
    REQUIRE(p.tracks.size() == 6);
    CHECK(p.tracks[2].id == copyId); // the same ids as the first time
}

TEST_CASE("Moving a folder moves what it holds", "[core][arrange]")
{
    auto p = arrangeProject();
    UndoStack stack;
    REQUIRE(stack.execute(p, std::make_unique<MoveTracks>(withFolderContents(p, {1}), invalidId, invalidId)));
    CHECK(order(p) == std::vector<Id>{4, 5, 1, 2, 3});
    CHECK(p.findTrack(2)->folder == 1); // still in their folder
    REQUIRE(stack.undo(p));
    CHECK(order(p) == std::vector<Id>{1, 2, 3, 4, 5});
    REQUIRE(stack.redo(p));
    CHECK(order(p) == std::vector<Id>{4, 5, 1, 2, 3});
}

TEST_CASE("Dragging tracks into and out of a folder changes what holds them", "[core][arrange]")
{
    auto p = arrangeProject();
    UndoStack stack;
    // C into the folder, before B.
    REQUIRE(stack.execute(p, std::make_unique<MoveTracks>(std::vector<Id>{4}, 3, 1)));
    CHECK(order(p) == std::vector<Id>{1, 2, 4, 3, 5});
    CHECK(p.findTrack(4)->folder == 1);
    // A and B out of it, to the end.
    REQUIRE(stack.execute(p, std::make_unique<MoveTracks>(std::vector<Id>{2, 3}, invalidId, invalidId)));
    CHECK(order(p) == std::vector<Id>{1, 4, 5, 2, 3});
    CHECK(p.findTrack(2)->folder == invalidId);
    CHECK(p.findTrack(3)->folder == invalidId);
    REQUIRE(stack.undo(p));
    CHECK(p.findTrack(2)->folder == 1);
    CHECK(order(p) == std::vector<Id>{1, 2, 4, 3, 5});
    REQUIRE(stack.undo(p));
    CHECK(order(p) == std::vector<Id>{1, 2, 3, 4, 5});
    CHECK(p.findTrack(4)->folder == invalidId);
    // A folder cannot go into itself.
    CHECK_FALSE(stack.execute(p, std::make_unique<MoveTracks>(std::vector<Id>{1, 2, 3}, invalidId, 1)));
}

TEST_CASE("Buses reorder", "[core][arrange]")
{
    auto p = arrangeProject();
    UndoStack stack;
    REQUIRE(stack.execute(p, std::make_unique<MoveBus>(90, 1)));
    CHECK(p.buses[0].id == 91);
    CHECK(p.buses[1].id == 90);
    REQUIRE(stack.undo(p));
    CHECK(p.buses[0].id == 90);
    REQUIRE(stack.redo(p));
    CHECK(p.buses[0].id == 91);
}

TEST_CASE("A clip's name and colour are set and undone", "[core][arrange]")
{
    auto p = arrangeProject();
    UndoStack stack;
    REQUIRE(stack.execute(p, std::make_unique<SetClipName>(2, 20, false, "Intro")));
    REQUIRE(stack.execute(p, std::make_unique<SetClipColour>(5, 50, true, 0xff3366ccu)));
    CHECK(p.findTrack(2)->findClip(20)->name == "Intro");
    CHECK(p.findTrack(5)->findMidiClip(50)->colour == 0xff3366ccu);
    CHECK_FALSE(stack.execute(p, std::make_unique<SetClipName>(2, 999, false, "x")));
    REQUIRE(stack.undo(p));
    CHECK(p.findTrack(5)->findMidiClip(50)->colour == 0u);
    REQUIRE(stack.undo(p));
    CHECK(p.findTrack(2)->findClip(20)->name == "a1");
    REQUIRE(stack.redo(p));
    CHECK(p.findTrack(2)->findClip(20)->name == "Intro");
}

TEST_CASE("Clip colours are saved with the project", "[core][arrange][json]")
{
    auto p = arrangeProject();
    p.tracks[1].clips[0].colour = 0xff112233u;
    p.tracks[4].midiClips[0].colour = 0xff445566u;
    const auto loaded = projectFromJson(projectToJson(p));
    REQUIRE(loaded.has_value());
    CHECK(loaded->tracks[1].clips[0].colour == 0xff112233u);
    CHECK(loaded->tracks[1].clips[1].colour == 0u);
    CHECK(loaded->tracks[4].midiClips[0].colour == 0xff445566u);
}

TEST_CASE("A clip part keeps its source and only its own fades", "[core][arrange]")
{
    const AudioClip clip{1, "c", "c.wav", 1000, 1000, 500, 1.0f, 200, 300};
    const auto middle = clipPart(clip, 1200, 1700);
    REQUIRE(middle.has_value());
    CHECK(middle->start == 1200);
    CHECK(middle->length == 500);
    CHECK(middle->sourceOffset == 700);
    CHECK(middle->fadeIn == 0);
    CHECK(middle->fadeOut == 0);
    const auto head = clipPart(clip, 0, 1100);
    REQUIRE(head.has_value());
    CHECK(head->length == 100);
    CHECK(head->fadeIn == 100); // clamped to the part
    CHECK_FALSE(clipPart(clip, 3000, 4000).has_value());
}

TEST_CASE("A MIDI clip part keeps the notes that start in it and the controller value at its start",
          "[core][arrange]")
{
    const auto p = arrangeProject();
    const auto& clip = p.tracks[4].midiClips[0]; // starts at 1000
    const auto part = midiClipPart(clip, 2000, 4200);
    REQUIRE(part.has_value());
    CHECK(part->start == 2000);
    CHECK(part->length == 2200);
    REQUIRE(part->notes.size() == 2);
    CHECK(part->notes[0].start == 500); // 2500 - 2000
    CHECK(part->notes[1].start == 2000);
    CHECK(part->notes[1].length == 200); // cut at the part's end
    // CC1 was 10 from 1100: the part opens with it, then the events at 2200 and 3500.
    REQUIRE(part->controllers.size() == 3);
    CHECK(part->controllers[0].time == 0);
    CHECK(part->controllers[0].value == 10);
    CHECK(part->controllers[1].time == 200);
    CHECK(part->controllers[1].value == 90);
    CHECK(part->controllers[2].time == 1500);
}

TEST_CASE("Removing a time range cuts clips and keeps automation either side", "[core][arrange]")
{
    auto p = arrangeProject();
    UndoStack stack;
    const auto before = p;
    // [500, 2500) on A: a1 loses its tail, a2 its head.
    REQUIRE(stack.execute(p, std::make_unique<RemoveTimeRange>(std::vector<Id>{2}, 500, 2500, false, true)));
    const auto& a = *p.findTrack(2);
    REQUIRE(a.clips.size() == 2);
    CHECK(a.clips[0].length == 500);
    CHECK(a.clips[1].start == 2500);
    CHECK(a.clips[1].length == 500);
    CHECK(a.clips[1].sourceOffset == 2500);
    CHECK(a.clips[1].fadeIn == 0);
    const auto& points = a.automation[0].points;
    REQUIRE(points.size() == 3);
    CHECK(points[0].position == 0);
    CHECK(points[1].position == 500);
    CHECK(points[1].value == before.tracks[1].automation[0].valueAt(500));
    CHECK(points[2].position == 2500);
    REQUIRE(stack.undo(p));
    CHECK(p == before);
    REQUIRE(stack.redo(p));
    CHECK(p.findTrack(2)->clips[1].start == 2500);
}

TEST_CASE("Ripple deleting a range closes the gap and splits a clip across it", "[core][arrange]")
{
    auto p = arrangeProject();
    UndoStack stack;
    // [200, 400) inside a1, with ripple: a1 becomes two parts, everything later moves back 200.
    REQUIRE(
        stack.execute(p, std::make_unique<RemoveTimeRange>(std::vector<Id>{2, 5}, 200, 400, true, false)));
    const auto& a = *p.findTrack(2);
    REQUIRE(a.clips.size() == 3);
    CHECK(a.clips[0].length == 200);
    CHECK(a.clips[1].start == 200);
    CHECK(a.clips[1].length == 600);
    CHECK(a.clips[1].sourceOffset == 400);
    CHECK(a.clips[1].id != a.clips[0].id);
    CHECK(a.clips[2].start == 1800);
    CHECK(p.findTrack(5)->midiClips[0].start == 800);
    CHECK(a.automation[0].points == arrangeProject().tracks[1].automation[0].points); // not asked to
    REQUIRE(stack.undo(p));
    CHECK(p.tracks == arrangeProject().tracks); // (ids given out stay given out)
    REQUIRE(stack.redo(p));
    CHECK(p.findTrack(2)->clips.size() == 3u);
    CHECK(p.findTrack(2)->clips[2].start == 1800);
}
