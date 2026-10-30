#include "MidiEffectRunner.h"
#include "plugins/midi/TimeEffects.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;
using namespace northlight::test;
namespace p = northlight::plugins;

// At 120 bpm and 48 kHz a quarter is 24000 samples: a 1/16 step is 6000, a 1/8 12000.

TEST_CASE("The arpeggiator's orders walk up, down and back without repeating the ends",
          "[plugins][midi][arp]")
{
    using p::arpeggiator::position;
    std::vector<int> up, down, upDown, downUp;
    for (int k = 0; k < 6; ++k)
    {
        up.push_back(position(p::arpeggiator::up, k, 3));
        down.push_back(position(p::arpeggiator::down, k, 3));
        upDown.push_back(position(p::arpeggiator::upDown, k, 3));
        downUp.push_back(position(p::arpeggiator::downUp, k, 3));
    }
    CHECK(up == std::vector<int>{0, 1, 2, 0, 1, 2});
    CHECK(down == std::vector<int>{2, 1, 0, 2, 1, 0});
    CHECK(upDown == std::vector<int>{0, 1, 2, 1, 0, 1});
    CHECK(downUp == std::vector<int>{2, 1, 0, 1, 2, 1});
    CHECK(position(p::arpeggiator::upDown, 5, 1) == 0); // one note: always it
}

TEST_CASE("An arpeggiator step names its keys and octave, which the editor draws as played",
          "[plugins][midi][arp]")
{
    namespace a = p::arpeggiator;
    // Three keys over two octaves, going up: keys 0, 1, 2, then 0, 1, 2 an octave up.
    const auto fourth = a::stepAt(a::up, 3, 3, 2);
    CHECK(fourth.first == 0);
    CHECK(fourth.count == 1);
    CHECK(fourth.octave == 1);
    CHECK(a::stepAt(a::down, 0, 3, 2).first == 2);
    CHECK(a::stepAt(a::down, 0, 3, 2).octave == 1);
    // Chord plays every key, an octave up on each step until the range wraps.
    const auto chord = a::stepAt(a::chord, 3, 3, 2);
    CHECK(chord.first == 0);
    CHECK(chord.count == 3);
    CHECK(chord.octave == 1);
    // Random plays the position it drew, kept inside the range.
    CHECK(a::stepAt(a::random, 0, 3, 2, 4).first == 1);
    CHECK(a::stepAt(a::random, 0, 3, 2, 4).octave == 1);
    CHECK(a::stepAt(a::random, 0, 3, 2, 99).octave == 1);
    // Nothing held plays nothing.
    CHECK(a::stepAt(a::up, 0, 0, 1).count == 0);
    // Only As played keeps the order the keys went down in.
    CHECK_FALSE(a::sortsByPitch(a::asPlayed));
    CHECK(a::sortsByPitch(a::upDown));
}

TEST_CASE("Stopped, the arpeggiator plays held notes from the first key at the project tempo",
          "[plugins][midi][arp]")
{
    EffectRunner r(p::arpeggiator::create());
    const auto out = r.runUntil(26000,
                                {{0, noteOn(0, 64)},
                                 {0, noteOn(0, 60)},
                                 {0, noteOn(0, 67)},
                                 {20000, noteOff(0, 60)},
                                 {20000, noteOff(0, 64)},
                                 {20000, noteOff(0, 67)}});
    const auto ons = onsOf(out);
    REQUIRE(ons.size() == 4); // 0, 6000, 12000, 18000; released before 24000
    CHECK(ons[0].isOn(60));
    CHECK(ons[0].at == 0);
    CHECK(ons[1].isOn(64));
    CHECK(ons[1].at == 6000);
    CHECK(ons[2].isOn(67));
    CHECK(ons[3].isOn(60));
    CHECK(ons[3].at == 18000);
    // Each step lasts half a step.
    for (const auto& e : out)
        if (e.isOff(64))
            CHECK(e.at == 9000);
}

TEST_CASE("Playing, the arpeggiator's steps fall on the song's grid", "[plugins][midi][arp]")
{
    EffectRunner r(p::arpeggiator::create());
    r.head.playing = true;
    r.head.quarterNotes = 0.1; // between grid steps
    const auto out = r.runUntil(12000, {{0, noteOn(0, 60)}});
    const auto ons = onsOf(out);
    REQUIRE(ons.size() == 2);
    CHECK(ons[0].at == 3600); // 1/16 = 0.25 quarters: (0.25 - 0.1) * 24000
    CHECK(ons[1].at == 9600);
}

TEST_CASE("The arpeggiator spans octaves, latches and plays chords", "[plugins][midi][arp]")
{
    {
        EffectRunner r(p::arpeggiator::create());
        r.effect().setParameter(p::arpeggiator::octaves, 2.0f);
        const auto ons = onsOf(r.runUntil(23552, {{0, noteOn(0, 60)}, {0, noteOn(0, 64)}}));
        REQUIRE(ons.size() == 4);
        CHECK(ons[0].isOn(60));
        CHECK(ons[1].isOn(64));
        CHECK(ons[2].isOn(72));
        CHECK(ons[3].isOn(76));
    }
    {
        EffectRunner r(p::arpeggiator::create());
        r.effect().setParameter(p::arpeggiator::latch, 1.0f);
        // Released at once, it keeps playing; the next key replaces the chord.
        auto ons = onsOf(r.runUntil(20480, {{0, noteOn(0, 60)}, {10, noteOff(0, 60)}}));
        CHECK(ons.size() == 4);
        ons = onsOf(r.runUntil(48000, {{20480, noteOn(0, 72)}, {20490, noteOff(0, 72)}}));
        REQUIRE_FALSE(ons.empty());
        for (const auto& e : ons)
            CHECK(e.isOn(72));
    }
    {
        EffectRunner r(p::arpeggiator::create());
        r.effect().setParameter(p::arpeggiator::mode, p::arpeggiator::chord);
        const auto ons = onsOf(r.runUntil(11776, {{0, noteOn(0, 60)}, {0, noteOn(0, 67)}}));
        REQUIRE(ons.size() == 4); // both notes, twice
        CHECK(ons[0].at == ons[1].at);
        CHECK(ons[2].at == 6000);
    }
}

TEST_CASE("An all-notes-off stops the arpeggiator and ends its note", "[plugins][midi][arp]")
{
    EffectRunner r(p::arpeggiator::create());
    r.effect().setParameter(p::arpeggiator::gate, 200.0f);
    r.runUntil(1024, {{0, noteOn(0, 60)}});
    const auto out = r.block({{0, engine::MidiEvent::Type::allNotesOff, 0, 0, 0}});
    bool ended = false;
    for (const auto& e : out)
        ended = ended || e.isOff(60);
    CHECK(ended);
    CHECK(onsOf(r.runUntil(30000)).empty());
}

TEST_CASE("Note Repeat repeats a held note in time, ramping its velocity", "[plugins][midi][repeat]")
{
    EffectRunner r(p::noterepeat::create());
    r.effect().setParameter(p::noterepeat::ramp, -50.0f);
    const auto ons = onsOf(r.runUntil(17000, {{0, noteOn(0, 36, 100)}, {16000, noteOff(0, 36)}}));
    REQUIRE(ons.size() == 3);
    CHECK(ons[0].at == 0);
    CHECK(ons[0].event.velocity == 100);
    CHECK(ons[1].at == 6000);
    CHECK(ons[1].event.velocity == 50);
    CHECK(ons[2].at == 12000);
    CHECK(ons[2].event.velocity == 25);
}

TEST_CASE("Note Echo repeats each note, fading and stepping in pitch, each as long as the note",
          "[plugins][midi][echo]")
{
    EffectRunner r(p::noteecho::create());
    r.effect().setParameter(p::noteecho::feedback, 50.0f);
    r.effect().setParameter(p::noteecho::pitch, 12.0f);
    const auto out = r.runUntil(40000, {{0, noteOn(0, 60, 100)}, {1000, noteOff(0, 60)}});
    const auto ons = onsOf(out);
    REQUIRE(ons.size() == 4);
    CHECK(ons[1].isOn(72));
    CHECK(ons[1].at == 12000);
    CHECK(ons[1].event.velocity == 50);
    CHECK(ons[2].isOn(84));
    CHECK(ons[2].at == 24000);
    CHECK(ons[3].isOn(96));
    for (const auto& e : out)
    {
        if (e.isOff(72))
            CHECK(e.at == 13000);
        if (e.isOff(96))
            CHECK(e.at == 37000);
    }
    int sounding = 0;
    for (const auto& e : out)
        sounding += e.isNoteOn() ? 1 : (e.event.type == engine::MidiEvent::Type::noteOff ? -1 : 0);
    CHECK(sounding == 0);
}

TEST_CASE("Note Echo's repeats end as they were set, even after a change", "[plugins][midi][echo]")
{
    EffectRunner r(p::noteecho::create());
    r.effect().setParameter(p::noteecho::pitch, 7.0f);
    auto out = r.runUntil(1024, {{0, noteOn(0, 60)}});
    r.effect().setParameter(p::noteecho::pitch, -5.0f);
    r.effect().setParameter(p::noteecho::repeats, 1.0f);
    const auto rest = r.runUntil(60000, {{2000, noteOff(0, 60)}});
    out.insert(out.end(), rest.begin(), rest.end());
    int sounding = 0;
    for (const auto& e : out)
        sounding += e.isNoteOn() ? 1 : (e.event.type == engine::MidiEvent::Type::noteOff ? -1 : 0);
    CHECK(sounding == 0); // 67, 74 and 81 all ended
}
