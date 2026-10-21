#include "engine/Midi.h"
#include "plugins/BeatSync.h"
#include "plugins/BuiltIns.h"
#include "plugins/Starbit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace northlight;
using Catch::Approx;

namespace
{
constexpr double rate = 48000.0;
constexpr int block = 512;

int choiceNamed(const char* name)
{
    int found = -1;
    for (int i = 0; i < plugins::sync::choiceCount; ++i)
        if (std::string(plugins::sync::noteValues[static_cast<size_t>(i)].name) == name)
            found = i;
    REQUIRE(found >= 0);
    return found;
}

engine::PlayHead headAt(double tempo, std::int64_t samples, bool playing)
{
    engine::PlayHead h;
    h.tempoBpm = tempo;
    h.sampleRate = rate;
    h.samples = samples;
    h.quarterNotes = static_cast<double>(samples) * h.quartersPerSample();
    h.playing = playing;
    return h;
}

// The first echo of an impulse through the delay, in samples.
int echoAt(double tempo, int sync, float timeMs)
{
    namespace del = plugins::delay;
    auto delay = plugins::BuiltInFactory().create(plugins::types::delay);
    delay->prepare(rate, block);
    delay->setParameter(del::time, timeMs);
    delay->setParameter(del::feedback, 0.0f);
    delay->setParameter(del::tone, 20000.0f);
    delay->setParameter(del::mix, 100.0f);
    delay->setParameter(del::sync, static_cast<float>(sync));
    std::vector<float> l(block), r(block), out;
    for (int b = 0; b < 400; ++b) // past four seconds
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        if (b == 0)
            l[0] = r[0] = 1.0f;
        delay->setPlayHead(headAt(tempo, b * block, true));
        delay->process(l.data(), r.data(), block);
        out.insert(out.end(), l.begin(), l.end());
    }
    return static_cast<int>(std::max_element(out.begin(), out.end()) - out.begin());
}

// Zero crossings of `s` in [from, to).
int crossings(const std::vector<float>& s, int from, int to)
{
    int n = 0;
    for (int i = std::max(1, from); i < to; ++i)
        if ((s[static_cast<size_t>(i - 1)] < 0.0f) != (s[static_cast<size_t>(i)] < 0.0f))
            ++n;
    return n;
}

// Starbit holding middle C with the octave arpeggio, synced to `sync`, from
// song sample `start` (playing).
std::vector<float> arpeggio(double tempo, int sync, std::int64_t start, bool playing, int blocks)
{
    namespace sb = plugins::starbit;
    auto synth = plugins::BuiltInFactory().create(sb::type);
    synth->prepare(rate, block);
    synth->setParameter(sb::wave, sb::triangle);
    synth->setParameter(sb::attack, 0.0f);
    synth->setParameter(sb::sustain, 1.0f);
    synth->setParameter(sb::vibratoDepth, 0.0f);
    synth->setParameter(sb::bits, 16.0f);
    synth->setParameter(sb::rate, 48000.0f);
    synth->setParameter(sb::arp, sb::arpOctave);
    synth->setParameter(sb::arpRate, 2.0f); // far from the synced rate
    synth->setParameter(sb::arpSync, static_cast<float>(sync));
    std::vector<float> l(block), r(block), out;
    for (int b = 0; b < blocks; ++b)
    {
        engine::MidiEventList events;
        if (b == 0)
            events.push({0, engine::MidiEvent::Type::noteOn, 0, 60, 127});
        synth->setPlayHead(headAt(tempo, playing ? start + b * block : start, playing));
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        synth->processMidi(events, l.data(), r.data(), block);
        out.insert(out.end(), l.begin(), l.end());
    }
    return out;
}
} // namespace

TEST_CASE("Note values: lengths in quarter notes, triplets two thirds, dotted half again", "[plugins][sync]")
{
    namespace s = plugins::sync;
    CHECK(s::quarters(0.0f) == 0.0);
    CHECK(s::quarters(static_cast<float>(choiceNamed("1/4"))) == Approx(1.0));
    CHECK(s::quarters(static_cast<float>(choiceNamed("1/16"))) == Approx(0.25));
    CHECK(s::quarters(static_cast<float>(choiceNamed("1/8 T"))) == Approx(1.0 / 3.0));
    CHECK(s::quarters(static_cast<float>(choiceNamed("1/8 D"))) == Approx(0.75));
    CHECK(s::seconds(static_cast<float>(choiceNamed("1/4")), 120.0) == Approx(0.5));
    CHECK(s::nearest(0.26) == choiceNamed("1/16"));
    CHECK(s::nearest(0.7) == choiceNamed("1/4 T"));
}

TEST_CASE("A synced delay repeats at its note value at the project tempo", "[plugins][sync]")
{
    // Free: the time in ms.
    CHECK(echoAt(120.0, 0, 100.0f) == Approx(4800).margin(2));
    // 1/8 at 120 bpm: a quarter of a second, whatever the ms say.
    CHECK(echoAt(120.0, choiceNamed("1/8"), 100.0f) == Approx(12000).margin(2));
    // The same at 90 bpm, and a triplet and a dotted value.
    CHECK(echoAt(90.0, choiceNamed("1/8"), 100.0f) == Approx(16000).margin(2));
    CHECK(echoAt(120.0, choiceNamed("1/4 T"), 100.0f) == Approx(16000).margin(2));
    CHECK(echoAt(120.0, choiceNamed("1/8 D"), 100.0f) == Approx(18000).margin(2));
    // Longer than the old two seconds: a whole note at 60 bpm.
    CHECK(echoAt(60.0, choiceNamed("1/1"), 100.0f) == Approx(192000).margin(2));
}

TEST_CASE("A synced arpeggio steps on the song's grid while playing", "[plugins][sync]")
{
    // 1/16 at 120 bpm: a step every 6000 samples. Started half a step in, the
    // first change falls 3000 samples into the render, then every 6000: the
    // octave step doubles the crossings.
    const int step = 6000;
    const auto out = arpeggio(120.0, choiceNamed("1/16"), step / 2, true, 60);
    const int low = crossings(out, 500, 2500);
    const int high = crossings(out, 3500, 8500) * 2000 / 5000;
    const int lowAgain = crossings(out, 9500, 14500) * 2000 / 5000;
    CHECK(high == Approx(2 * low).margin(2));
    CHECK(lowAgain == Approx(low).margin(2));
    // The change lands on the grid: just before 3000 is low, just after high.
    CHECK(crossings(out, 3050, 4000) > crossings(out, 2000, 2950) * 3 / 2);
}

TEST_CASE("A synced arpeggio runs at its note value's rate while stopped", "[plugins][sync]")
{
    // Stopped, 1/8 at 120 bpm: 4 steps a second (the free rate is 2 Hz), so
    // over a second the pitch changes about four times.
    const auto out = arpeggio(120.0, choiceNamed("1/8"), 0, false, 94);
    int changes = 0;
    bool wasHigh = false;
    for (int w = 0; w + 1000 <= 48000; w += 1000)
    {
        const bool high = crossings(out, w, w + 1000) > 16; // C4 ~11, C5 ~22 a window
        if (w > 0 && high != wasHigh)
            ++changes;
        wasHigh = high;
    }
    CHECK(changes >= 3);
    CHECK(changes <= 5);
}
