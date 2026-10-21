#include "ui/edit/ClipOps.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;
using namespace northlight::core;
using ui::edit::ClipRef;

namespace
{
// Two audio clips back to back from one file, one later on, and a MIDI track.
Project editProject()
{
    Project p;
    Track audio;
    audio.id = 1;
    audio.clips.push_back({10, "a", "a.wav", 0, 1000, 0, 1.0f});
    audio.clips.push_back({11, "b", "a.wav", 1000, 1000, 1000, 1.0f});
    audio.clips.push_back({12, "c", "c.wav", 5000, 500, 0, 1.0f});
    Track midi;
    midi.id = 2;
    midi.kind = TrackKind::midi;
    MidiClip clip;
    clip.id = 20;
    clip.start = 0;
    clip.length = 4000;
    clip.notes = {{21, 60, 100, 0, 100}};
    clip.controllers = {{22, MidiControllerType::controller, 1, 64, 50}};
    midi.midiClips.push_back(clip);
    p.tracks = {audio, midi};
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

TEST_CASE("Ripple delete closes each gap; plain delete leaves them", "[ui][edit]")
{
    const auto p = editProject();
    auto plain = ui::edit::deleteClips(p, {{1, 11}}, false);
    CHECK(plain.name == "Delete clips");
    auto after = apply(p, plain);
    REQUIRE(after.tracks[0].clips.size() == 2);
    CHECK(after.tracks[0].findClip(12)->start == 5000);

    auto ripple = ui::edit::deleteClips(p, {{1, 10}, {1, 11}}, true);
    CHECK(ripple.name == "Ripple delete");
    after = apply(p, ripple);
    REQUIRE(after.tracks[0].clips.size() == 1);
    CHECK(after.tracks[0].clips[0].start == 3000); // moved back by both lengths
}

TEST_CASE("Splitting cuts the selected clips, or every clip with none selected", "[ui][edit]")
{
    const auto p = editProject();
    auto all = ui::edit::splitAt(p, {}, 500);
    CHECK(all.commands.size() == 2); // the first audio clip and the MIDI clip
    auto one = ui::edit::splitAt(p, {{2, 20, true}}, 500);
    CHECK(one.commands.size() == 1);
    auto range = ui::edit::splitAtRange(p, {{1, 12}}, 5100, 5300);
    auto after = apply(p, range);
    CHECK(after.tracks[0].clips.size() == 5); // c in three
    CHECK(ui::edit::splitAtRange(p, {}, 500, 500).empty());
}

TEST_CASE("Joining merges clips that continue each other's source; nudging stops at zero", "[ui][edit]")
{
    const auto p = editProject();
    auto join = ui::edit::joinClips(p, {{1, 10}, {1, 11}, {1, 12}});
    REQUIRE(join.commands.size() == 1); // c is another file
    auto after = apply(p, join);
    CHECK(after.tracks[0].clips.size() == 2);
    CHECK(after.tracks[0].clips[0].length == 2000);

    auto nudge = ui::edit::nudgeClips(p, {{1, 11}, {2, 20, true}}, -1500);
    after = apply(p, nudge);
    CHECK(after.tracks[0].findClip(11)->start == 0);
    CHECK(after.tracks[1].findMidiClip(20)->start == 0);
}

TEST_CASE("Copy and paste keep track offsets and land only on matching tracks", "[ui][edit]")
{
    const auto p = editProject();
    const auto clipboard = ui::edit::copyClips(p, {{1, 12}, {2, 20, true}});
    REQUIRE(clipboard.size() == 2);
    CHECK(clipboard[0].trackOffset == 0);
    CHECK(clipboard[1].trackOffset == 1);

    auto paste = ui::edit::pasteClips(p, clipboard, 0, 8000);
    auto after = apply(p, paste);
    CHECK(after.tracks[0].clips.size() == 4);
    CHECK(after.tracks[0].clips.back().start == 8000 + 5000); // its distance from the earliest kept
    CHECK(after.tracks[1].midiClips.size() == 2);
    CHECK(after.tracks[1].midiClips.back().start == 8000);
    REQUIRE(after.tracks[1].midiClips.back().controllers.size() == 1);
    CHECK(after.tracks[1].midiClips.back().controllers[0].id != 22); // a fresh id

    // Pasted one row down, the audio clip would land on the MIDI track: it is skipped.
    auto shifted = ui::edit::pasteClips(p, clipboard, 1, 8000);
    CHECK(shifted.commands.size() == 1);
}
