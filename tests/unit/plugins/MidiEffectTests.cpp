#include "engine/Midi.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/midi/Chords.h"
#include "plugins/midi/MidiEffectBase.h"
#include "plugins/midi/TimeEffects.h"
#include "plugins/midi/Transpose.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <vector>

using namespace northlight;
using engine::MidiEvent;
using engine::MidiEventList;

namespace
{
constexpr std::uint16_t cMajor = 0b1010'1011'0101; // C D E F G A B

MidiEvent on(int offset, int note, int velocity = 100, int channel = 0)
{
    return {offset,
            MidiEvent::Type::noteOn,
            static_cast<std::uint8_t>(channel),
            static_cast<std::uint8_t>(note),
            static_cast<std::uint8_t>(velocity)};
}
MidiEvent off(int offset, int note, int channel = 0)
{
    return {offset,
            MidiEvent::Type::noteOff,
            static_cast<std::uint8_t>(channel),
            static_cast<std::uint8_t>(note),
            0};
}

// Runs one block through an effect and returns what it made.
struct Runner
{
    std::unique_ptr<engine::InsertProcessor> effect;
    MidiEventList in{256}, out{256};
    engine::PlayHead head;
    explicit Runner(std::unique_ptr<engine::InsertProcessor> e) : effect(std::move(e))
    {
        effect->prepare(48000.0, 64);
    }
    std::vector<MidiEvent> block(std::vector<MidiEvent> events, int length = 64)
    {
        in.clear();
        out.clear();
        for (const auto& e : events)
            in.push(e);
        effect->setPlayHead(head);
        effect->processMidiEffect(in, out, length);
        return {out.begin(), out.end()};
    }
};

bool isOn(const MidiEvent& e, int note)
{
    return e.type == MidiEvent::Type::noteOn && e.note == note && e.velocity > 0;
}
bool isOff(const MidiEvent& e, int note)
{
    return e.type == MidiEvent::Type::noteOff && e.note == note;
}

// "test.later": plays every note-on and note-off `delay` samples after it
// arrives, through the base's queue, so a test can follow events across blocks.
class Later final : public plugins::midi::MidiEffectBase
{
public:
    explicit Later(int delay) : MidiEffectBase("test.later", {}), delay_(delay) {}

private:
    void noteOn(const MidiEvent& e) noexcept override { schedule(blockClock() + e.sampleOffset + delay_, e); }
    void noteOff(const MidiEvent& e) noexcept override
    {
        schedule(blockClock() + e.sampleOffset + delay_, e);
    }

    int delay_;
};
} // namespace

TEST_CASE("A step of the key moves to the next scale note; with no key it is a semitone", "[plugins][midi]")
{
    using plugins::transpose::stepInScale;
    CHECK(stepInScale(60, 2, cMajor) == 64);  // C up two steps: E
    CHECK(stepInScale(64, 1, cMajor) == 65);  // E up one: F (a semitone)
    CHECK(stepInScale(60, -1, cMajor) == 59); // C down one: B
    CHECK(stepInScale(60, 7, cMajor) == 72);  // a whole octave of steps
    CHECK(stepInScale(61, 1, cMajor) == 63);  // C#: from C to D, keeping the sharp
    CHECK(stepInScale(60, 3, 0) == 63);       // no key: semitones
    CHECK(stepInScale(60, 0, cMajor) == 60);
}

TEST_CASE("Transpose moves notes, and a note-off ends what its note-on started after a change",
          "[plugins][midi]")
{
    Runner r(plugins::transpose::create());
    REQUIRE(r.effect->isMidiEffect());
    r.effect->setParameter(plugins::transpose::octaves, 1.0f);
    auto out = r.block({on(5, 60)});
    REQUIRE(out.size() == 1);
    CHECK(isOn(out[0], 72));
    CHECK(out[0].sampleOffset == 5);
    CHECK(out[0].velocity == 100);

    // The setting changes while the key is down: the release still ends 72.
    r.effect->setParameter(plugins::transpose::octaves, 0.0f);
    r.effect->setParameter(plugins::transpose::semitones, 7.0f);
    out = r.block({off(3, 60)});
    REQUIRE(out.size() == 1);
    CHECK(isOff(out[0], 72));
    CHECK(out[0].sampleOffset == 3);

    // Steps follow the project's key from the play head.
    r.effect->setParameter(plugins::transpose::semitones, 0.0f);
    r.effect->setParameter(plugins::transpose::steps, 2.0f);
    r.head.keyRoot = 0;
    r.head.keyMask = cMajor;
    out = r.block({on(0, 64)});
    REQUIRE(out.size() == 1);
    CHECK(isOn(out[0], 67)); // E up two steps: G
}

TEST_CASE("A note moved out of range plays nothing, and its release ends nothing", "[plugins][midi]")
{
    Runner r(plugins::transpose::create());
    r.effect->setParameter(plugins::transpose::octaves, 4.0f);
    auto out = r.block({on(0, 120), on(1, 60)}); // 168: out of range; 60 -> 108
    REQUIRE(out.size() == 1);
    CHECK(isOn(out[0], 108));
    r.effect->setParameter(plugins::transpose::octaves, 0.0f);
    // 120's release must not end anything, even a note 120 that is sounding
    // (here none is, and 108 keeps sounding).
    out = r.block({off(0, 120)});
    CHECK(out.empty());
    CHECK(r.effect != nullptr);
    CHECK(static_cast<plugins::midi::MidiEffectBase*>(r.effect.get())->soundingCount() == 1);
}

TEST_CASE("Two keys moved onto one note hold it until both are released", "[plugins][midi]")
{
    Runner r(plugins::transpose::create());
    r.effect->setParameter(plugins::transpose::octaves, 1.0f);
    auto out = r.block({on(0, 60)}); // 72
    r.effect->setParameter(plugins::transpose::octaves, 0.0f);
    out = r.block({on(0, 72)}); // 72 again
    REQUIRE(out.size() == 1);
    CHECK(isOn(out[0], 72));
    out = r.block({off(0, 60)});
    CHECK(out.empty()); // the other key still holds it
    out = r.block({off(0, 72)});
    REQUIRE(out.size() == 1);
    CHECK(isOff(out[0], 72));
}

TEST_CASE("Switching bypass ends what the effect was holding, both ways", "[plugins][midi]")
{
    Runner r(plugins::transpose::create());
    r.effect->setParameter(plugins::transpose::octaves, 1.0f);
    r.block({on(0, 60)}); // 72
    r.effect->setBypassed(true);
    auto out = r.block({});
    REQUIRE(out.size() == 1);
    CHECK(isOff(out[0], 72));
    CHECK(out[0].sampleOffset == 0);
    // Bypassed, notes pass as they are; the old key's release ends nothing.
    out = r.block({off(0, 60), on(4, 64)});
    REQUIRE(out.size() == 1);
    CHECK(isOn(out[0], 64));
    // Back on: the note that passed while bypassed ends.
    r.effect->setBypassed(false);
    out = r.block({});
    REQUIRE(out.size() == 1);
    CHECK(isOff(out[0], 64));
    // And its release afterwards ends nothing more.
    out = r.block({off(0, 64)});
    CHECK(out.empty());
}

TEST_CASE("An all-notes-off ends every note, passes on and empties the queue", "[plugins][midi]")
{
    Runner r(std::make_unique<Later>(100));
    // A note on at 10 plays at 110 (the next block, offset 46).
    auto out = r.block({on(10, 60)});
    CHECK(out.empty());
    out = r.block({});
    REQUIRE(out.size() == 1);
    CHECK(isOn(out[0], 60));
    CHECK(out[0].sampleOffset == 46);
    // Another, queued for later, then a jump: 60 ends, the queued note never plays.
    out = r.block({on(0, 62), {5, MidiEvent::Type::allNotesOff, 0, 0, 0}});
    REQUIRE(out.size() == 2);
    CHECK(isOff(out[0], 60));
    CHECK(out[0].sampleOffset == 5);
    CHECK(out[1].type == MidiEvent::Type::allNotesOff);
    for (int b = 0; b < 4; ++b)
        CHECK(r.block({}).empty());
}

TEST_CASE("Queued events land at their offsets across block boundaries, in time order", "[plugins][midi]")
{
    Runner r(std::make_unique<Later>(100));
    // Ons at 0, 30 and 50 play at 100, 130 and 150: block 1 offset 36, then
    // block 2 offsets 2 and 22.
    CHECK(r.block({on(0, 60), on(30, 62), on(50, 64)}).empty());
    auto out = r.block({});
    REQUIRE(out.size() == 1);
    CHECK(isOn(out[0], 60));
    CHECK(out[0].sampleOffset == 36);
    out = r.block({});
    REQUIRE(out.size() == 2);
    CHECK(isOn(out[0], 62));
    CHECK(out[0].sampleOffset == 2);
    CHECK(isOn(out[1], 64));
    CHECK(out[1].sampleOffset == 22);
}

TEST_CASE("Controllers pass through, and poly pressure follows the note it belongs to", "[plugins][midi]")
{
    Runner r(plugins::transpose::create());
    r.effect->setParameter(plugins::transpose::semitones, 2.0f);
    MidiEvent cc{1, MidiEvent::Type::controller, 0, 1, 64};
    MidiEvent pressure{2, MidiEvent::Type::polyPressure, 0, 60, 90};
    auto out = r.block({on(0, 60), cc, pressure});
    REQUIRE(out.size() == 3);
    CHECK(isOn(out[0], 62));
    CHECK(out[1].type == MidiEvent::Type::controller);
    CHECK(out[1].note == 1);
    CHECK(out[2].type == MidiEvent::Type::polyPressure);
    CHECK(out[2].note == 62);
}

TEST_CASE("A full output list drops events without starting notes it cannot end", "[plugins][midi]")
{
    auto effect = plugins::transpose::create();
    effect->prepare(48000.0, 64);
    MidiEventList in(64), out(4);
    for (int n = 0; n < 8; ++n)
        in.noteOn(n, 60 + n, 100);
    effect->processMidiEffect(in, out, 64);
    CHECK(out.size() == 4);
    CHECK(static_cast<plugins::midi::MidiEffectBase*>(effect.get())->soundingCount() == 4);
}

TEST_CASE("MIDI effects are built-ins of their own kind, kept out of the audio lists", "[plugins][midi]")
{
    using plugins::BuiltInFactory;
    CHECK(BuiltInFactory::isMidiEffect(plugins::transpose::type));
    CHECK_FALSE(BuiltInFactory::isMidiEffect("northlight.eq"));
    CHECK_FALSE(BuiltInFactory::isInstrument(plugins::transpose::type));
    const auto audio = BuiltInFactory().typeNames();
    CHECK(std::find(audio.begin(), audio.end(), plugins::transpose::type) == audio.end());
    const auto midi = BuiltInFactory::midiEffectTypeNames();
    CHECK(std::find(midi.begin(), midi.end(), plugins::transpose::type) != midi.end());
    const auto made = BuiltInFactory().create(plugins::transpose::type);
    REQUIRE(made != nullptr);
    CHECK(made->isMidiEffect());
    CHECK(BuiltInFactory::displayName(plugins::transpose::type) == "Transpose");
    CHECK(plugins::defaultParameters(plugins::transpose::type).size() == plugins::transpose::count);
}

namespace
{
#if defined(NORTHLIGHT_RT_CHECKS)
int effectViolations = 0;
void countEffectViolation(const rt::Violation&) noexcept
{
    ++effectViolations;
}
#endif

// Plays an effect hard for a while: keys up and down, the pedal, and (when
// `moving`) its settings changing. The keys are all up by block 500 and the run
// ends `tail` blocks later. Returns how many notes it still had sounding then.
int playHard(const std::string& type, bool moving, int tail)
{
    auto effect = plugins::BuiltInFactory().create(type);
    REQUIRE(effect != nullptr);
    effect->prepare(48000.0, 256);
    auto* base = dynamic_cast<plugins::midi::MidiEffectBase*>(effect.get());
    REQUIRE(base != nullptr);
    const auto& infos = effect->parameters();
    MidiEventList in(256), out(8192);
    engine::PlayHead head;
    head.keyRoot = 0;
    head.keyMask = 0b1010'1011'0101;
    std::uint32_t seed = 12345;
    const auto next = [&seed]
    {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    bool down[128]{};
#if defined(NORTHLIGHT_RT_CHECKS)
    effectViolations = 0;
    const auto previous = rt::setViolationHandler(&countEffectViolation);
#endif
    for (int block = 0; block < 500 + tail; ++block)
    {
        in.clear();
        out.clear();
        if (moving && block % 37 == 0 && block < 500 && !infos.empty())
        {
            const auto index = static_cast<int>(next() % infos.size());
            const auto& info = infos[static_cast<size_t>(index)];
            const float t = static_cast<float>(next() % 1000) / 999.0f;
            // Latch would keep playing after the keys are up; leave it off.
            if (info.name != "Latch")
                effect->setParameter(index, info.minimum + t * (info.maximum - info.minimum));
        }
        const bool releasing = block >= 480;
        for (int k = 0; k < 4; ++k)
        {
            const int offset = static_cast<int>(next() % 256);
            const int note = 36 + static_cast<int>(next() % 48);
            if (down[note])
            {
                in.noteOff(offset, note);
                down[note] = false;
            }
            else if (!releasing && next() % 3 == 0)
            {
                in.noteOn(offset, note, 1 + static_cast<int>(next() % 127));
                down[note] = true;
            }
        }
        if (block == 499)
            for (int note = 0; note < 128; ++note)
                if (down[note])
                {
                    in.noteOff(255, note);
                    down[note] = false;
                }
        if (block % 50 == 7 && block < 480)
            in.controller(0, 64, block % 100 < 50 ? 127 : 0);
        if (block == 499)
            in.controller(255, 64, 0); // the pedal up with the last keys
        in.sortByTime();
        {
            rt::ScopedRealtimeSection section;
            effect->setPlayHead(head);
            effect->processMidiEffect(in, out, 256);
        }
        for (size_t i = 0; i < out.size(); ++i)
        {
            CHECK(out[i].sampleOffset >= 0);
            CHECK(out[i].sampleOffset < 256);
            if (i > 0)
                CHECK(out[i - 1].sampleOffset <= out[i].sampleOffset);
        }
    }
#if defined(NORTHLIGHT_RT_CHECKS)
    rt::setViolationHandler(previous);
    CHECK(effectViolations == 0);
#endif
    const int sounding = base->soundingCount();
    // A stop ends whatever is left.
    in.clear();
    out.clear();
    in.allNotesOff(0);
    effect->processMidiEffect(in, out, 256);
    CHECK(base->soundingCount() == 0);
    return sounding;
}
} // namespace

TEST_CASE("Every MIDI effect, played hard with its settings moving, never allocates and a stop silences it",
          "[plugins][midi][stress]")
{
    const auto types = plugins::BuiltInFactory::midiEffectTypeNames();
    REQUIRE(types.size() >= 12);
    for (const auto& type : types)
    {
        CAPTURE(type);
        const auto effect = plugins::BuiltInFactory().create(type);
        REQUIRE(effect != nullptr);
        CHECK(effect->isMidiEffect());
        CHECK(effect->midiExpansion() >= 1);
        for (const auto& info : effect->parameters())
        {
            CHECK_FALSE(info.name.empty());
            CHECK(info.minimum <= info.defaultValue);
            CHECK(info.defaultValue <= info.maximum);
            if (!info.choices.empty())
                CHECK(info.maximum == static_cast<float>(info.choices.size() - 1));
        }
        playHard(type, true, 20);
    }
}

TEST_CASE("Every MIDI effect at its defaults falls silent once the keys are up", "[plugins][midi][stress]")
{
    for (const auto& type : plugins::BuiltInFactory::midiEffectTypeNames())
    {
        CAPTURE(type);
        CHECK(playHard(type, false, 600) == 0); // 600 blocks: over three seconds of tail
    }
}

TEST_CASE("A MIDI effect's note feedback shows what is held, what sounds and the last key's notes",
          "[plugins][midi][feedback]")
{
    using engine::NoteFeedback;
    Runner r(plugins::transpose::create());
    r.effect->setParameter(plugins::transpose::octaves, 1.0f);
    const auto* n = r.effect->noteFeedback();
    REQUIRE(n != nullptr);
    const auto before = n->changes.load();
    r.block({on(0, 60, 90)});
    CHECK(n->changes.load() != before);
    CHECK(NoteFeedback::test(n->held, 60));
    CHECK(NoteFeedback::test(n->sounding, 72));
    CHECK_FALSE(NoteFeedback::test(n->sounding, 60));
    CHECK(NoteFeedback::test(n->last, 72));
    CHECK(n->lastInput.load() == 60);
    REQUIRE(n->velocityCount.load() == 1);
    CHECK(n->velocities[0].load() == (90 << 8 | 90));
    r.block({off(0, 60)});
    CHECK_FALSE(NoteFeedback::test(n->held, 60));
    CHECK_FALSE(NoteFeedback::test(n->sounding, 72));
    CHECK(NoteFeedback::test(n->last, 72)); // the last chord stays to be read
    // A stop clears what sounds.
    r.block({on(0, 64)});
    r.block({{0, MidiEvent::Type::allNotesOff, 0, 0, 0}});
    CHECK_FALSE(NoteFeedback::test(n->sounding, 76));
}

TEST_CASE("A strummed chord's feedback names all its notes, and the arpeggiator its order and step",
          "[plugins][midi][feedback]")
{
    using engine::NoteFeedback;
    {
        Runner r(plugins::chord::create());
        r.effect->setParameter(plugins::chord::strum, 1.0f);
        r.block({on(0, 60)});
        const auto* n = r.effect->noteFeedback();
        CHECK(NoteFeedback::test(n->last, 60));
        CHECK(NoteFeedback::test(n->last, 64)); // not played yet, but the key's
        CHECK(NoteFeedback::test(n->last, 67));
    }
    {
        Runner r(plugins::arpeggiator::create());
        r.block({on(0, 67), on(0, 60)});
        const auto* n = r.effect->noteFeedback();
        REQUIRE(n->orderCount.load() == 2);
        CHECK(n->order[0].load() == 67); // as pressed
        CHECK(n->order[1].load() == 60);
        CHECK(n->step.load() == 0);
        r.block({off(0, 67), off(0, 60)});
        CHECK(n->orderCount.load() == 2); // the last chord's pattern stays
    }
}
