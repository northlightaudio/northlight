#include "MidiEffectRunner.h"
#include "plugins/midi/MidiEffectBase.h"
#include "plugins/midi/Utilities.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;
using namespace northlight::test;
namespace p = northlight::plugins;

namespace
{
constexpr std::uint16_t cMajor = 0b1010'1011'0101;
constexpr std::uint16_t aMinor = 0b1010'1011'0101; // the same notes, rooted on A
} // namespace

TEST_CASE("Scale moves a note into the key by its mode", "[plugins][midi][scale]")
{
    using p::scale::place;
    CHECK(place(60, p::scale::nearest, 0, cMajor) == 60);
    CHECK(place(61, p::scale::nearest, 0, cMajor) == 60); // a tie goes down
    CHECK(place(61, p::scale::up, 0, cMajor) == 62);
    CHECK(place(61, p::scale::down, 0, cMajor) == 60);
    CHECK(place(61, p::scale::block, 0, cMajor) == -1);
    CHECK(place(62, p::scale::block, 0, cMajor) == 62);
    // White keys: C D E of an octave play the scale's first three degrees from the root.
    CHECK(place(60, p::scale::whiteKeys, 9, aMinor) == 69); // A
    CHECK(place(62, p::scale::whiteKeys, 9, aMinor) == 71); // B
    CHECK(place(64, p::scale::whiteKeys, 9, aMinor) == 72); // C
    CHECK(place(61, p::scale::whiteKeys, 9, aMinor) == -1); // a black key plays nothing
    // No key: the note as it is.
    CHECK(place(61, p::scale::nearest, 0, 0) == 61);
}

TEST_CASE("Scale follows the project's key, or its own, and a blocked key's release ends nothing",
          "[plugins][midi][scale]")
{
    EffectRunner r(p::scale::create());
    r.effect().setParameter(p::scale::mode, p::scale::up);
    r.head.keyRoot = 2; // D major: F# and C#
    r.head.keyMask = 0b1010'1101'0110;
    auto out = r.block({noteOn(0, 65)}); // F -> F#
    REQUIRE(out.size() == 1);
    CHECK(out[0].isOn(66));
    // Its own key instead: C major keeps F. Pressing F again (no release between)
    // first ends the F# its first press started.
    r.effect().setParameter(p::scale::follow, 0.0f);
    out = r.block({noteOn(0, 65), noteOff(1, 65)});
    REQUIRE(out.size() == 3);
    CHECK(out[0].isOff(66));
    CHECK(out[1].isOn(65));
    CHECK(out[2].isOff(65));
    // Blocked, a key plays nothing and its release ends nothing.
    r.effect().setParameter(p::scale::mode, p::scale::block);
    CHECK(r.block({noteOn(0, 61), noteOff(5, 61)}).empty());
}

TEST_CASE("Velocity shapes note-on velocities and leaves the releases alone", "[plugins][midi][velocity]")
{
    using p::velocity::shape;
    p::velocity::Settings s;
    CHECK(shape(100, s) == 100);
    s.dynamics = 50.0f; // halfway to the centre
    CHECK(shape(100, s) == 82);
    CHECK(shape(28, s) == 46);
    s = {};
    s.gain = 50.0f;
    s.offset = 10.0f;
    CHECK(shape(100, s) == 60);
    s = {};
    s.minimum = 40.0f;
    s.maximum = 90.0f;
    CHECK(shape(10, s) == 40);
    CHECK(shape(120, s) == 90);
    CHECK(shape(64, s, 100.0f) == 90); // random is clamped too
    s.fixed = true;
    s.fixedVelocity = 77.0f;
    CHECK(shape(5, s) == 77);

    EffectRunner r(p::velocity::create());
    r.effect().setParameter(p::velocity::fixed, 1.0f);
    r.effect().setParameter(p::velocity::fixedVelocity, 33.0f);
    const auto out = r.block({noteOn(0, 60, 120), noteOff(10, 60)});
    REQUIRE(out.size() == 2);
    CHECK(out[0].event.velocity == 33);
    CHECK(out[1].isOff(60));
}

TEST_CASE("Key Range blocks the notes outside it, or folds them in by octaves", "[plugins][midi][keyrange]")
{
    EffectRunner r(p::keyrange::create());
    r.effect().setParameter(p::keyrange::low, 48.0f);
    r.effect().setParameter(p::keyrange::high, 72.0f);
    auto out = r.block({noteOn(0, 40), noteOn(1, 60), noteOff(2, 40)});
    REQUIRE(out.size() == 1);
    CHECK(out[0].isOn(60));
    r.effect().setParameter(p::keyrange::mode, 1.0f);
    out = r.block({noteOn(0, 40), noteOn(1, 90)});
    REQUIRE(out.size() == 2);
    CHECK(out[0].isOn(52)); // up an octave
    CHECK(out[1].isOn(66)); // down two
}

TEST_CASE("MIDI Filter blocks kinds of events and moves a controller", "[plugins][midi][filter]")
{
    EffectRunner r(p::midifilter::create());
    r.effect().setParameter(p::midifilter::pitchBend, 0.0f);
    r.effect().setParameter(p::midifilter::move, 1.0f);
    r.effect().setParameter(p::midifilter::mapFrom, 1.0f);
    r.effect().setParameter(p::midifilter::mapTo, 11.0f);
    engine::MidiEvent bend{3, engine::MidiEvent::Type::pitchBend, 0, 0, 64};
    auto out = r.block({controller(0, 1, 90), controller(1, 7, 100), bend});
    REQUIRE(out.size() == 2);
    CHECK(out[0].event.note == 11); // the mod wheel, moved to expression
    CHECK(out[0].event.velocity == 90);
    CHECK(out[1].event.note == 7);
    // Notes blocked: nothing, and the release ends nothing.
    r.effect().setParameter(p::midifilter::notes, 0.0f);
    CHECK(r.block({noteOn(0, 60), noteOff(4, 60)}).empty());
}

TEST_CASE("Note Length plays a fixed length whatever the release", "[plugins][midi][notelength]")
{
    EffectRunner r(p::notelength::create());
    r.effect().setParameter(p::notelength::mode, p::notelength::fixedTime);
    r.effect().setParameter(p::notelength::time, 100.0f); // 4800 samples
    const auto out = r.runUntil(10000, {{10, noteOn(0, 60)}, {200, noteOff(0, 60)}});
    REQUIRE(out.size() == 2);
    CHECK(out[0].isOn(60));
    CHECK(out[0].at == 10);
    CHECK(out[1].isOff(60));
    CHECK(out[1].at == 4810);
}

TEST_CASE("Note Length's legato holds each note until the next one starts; a chord stays together",
          "[plugins][midi][notelength]")
{
    EffectRunner r(p::notelength::create());
    r.effect().setParameter(p::notelength::mode, p::notelength::legato);
    const auto out = r.runUntil(2048,
                                {{0, noteOn(0, 60)},
                                 {0, noteOn(0, 64)},
                                 {100, noteOff(0, 60)},
                                 {100, noteOff(0, 64)},
                                 {700, noteOn(0, 67)}});
    REQUIRE(out.size() == 5);
    CHECK(out[0].isOn(60));
    CHECK(out[1].isOn(64));
    CHECK(out[2].isOff(60));
    CHECK(out[2].at == 700);
    CHECK(out[3].isOff(64));
    CHECK(out[4].isOn(67));
}

TEST_CASE("Note Length's pedal hold sustains releases until the pedal lifts, and keeps the pedal",
          "[plugins][midi][notelength]")
{
    EffectRunner r(p::notelength::create());
    r.effect().setParameter(p::notelength::pedal, 1.0f);
    const auto out = r.runUntil(2048,
                                {{0, noteOn(0, 60)},
                                 {10, controller(0, 64, 127)},
                                 {20, noteOff(0, 60)},
                                 {30, noteOn(0, 62)},
                                 {40, controller(0, 64, 0)},
                                 {600, noteOff(0, 62)}});
    REQUIRE(out.size() == 4);
    CHECK(out[0].isOn(60));
    CHECK(out[1].isOn(62));
    CHECK(out[2].isOff(60)); // when the pedal lifts
    CHECK(out[2].at == 40);
    CHECK(out[3].isOff(62)); // still held at the lift: it ends at its own release
    CHECK(out[3].at == 600);
}

TEST_CASE("Humanise delays a note and its release alike, drops notes by chance, and repeats from its seed",
          "[plugins][midi][humanise]")
{
    const auto run = []
    {
        EffectRunner r(p::humanise::create());
        r.effect().setParameter(p::humanise::timing, 10.0f); // up to 480 samples late
        r.effect().setParameter(p::humanise::velocity, 20.0f);
        std::vector<TimedEvent> in;
        for (int i = 0; i < 20; ++i)
        {
            in.push_back({i * 1000, noteOn(0, 60 + i)});
            in.push_back({i * 1000 + 300, noteOff(0, 60 + i)});
        }
        return r.runUntil(22000, in);
    };
    const auto first = run();
    REQUIRE(first.size() == 40);
    for (int i = 0; i < 20; ++i)
    {
        const TimedEvent* on = nullptr;
        const TimedEvent* off = nullptr;
        for (const auto& e : first)
        {
            if (e.isOn(60 + i))
                on = &e;
            if (e.isOff(60 + i))
                off = &e;
        }
        REQUIRE(on != nullptr);
        REQUIRE(off != nullptr);
        CHECK(on->at >= i * 1000);
        CHECK(on->at < i * 1000 + 480);
        CHECK(off->at - on->at == 300); // the length is kept
        CHECK(on->event.velocity >= 80);
        CHECK(on->event.velocity <= 120);
    }
    const auto second = run();
    REQUIRE(second.size() == first.size());
    for (size_t i = 0; i < first.size(); ++i)
    {
        CHECK(second[i].at == first[i].at);
        CHECK(second[i].event.velocity == first[i].event.velocity);
    }

    EffectRunner never(p::humanise::create());
    never.effect().setParameter(p::humanise::chance, 0.0f);
    CHECK(never.runUntil(4096, {{0, noteOn(0, 60)}, {100, noteOff(0, 60)}}).empty());
}
