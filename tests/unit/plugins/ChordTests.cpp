#include "MidiEffectRunner.h"
#include "plugins/midi/Chords.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace northlight;
using namespace northlight::test;
namespace p = northlight::plugins;

namespace
{
constexpr std::uint16_t cMajor = 0b1010'1011'0101;

std::vector<int> notesOf(const p::ChordNotes& c)
{
    return {c.notes.begin(), c.notes.begin() + c.count};
}
} // namespace

TEST_CASE("A chord by shape is inverted and voiced", "[plugins][midi][chord]")
{
    using namespace p::chord;
    CHECK(notesOf(byShape(60, major, 0, close)) == std::vector<int>{60, 64, 67});
    CHECK(notesOf(byShape(60, minor7, 0, close)) == std::vector<int>{60, 63, 67, 70});
    CHECK(notesOf(byShape(60, major, 1, close)) == std::vector<int>{64, 67, 72});
    CHECK(notesOf(byShape(60, major7, 0, drop2)) == std::vector<int>{55, 60, 64, 71});
    CHECK(notesOf(byShape(60, major, 0, open)) == std::vector<int>{60, 67, 76});
}

TEST_CASE("A chord in the key stacks the scale's thirds from the key pressed", "[plugins][midi][chord]")
{
    using namespace p::chord;
    CHECK(notesOf(inKey(62, 0, 0, cMajor, 0, close)) == std::vector<int>{62, 65, 69});     // D minor
    CHECK(notesOf(inKey(67, 1, 0, cMajor, 0, close)) == std::vector<int>{67, 71, 74, 77}); // G7
    CHECK(notesOf(inKey(71, 0, 0, cMajor, 0, close)) == std::vector<int>{71, 74, 77});     // B diminished
    CHECK(notesOf(inKey(57, 0, 9, cMajor, 0, close)) == std::vector<int>{57, 60, 64}); // A minor in A minor
}

TEST_CASE("Chord plays its notes with the key and ends them all on release", "[plugins][midi][chord]")
{
    EffectRunner r(p::chord::create());
    const auto out = r.runUntil(1024, {{0, noteOn(0, 60)}, {100, noteOff(0, 60)}});
    REQUIRE(out.size() == 6);
    CHECK(out[0].isOn(60));
    CHECK(out[1].isOn(64));
    CHECK(out[2].isOn(67));
    for (int i = 3; i < 6; ++i)
    {
        CHECK(out[static_cast<size_t>(i)].event.type == engine::MidiEvent::Type::noteOff);
        CHECK(out[static_cast<size_t>(i)].at == 100);
    }
}

TEST_CASE("A strummed chord starts low to high, and a quick release still ends every note",
          "[plugins][midi][chord]")
{
    EffectRunner r(p::chord::create());
    r.effect().setParameter(p::chord::strum, 1.0f);
    r.effect().setParameter(p::chord::strumTime, 30.0f); // 1440 samples
    const auto out = r.runUntil(5000, {{0, noteOn(0, 60)}, {100, noteOff(0, 60)}});
    const auto ons = onsOf(out);
    REQUIRE(ons.size() == 3);
    CHECK(ons[0].at == 0);
    CHECK(ons[1].at == 1440);
    CHECK(ons[2].isOn(67));
    CHECK(ons[2].at == 2880);
    int sounding = 0;
    for (const auto& e : out)
        sounding += e.isNoteOn() ? 1 : (e.event.type == engine::MidiEvent::Type::noteOff ? -1 : 0);
    CHECK(sounding == 0);
}

TEST_CASE("Chord Pads play the key's chords on the white keys and borrowed ones on the black",
          "[plugins][midi][chordpads]")
{
    using p::chordpads::padChord;
    CHECK(notesOf(padChord(60, 0, 0, cMajor, 54)) == std::vector<int>{60, 64, 67});     // I
    CHECK(notesOf(padChord(65, 0, 0, cMajor, 54)) == std::vector<int>{65, 69, 72});     // IV
    CHECK(notesOf(padChord(67, 1, 0, cMajor, 54)) == std::vector<int>{55, 59, 62, 65}); // V7
    CHECK(notesOf(padChord(62, 0, 0, cMajor, 54)) == std::vector<int>{62, 65, 69});     // ii
    CHECK(notesOf(padChord(66, 0, 0, cMajor, 54)) == std::vector<int>{65, 68, 72});     // iv, borrowed
    CHECK(notesOf(padChord(70, 0, 0, cMajor, 54)) == std::vector<int>{58, 62, 65});     // bVII
    // The same key in another octave plays the same chord.
    CHECK(notesOf(padChord(48, 0, 0, cMajor, 54)) == notesOf(padChord(60, 0, 0, cMajor, 54)));
    // In G: C is the G chord.
    CHECK(notesOf(padChord(60, 0, 7, 0b1010'1101'0101, 54)) == std::vector<int>{55, 59, 62});
}

TEST_CASE("Voice leading takes the inversion nearest the last chord", "[plugins][midi][chordpads]")
{
    using namespace p::chordpads;
    const auto c = padChord(60, 0, 0, cMajor, 54); // C E G
    const auto f = padChord(65, 0, 0, cMajor, 54); // F A C
    const auto led = leadFrom(f, c, 54);
    CHECK(notesOf(led) == std::vector<int>{60, 65, 69}); // C stays, E-F, G-A
    // With nothing before, it sits near the middle.
    const auto first = leadFrom(c, {}, 54);
    REQUIRE(first.count == 3);
    CHECK(first.notes[0] >= 48);
    CHECK(first.notes[2] <= 72);
}

TEST_CASE("Chord Pads add the bass note and end everything on release", "[plugins][midi][chordpads]")
{
    EffectRunner r(p::chordpads::create());
    r.head.keyRoot = 0;
    r.head.keyMask = cMajor;
    r.effect().setParameter(p::chordpads::bass, 1.0f);
    const auto out = r.runUntil(1024, {{0, noteOn(0, 60)}, {200, noteOff(0, 60)}});
    const auto ons = onsOf(out);
    REQUIRE(ons.size() == 4);
    CHECK(ons[0].isOn(48)); // C below the chord
    int sounding = 0;
    for (const auto& e : out)
        sounding += e.isNoteOn() ? 1 : (e.event.type == engine::MidiEvent::Type::noteOff ? -1 : 0);
    CHECK(sounding == 0);
}
