#include "core/Command.h"
#include "core/Comping.h"
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

TEST_CASE("The glue tool joins a clip with the next when they can join", "[ui][edit]")
{
    auto p = editProject();
    // Audio: a and b continue one recording; b and c do not.
    CHECK(ui::edit::nextClip(p, {1, 10}) == ClipRef{1, 11});
    CHECK_FALSE(ui::edit::nextClip(p, {1, 12}).has_value());
    CHECK(ui::edit::canJoinWithNext(p, {1, 10}));
    CHECK_FALSE(ui::edit::canJoinWithNext(p, {1, 11}));
    CHECK(ui::edit::joinWithNext(p, {1, 11}).empty());
    auto join = ui::edit::joinWithNext(p, {1, 10});
    auto after = apply(p, join);
    REQUIRE(after.tracks[0].clips.size() == 2);
    CHECK(after.tracks[0].clips[0].length == 2000);

    // MIDI: any later clip joins, across the gap.
    MidiClip later;
    later.id = 30;
    later.start = 8000;
    later.length = 1000;
    later.notes = {{31, 62, 100, 0, 100}};
    p.tracks[1].midiClips.push_back(later);
    CHECK(ui::edit::canJoinWithNext(p, {2, 20, true}));
    after = apply(p, join = ui::edit::joinWithNext(p, {2, 20, true}));
    REQUIRE(after.tracks[1].midiClips.size() == 1);
    CHECK(after.tracks[1].midiClips[0].length == 9000);
    CHECK(after.tracks[1].midiClips[0].notes.size() == 2);

    // A MIDI selection joins into its earliest clip.
    auto selected = ui::edit::joinClips(p, {{2, 20, true}, {2, 30, true}});
    after = apply(p, selected);
    CHECK(after.tracks[1].midiClips.size() == 1);
}

TEST_CASE("Muting sets only the clips that differ; toggling mutes while any clip plays", "[ui][edit]")
{
    const auto p = editProject();
    auto mute = ui::edit::setMuted(p, {{1, 10}, {2, 20, true}}, true);
    CHECK(mute.name == "Mute clips");
    CHECK(mute.commands.size() == 2);
    auto after = apply(p, mute);
    CHECK(ui::edit::isMuted(after, {1, 10}));
    CHECK(ui::edit::isMuted(after, {2, 20, true}));
    CHECK_FALSE(ui::edit::isMuted(after, {1, 11}));
    CHECK(ui::edit::setMuted(after, {{1, 10}}, true).empty()); // already muted

    // One muted, one playing: the toggle mutes both; then it unmutes both.
    auto toggle = ui::edit::toggleMuted(after, {{1, 10}, {1, 11}});
    CHECK(toggle.name == "Mute clips");
    CHECK(toggle.commands.size() == 1);
    after = apply(after, toggle);
    toggle = ui::edit::toggleMuted(after, {{1, 10}, {1, 11}});
    CHECK(toggle.name == "Unmute clips");
    after = apply(after, toggle);
    CHECK_FALSE(ui::edit::isMuted(after, {1, 10}));
    CHECK_FALSE(ui::edit::isMuted(after, {1, 11}));
}

TEST_CASE("A moved clip lands only on a track of its own kind", "[ui][edit]")
{
    const auto p = editProject(); // track 0 audio, track 1 MIDI
    CHECK(ui::edit::landingTrack(p, {1, 10}, 0) == &p.tracks[0]);
    CHECK(ui::edit::landingTrack(p, {1, 10}, 1) == nullptr); // audio onto MIDI
    CHECK(ui::edit::landingTrack(p, {2, 20, true}, -1) == nullptr);
    CHECK(ui::edit::landingTrack(p, {2, 20, true}, 5) == &p.tracks[1]); // clamped to the last track
    CHECK(ui::edit::landingTrack(p, {9, 20, true}, 0) == nullptr);      // its track is gone
    CHECK(ui::edit::takesClip(p.tracks[1], true));
    CHECK_FALSE(ui::edit::takesClip(p.tracks[1], false));
}

TEST_CASE("A move drag shifts the selection, each clip onto its own kind of track", "[ui][edit]")
{
    auto p = editProject();
    Track second;
    second.id = 3;
    p.tracks.push_back(second); // audio, after the MIDI track
    // Two rows down: the audio clip lands on track 3; the MIDI clip would land on
    // audio (clamped to the last track) and stays.
    auto move = ui::edit::moveClips(p, {{1, 12}, {2, 20, true}}, 500, 2);
    CHECK(move.edit.name == "Move clip");
    REQUIRE(move.moved.size() == 1);
    CHECK(move.moved[0] == ClipRef{3, 12});
    const auto after = apply(p, move.edit);
    REQUIRE(after.tracks[2].clips.size() == 1);
    CHECK(after.tracks[2].clips[0].start == 5500);

    // Not before the start; no movement, no edit.
    move = ui::edit::moveClips(p, {{1, 10}, {1, 12}}, -3000, 0);
    CHECK(move.edit.name == "Move clips");
    const auto left = apply(p, move.edit);
    CHECK(left.tracks[0].findClip(10)->start == 0);
    CHECK(left.tracks[0].findClip(12)->start == 2000);
    CHECK(ui::edit::moveClips(p, {{1, 10}}, 0, 0).edit.empty());
}

TEST_CASE("Moving a comp's clips moves its take group once", "[ui][edit]")
{
    auto p = editProject();
    TakeGroup group;
    group.id = 50;
    group.clips = {10, 11};
    p.tracks[0].takeGroups.push_back(group);
    const auto move = ui::edit::moveClips(p, {{1, 10}, {1, 11}}, 1000, 0);
    REQUIRE(move.edit.commands.size() == 1);
    CHECK(dynamic_cast<const MoveTakeGroup*>(move.edit.commands[0].get()) != nullptr);
    CHECK(move.moved == ui::edit::Selection{{1, 10}, {1, 11}});
}

TEST_CASE("Trim, stretch, fade and gain drags make one command for the clip", "[ui][edit]")
{
    const auto p = editProject();
    auto trim = ui::edit::trimClip(p, {1, 11}, 1200, 800, 1200);
    CHECK(trim.name == "Trim clip");
    auto after = apply(p, trim);
    CHECK(after.tracks[0].findClip(11)->start == 1200);
    CHECK(after.tracks[0].findClip(11)->sourceOffset == 1200);
    auto trimMidi = ui::edit::trimClip(p, {2, 20, true}, 0, 2000, 0);
    after = apply(p, trimMidi);
    CHECK(after.tracks[1].midiClips[0].length == 2000);
    CHECK(ui::edit::trimClip(p, {1, 99}, 0, 10, 0).empty());

    auto stretch = ui::edit::stretchClip(p, {1, 12}, 1000); // twice as long
    after = apply(p, stretch);
    CHECK(after.tracks[0].findClip(12)->length == 1000);
    CHECK(after.tracks[0].findClip(12)->stretch == 2.0);
    CHECK(ui::edit::stretchClip(p, {1, 12}, 500).empty());       // the same length
    CHECK(ui::edit::stretchClip(p, {2, 20, true}, 900).empty()); // MIDI does not stretch

    auto fade = ui::edit::setFade(p, {1, 12}, false, 200, 0.5f);
    after = apply(p, fade);
    CHECK(after.tracks[0].findClip(12)->fadeOut == 200);
    CHECK(after.tracks[0].findClip(12)->fadeOutCurve == 0.5f);
    CHECK(after.tracks[0].findClip(12)->fadeIn == 0);

    auto gain = ui::edit::setClipGain(p, {1, 12}, 0.5f);
    CHECK(gain.name == "Set clip gain");
    after = apply(p, gain);
    CHECK(after.tracks[0].findClip(12)->gain == 0.5f);
}
