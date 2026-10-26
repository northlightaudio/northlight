#include "core/ArrangeCommands.h"
#include "core/Command.h"
#include "ui/edit/ClipOps.h"
#include "ui/timeline/GridLines.h"
#include "ui/timeline/SnapGrids.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace northlight;
using namespace northlight::core;
using ui::edit::ClipRef;

namespace
{
// An audio track with two clips and a volume lane, a second audio track, a MIDI track.
Project editProject()
{
    Project p;
    Track audio;
    audio.id = 1;
    audio.clips.push_back({10, "a", "a.wav", 0, 1000, 0, 1.0f});
    audio.clips.push_back({11, "b", "b.wav", 3000, 1000, 0, 1.0f});
    AutomationLane volume;
    volume.id = 12;
    volume.points = {{100, 0.5f}, {900, 0.8f}, {2000, 1.0f}, {3500, 0.2f}};
    audio.automation.push_back(volume);
    Track second;
    second.id = 2;
    Track midi;
    midi.id = 3;
    midi.kind = TrackKind::midi;
    MidiClip clip;
    clip.id = 30;
    clip.start = 0;
    clip.length = 2000;
    clip.notes = {{31, 60, 100, 100, 100}, {32, 62, 100, 1500, 100}};
    midi.midiClips.push_back(clip);
    p.tracks = {audio, second, midi};
    p.nextId = 100;
    return p;
}

Project apply(Project p, ui::edit::Edit& edit)
{
    UndoStack stack;
    for (auto& c : edit.commands)
        stack.execute(p, std::move(c));
    return p;
}
} // namespace

TEST_CASE("Ctrl+drag copies clips and keeps the originals", "[ui][edit]")
{
    const auto p = editProject();
    auto copy = ui::edit::copyClipsTo(p, {{1, 10}}, 5000, 1);
    REQUIRE(copy.edit.commands.size() == 1);
    REQUIRE(copy.moved.size() == 1);
    CHECK(copy.moved[0].track == 2);
    CHECK(copy.edit.name == "Copy clip");
    const auto after = apply(p, copy.edit);
    CHECK(after.tracks[0].clips.size() == 2); // the original stays
    REQUIRE(after.tracks[1].clips.size() == 1);
    CHECK(after.tracks[1].clips[0].start == 5000);
    CHECK(after.tracks[1].clips[0].id != 10);
    // A MIDI clip only lands on a MIDI track: none from the audio track's row.
    auto midi = ui::edit::copyClipsTo(p, {{3, 30, true}}, 100, -1);
    CHECK(midi.edit.empty());
}

TEST_CASE("Automation moves with the clips over it", "[ui][edit]")
{
    const auto p = editProject();
    // Clip a [0, 1000) moves 3000 later: its points (100 and 900) go to 3100 and
    // 3900, and the point at 3500 under where they land gives way.
    auto move = ui::edit::moveAutomation(p, {{1, 10}}, 3000, false);
    REQUIRE(move.commands.size() == 1);
    auto after = apply(p, move);
    const auto& points = after.tracks[0].automation[0].points;
    REQUIRE(points.size() == 3);
    CHECK(points[0].position == 2000);
    CHECK(points[1].position == 3100);
    CHECK(points[2].position == 3900);
    // A copy keeps the originals.
    auto copy = ui::edit::moveAutomation(p, {{1, 10}}, 3000, true);
    after = apply(p, copy);
    CHECK(after.tracks[0].automation[0].points.size() == 5);
    // No clip over the points: nothing to do.
    CHECK(ui::edit::moveAutomation(p, {{2, 99}}, 100, false).empty());
}

TEST_CASE("A range copy pastes its parts at the same distance from the range start", "[ui][edit]")
{
    const auto p = editProject();
    const auto items = ui::edit::copyRange(p, {1, 3}, 500, 3500);
    // a's tail, b's head, the MIDI clip's part with the note at 1500.
    REQUIRE(items.size() == 3);
    CHECK(items[0].audio.start == 500);
    CHECK(items[0].audio.length == 500);
    CHECK(items[1].audio.start == 3000);
    CHECK(items[1].audio.length == 500);
    CHECK(items[2].midi);
    CHECK(items[2].trackOffset == 2);
    CHECK(items[2].midiClip.notes.size() == 1);
    auto paste = ui::edit::pasteClips(p, items, 0, 10000, SamplePosition{500});
    const auto after = apply(p, paste);
    REQUIRE(after.tracks[0].clips.size() == 4);
    CHECK(after.tracks[0].clips[2].start == 10000);
    CHECK(after.tracks[0].clips[3].start == 12500); // 3000 - 500 after the paste point
    CHECK(after.tracks[2].midiClips.back().start == 10000);
}

TEST_CASE("Deleting a range is one command over the tracks", "[ui][edit]")
{
    const auto p = editProject();
    auto e = ui::edit::deleteRange(p, {1, 99}, 500, 3500, true, true);
    CHECK(e.name == "Ripple delete range");
    REQUIRE(e.commands.size() == 1);
    const auto after = apply(p, e);
    REQUIRE(after.tracks[0].clips.size() == 2);
    CHECK(after.tracks[0].clips[1].start == 500); // b's tail, closed up
    CHECK(after.tracks[0].clips[1].length == 500);
    CHECK(ui::edit::deleteRange(p, {1}, 500, 500, false, false).empty());
}

TEST_CASE("Clip colour and name edits skip clips that are gone", "[ui][edit]")
{
    const auto p = editProject();
    auto colour = ui::edit::setClipColour(p, {{1, 10}, {1, 99}, {3, 30, true}}, 0xff00ff00u);
    CHECK(colour.commands.size() == 2);
    CHECK(colour.name == "Colour clips");
    auto after = apply(p, colour);
    CHECK(after.tracks[0].clips[0].colour == 0xff00ff00u);
    CHECK(after.tracks[2].midiClips[0].colour == 0xff00ff00u);
    CHECK(ui::edit::renameClip(p, {1, 10}, "").empty());
    auto rename = ui::edit::renameClip(p, {1, 11}, "Chorus");
    after = apply(p, rename);
    CHECK(after.tracks[0].clips[1].name == "Chorus");
}

TEST_CASE("Triplet and dotted snap grids follow the signature", "[ui][grid]")
{
    const auto four = TimeSignature{4, 4};
    const auto sixEight = TimeSignature{6, 8};
    CHECK(ui::snapGridQuarters(12, four) == Catch::Approx(1.0 / 3.0)); // eighth triplets
    CHECK(ui::snapGridQuarters(6, four) == Catch::Approx(2.0 / 3.0));
    CHECK(ui::snapGridQuarters(ui::dottedSnapBase + 8, four) == Catch::Approx(0.75)); // dotted eighth
    CHECK(ui::snapGridQuarters(4, sixEight) == Catch::Approx(0.5));                   // the beat is an eighth
    CHECK(ui::snapGridQuarters(0, four) == 0.0);
    CHECK(ui::validSnapGrid(7) == 0);
    CHECK(ui::validSnapGrid(24) == 24);
    CHECK(ui::snapGridLabel(12) == "1/8T");
    // Every grid in the table has a step (or is adaptive or a bar).
    for (const auto& g : ui::snapGridChoices())
        if (g.family != ui::SnapFamily::adaptive && g.family != ui::SnapFamily::bar)
            CHECK(ui::snapGridQuarters(g.code, four) > 0.0);
}

TEST_CASE("Snapping to eighth triplets lands on thirds of a beat across a tempo change", "[ui][grid]")
{
    // 120 BPM (24000 samples a quarter at 48 kHz), 60 BPM from bar 2.
    const TempoMap map(48000.0, 120.0, {4, 4}, {{4.0, 60.0, false}});
    // In bar 1 a triplet eighth is 8000 samples.
    CHECK(ui::snappedToGrid(map, 8100, 12, 100.0) == 8000);
    // In bar 2 (from 96000) a quarter is 48000, so a triplet eighth 16000.
    CHECK(ui::snappedToGrid(map, 96000 + 16100, 12, 100.0) == 96000 + 16000);
    // A dotted eighth restarts at each bar line: 36000 into bar 2 (0.75 of a quarter).
    CHECK(ui::snappedToGrid(map, 96000 + 35000, ui::dottedSnapBase + 8, 100.0) == 96000 + 36000);
}
