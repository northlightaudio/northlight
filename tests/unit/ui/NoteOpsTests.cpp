#include "core/TempoMap.h"
#include "ui/edit/NoteGrid.h"
#include "ui/edit/NoteOps.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;
using namespace northlight::ui;
using core::MidiNote;

namespace
{
// 120 bpm at 48 kHz: a beat is 24 000 samples, a sixteenth 6 000.
constexpr core::SamplePosition beat = 24000;

core::MidiClip clipOf(std::vector<MidiNote> notes, core::SamplePosition start = 0)
{
    core::MidiClip c;
    c.id = 1;
    c.start = start;
    c.length = 16 * beat;
    core::Id id = 10;
    for (auto& n : notes)
    {
        n.id = id++;
        c.notes.push_back(n);
    }
    c.sortNotes();
    return c;
}

MidiNote note(int pitch, core::SamplePosition start, core::SamplePosition length, int velocity = 100)
{
    MidiNote n;
    n.note = pitch;
    n.start = start;
    n.length = length;
    n.velocity = velocity;
    return n;
}

core::TempoMap map120()
{
    core::Project p;
    p.sampleRate = 48000.0;
    p.tempoBpm = 120.0;
    return core::tempoMapOf(p);
}
} // namespace

TEST_CASE("Copied notes paste from a position keeping their spacing", "[ui][notes]")
{
    const auto clip = clipOf({note(60, beat, beat / 2), note(64, beat * 2, beat), note(67, beat * 5, beat)});
    const auto copied = noteops::copy(clip, {10, 11}, beat / 4);
    REQUIRE(copied.notes.size() == 2);
    CHECK(copied.notes[0].start == 0);
    CHECK(copied.notes[1].start == beat);
    CHECK(copied.span == 2 * beat); // to the second note's end, in whole grid steps
    const auto edit = noteops::paste(clip, copied, 8 * beat);
    REQUIRE(edit.add.size() == 2);
    CHECK(edit.add[0].start == 8 * beat);
    CHECK(edit.add[1].start == 9 * beat);
    CHECK(edit.add[0].id == core::invalidId);
    // Past the clip's end nothing lands.
    CHECK(noteops::paste(clip, copied, 16 * beat).add.empty());
}

TEST_CASE("Legato ends each note where the next starts", "[ui][notes]")
{
    const auto clip = clipOf({note(60, 0, beat / 4), note(62, beat, beat / 4), note(64, 3 * beat, beat / 4)});
    const auto result = noteops::applied(clip, noteops::legato(clip, {10, 11, 12}));
    CHECK(result.notes[0].length == beat);
    CHECK(result.notes[1].length == 2 * beat);
    CHECK(result.notes[2].length == beat / 4); // the last keeps its length
}

TEST_CASE("A velocity drag moves the selection by the same amount, Alt sets them all", "[ui][notes]")
{
    const auto clip =
        clipOf({note(60, 0, beat, 40), note(62, beat, beat, 80), note(64, 2 * beat, beat, 120)});
    // The 80 dragged to 100: every note +20, clamped at 127.
    const auto relative = noteops::applied(clip, noteops::dragVelocity(clip, {10, 11, 12}, 11, 100, false));
    CHECK(relative.notes[0].velocity == 60);
    CHECK(relative.notes[1].velocity == 100);
    CHECK(relative.notes[2].velocity == 127);
    const auto absolute = noteops::applied(clip, noteops::dragVelocity(clip, {10, 11, 12}, 11, 100, true));
    for (const auto& n : absolute.notes)
        CHECK(n.velocity == 100);
    // A note outside the selection drags alone.
    const auto alone = noteops::applied(clip, noteops::dragVelocity(clip, {10}, 12, 90, false));
    CHECK(alone.notes[0].velocity == 40);
    CHECK(alone.notes[2].velocity == 90);
}

TEST_CASE("Velocity scales, compresses towards the average and randomises repeatably", "[ui][notes]")
{
    const auto clip = clipOf({note(60, 0, beat, 40), note(62, beat, beat, 120)});
    const auto compressed = noteops::applied(clip, noteops::compressVelocity(clip, {10, 11}, 0.5));
    CHECK(compressed.notes[0].velocity == 60);
    CHECK(compressed.notes[1].velocity == 100);
    const auto scaled = noteops::applied(clip, noteops::scaleVelocity(clip, {10, 11}, 0.5));
    CHECK(scaled.notes[0].velocity == 20);
    CHECK(scaled.notes[1].velocity == 60);
    const auto a = noteops::randomiseVelocity(clip, {10, 11}, 10, 42);
    const auto b = noteops::randomiseVelocity(clip, {10, 11}, 10, 42);
    CHECK(noteops::applied(clip, a).notes == noteops::applied(clip, b).notes);
    for (const auto& n : noteops::applied(clip, a).notes)
        CHECK((n.velocity >= 30 && n.velocity <= 127));
}

TEST_CASE("Quantise moves starts to the grid, with swing delaying every second line", "[ui][notes]")
{
    const auto map = map120();
    const NoteGrid eighths = noteGrids[3]; // 1/8
    const auto clip = clipOf({note(60, 1000, beat / 4), note(62, beat / 2 + 900, beat / 4)});
    const auto straight = noteops::applied(clip, noteops::quantise(clip, {}, map, eighths, 100.0, {}));
    CHECK(straight.notes[0].start == 0);
    CHECK(straight.notes[1].start == beat / 2);
    // 66% swing (a third of a step late): the off-beat eighth moves to 2/3 of the beat.
    noteops::QuantiseOptions swung;
    swung.swing = noteops::swingFromPercent(66.6667f);
    const auto swingClip = clipOf({note(60, 1000, beat / 4), note(62, beat * 2 / 3 - 500, beat / 4)});
    const auto result =
        noteops::applied(swingClip, noteops::quantise(swingClip, {}, map, eighths, 100.0, swung));
    CHECK(result.notes[0].start == 0);
    CHECK(std::abs(result.notes[1].start - beat * 2 / 3) <= 2);
    // Half strength goes half way.
    noteops::QuantiseOptions half;
    half.strength = 0.5f;
    const auto halfway = noteops::applied(clip, noteops::quantise(clip, {}, map, eighths, 100.0, half));
    CHECK(halfway.notes[0].start == 500);
}

TEST_CASE("Humanise moves timing and velocity within the range, the same for a seed", "[ui][notes]")
{
    const auto clip = clipOf({note(60, beat, beat, 100), note(62, 2 * beat, beat, 100)});
    const auto a = noteops::applied(clip, noteops::humanise(clip, {10, 11}, 480, 8, 7));
    const auto b = noteops::applied(clip, noteops::humanise(clip, {10, 11}, 480, 8, 7));
    CHECK(a.notes == b.notes);
    for (size_t i = 0; i < a.notes.size(); ++i)
    {
        CHECK(std::abs(a.notes[i].start - clip.notes[i].start) <= 480);
        CHECK(std::abs(a.notes[i].velocity - 100) <= 8);
    }
}

TEST_CASE("Notes split at a position and join pitch by pitch", "[ui][notes]")
{
    const auto clip = clipOf({note(60, 0, 2 * beat), note(60, 3 * beat, beat), note(64, 0, beat)});
    const auto split = noteops::applied(clip, noteops::split(clip, {10}, beat, 1));
    REQUIRE(split.notes.size() == 4);
    CHECK(split.findNote(10)->length == beat);
    const auto joined = noteops::applied(clip, noteops::join(clip, {10, 11, 12}));
    REQUIRE(joined.notes.size() == 2);
    CHECK(joined.findNote(10)->length == 4 * beat); // to the second C's end
    CHECK(joined.findNote(12) != nullptr);          // alone on its pitch: unchanged
}

TEST_CASE("Mute toggles: all muted unmutes, else every note mutes", "[ui][notes]")
{
    auto clip = clipOf({note(60, 0, beat), note(62, beat, beat)});
    const auto muted = noteops::applied(clip, noteops::toggleMute(clip, {10, 11}));
    CHECK(muted.notes[0].muted);
    CHECK(muted.notes[1].muted);
    const auto unmuted = noteops::applied(muted, noteops::toggleMute(muted, {10, 11}));
    CHECK_FALSE(unmuted.notes[0].muted);
}

TEST_CASE("Stepping the selection walks the notes in time order", "[ui][notes]")
{
    const auto clip = clipOf({note(64, beat, beat), note(60, 0, beat), note(67, 2 * beat, beat)});
    CHECK(noteops::stepSelection(clip, {}, 1) == 11); // the first note in time
    CHECK(noteops::stepSelection(clip, {11}, 1) == 10);
    CHECK(noteops::stepSelection(clip, {10}, -1) == 11);
    CHECK(noteops::stepSelection(clip, {12}, 1) == core::invalidId);
    CHECK(noteops::notesOfPitch(clip, 64) == std::vector<core::Id>{10});
}

TEST_CASE("Transposing keeps the selection inside the MIDI range", "[ui][notes]")
{
    const auto clip = clipOf({note(120, 0, beat), note(100, beat, beat)});
    const auto up = noteops::applied(clip, noteops::transpose(clip, {10, 11}, 12));
    CHECK(up.findNote(10)->note == 127); // as far as the highest can go: 7
    CHECK(up.findNote(11)->note == 107);
}

TEST_CASE("The grid list has triplet and dotted values, and [ ] step within a family", "[ui][notes][grid]")
{
    const auto map = map120();
    const auto at = [&](std::string_view label)
    {
        for (std::size_t i = 0; i < noteGrids.size(); ++i)
            if (noteGrids[i].label == label)
                return i;
        FAIL("no grid " << label);
        return std::size_t{0};
    };
    CHECK(map.stepSamples(noteGridStep(noteGrids[at("1/8T")], map, 0, 100.0), 0) == 8000.0);
    CHECK(map.stepSamples(noteGridStep(noteGrids[at("1/8.")], map, 0, 100.0), 0) == 18000.0);
    CHECK(noteGridStep(noteGrids[at("Bar")], map, 0, 100.0).bars == 1);
    CHECK(noteGridStepped(at("1/16"), 1) == at("1/32"));
    CHECK(noteGridStepped(at("1/16"), -1) == at("1/8"));
    CHECK(noteGridStepped(at("1/8T"), 1) == at("1/16T"));
    CHECK(noteGridStepped(at("1/32"), 1) == at("1/32")); // the finest stays
    CHECK(noteGridStepped(at("Bar"), 1) == at("1/4"));
}
