#include "core/Command.h"
#include "core/Model.h"
#include "core/ProjectJson.h"
#include "core/TempoMap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace northlight::core;
using Catch::Approx;

TEST_CASE("A constant tempo map converts samples to bars and beats and back", "[core][tempo]")
{
    const TempoMap map(48000.0, 120.0, {4, 4});
    CHECK(map.constant());
    CHECK(map.positionAt(1.0) == 24000); // a quarter at 120 BPM is half a second
    CHECK(map.quartersAt(96000.0) == Approx(4.0));
    CHECK(map.barStart(1) == 96000);
    CHECK(map.barsBeatsAt(0) == BarsBeats{1, 1, 0});
    CHECK(map.barsBeatsAt(24000) == BarsBeats{1, 2, 0});
    CHECK(map.barsBeatsAt(96000) == BarsBeats{2, 1, 0});
    CHECK(map.barsBeatsAt(12000) == BarsBeats{1, 1, 480});
    CHECK(map.positionAt(BarsBeats{2, 1, 0}) == 96000);
    CHECK(map.positionAt(BarsBeats{1, 1, 480}) == 12000);
    for (SamplePosition s :
         {SamplePosition{0}, SamplePosition{1234}, SamplePosition{96000}, SamplePosition{500017}})
        CHECK(std::abs(map.positionAt(map.barsBeatsAt(s)) - s) <= 50); // to the tick
    // 6/8: beats are eighths (0.25 s at 120 quarters a minute), a bar is three quarters.
    const TempoMap compound(48000.0, 120.0, {6, 8});
    CHECK(compound.barStart(1) == 72000);
    CHECK(compound.barsBeatsAt(12000) == BarsBeats{1, 2, 0});
    CHECK(compound.barsBeatsAt(6 * 12000) == BarsBeats{2, 1, 0});
}

TEST_CASE("A tempo change moves later positions and keeps earlier ones", "[core][tempo]")
{
    // 120 BPM for 4 quarters (2 s), then 60 BPM.
    const TempoMap map(48000.0, 120.0, {4, 4}, {{4.0, 60.0, false}});
    CHECK_FALSE(map.constant());
    CHECK(map.positionAt(4.0) == 96000);
    CHECK(map.positionAt(5.0) == 96000 + 48000);
    CHECK(map.quartersAt(96000.0 + 48000.0) == Approx(5.0));
    CHECK(map.bpmAt(10) == Approx(120.0));
    CHECK(map.bpmAt(100000) == Approx(60.0));
    CHECK(map.barsBeatsAt(96000 + 48000) == BarsBeats{2, 2, 0});
    // Round trips through the change.
    for (double q : {0.0, 1.5, 3.999, 4.0, 4.25, 17.0})
        CHECK(map.quartersAt(map.samplesAt(q)) == Approx(q).margin(1e-9));
}

TEST_CASE("A ramp glides the tempo and integrates exactly", "[core][tempo]")
{
    // 60 BPM at the start, ramping to 120 at quarter 4, then holding.
    const TempoMap map(48000.0, 60.0, {4, 4}, {{4.0, 120.0, true}});
    CHECK(map.bpmAtQuarter(0.0) == Approx(60.0));
    CHECK(map.bpmAtQuarter(2.0) == Approx(90.0));
    CHECK(map.bpmAtQuarter(4.0) == Approx(120.0));
    CHECK(map.bpmAtQuarter(8.0) == Approx(120.0));
    // Seconds over the ramp: integral of 60/bpm(q) dq = 60/k ln(120/60), k = 15/quarter.
    const double rampSeconds = 60.0 / 15.0 * std::log(2.0);
    CHECK(map.samplesAt(4.0) == Approx(rampSeconds * 48000.0));
    // Summing small constant steps converges on the same answer.
    double seconds = 0.0;
    const int steps = 100000;
    for (int i = 0; i < steps; ++i)
        seconds += 60.0 / map.bpmAtQuarter((i + 0.5) * 4.0 / steps) * (4.0 / steps);
    CHECK(seconds == Approx(rampSeconds).epsilon(1e-6));
    for (double q : {0.5, 2.0, 3.9, 4.0, 6.0})
        CHECK(map.quartersAt(map.samplesAt(q)) == Approx(q).margin(1e-9));
}

TEST_CASE("Meter changes set bar lengths from their bar on", "[core][tempo]")
{
    // 4/4 for two bars, then 3/4 from bar 3 (index 2), then 7/8 from bar 5.
    const TempoMap map(48000.0, 120.0, {4, 4}, {}, {{2, {3, 4}}, {4, {7, 8}}});
    CHECK(map.barStartQuarter(2) == Approx(8.0));
    CHECK(map.barStartQuarter(3) == Approx(11.0));
    CHECK(map.barStartQuarter(4) == Approx(14.0));
    CHECK(map.barStartQuarter(5) == Approx(17.5));
    CHECK(map.barAtQuarter(10.99) == 2);
    CHECK(map.barAtQuarter(11.0) == 3);
    CHECK(map.signatureAtBar(3) == TimeSignature{3, 4});
    CHECK(map.signatureAtBar(4) == TimeSignature{7, 8});
    CHECK(map.barsBeatsAt(map.positionAt(12.0)) == BarsBeats{4, 2, 0});
    CHECK(map.barsBeatsAt(map.positionAt(14.5)) == BarsBeats{5, 2, 0}); // eighth beats
    CHECK(map.positionAt(BarsBeats{5, 2, 0}) == map.positionAt(14.5));
}

TEST_CASE("Snapping follows bars and beats through tempo and meter changes", "[core][tempo]")
{
    const TempoMap map(48000.0, 120.0, {4, 4}, {{8.0, 60.0, false}}, {{2, {3, 4}}});
    const GridStep beat{0, 1.0};
    const GridStep bar{1, 0.0};
    // Before the change: quarters are 24000 samples.
    CHECK(map.snap(24000 + 11999, beat) == 24000);
    CHECK(map.snap(24000 + 12001, beat) == 48000);
    // After it (quarter 8 = 192000): quarters are 48000.
    CHECK(map.snap(192000 + 23999, beat) == 192000);
    CHECK(map.snap(192000 + 24001, beat) == 240000);
    // Bars: bar 3 (3/4) starts at quarter 8, bar 4 at quarter 11.
    CHECK(map.snap(192000 + 1000, bar) == 192000);
    CHECK(map.snap(map.positionAt(10.6), bar) == map.positionAt(11.0));
    // Moving by steps.
    CHECK(map.move(0, bar, 3) == map.positionAt(11.0));
    CHECK(map.move(map.positionAt(9.0), beat, 2) == map.positionAt(11.0));
    CHECK(map.nextLine(1, beat) == 24000);
    CHECK(map.nextLine(24000, beat) == 24000);
}

TEST_CASE("The adaptive grid picks the division the zoom can show", "[core][tempo]")
{
    const TempoMap map(48000.0, 120.0, {4, 4});
    // At 100 samples a pixel and 16 px minimum, 1600 samples: an eighth of a beat is 3000.
    CHECK(map.adaptiveStep(100.0, 16, 0) == GridStep{0, 0.125});
    CHECK(map.adaptiveStep(1000.0, 16, 0) == GridStep{0, 1.0});
    CHECK(map.adaptiveStep(5000.0, 16, 0) == GridStep{1, 0.0});
    CHECK(map.adaptiveStep(20000.0, 16, 0) == GridStep{4, 0.0});
}

TEST_CASE("Beats are walked across tempo and meter changes", "[core][tempo]")
{
    const TempoMap map(48000.0, 120.0, {2, 4}, {{2.0, 60.0, false}}, {{2, {3, 8}}});
    auto beat = map.beatAtOrAfter(0.0);
    CHECK(beat.position == 0);
    CHECK(beat.bar == 0);
    std::vector<SamplePosition> positions;
    std::vector<std::pair<int, int>> where;
    for (int i = 0; i < 8; ++i)
    {
        positions.push_back(beat.position);
        where.emplace_back(beat.bar, beat.beat);
        beat = map.nextBeat(beat);
    }
    // 2/4 at 120 for bar 1 (24000 a quarter), then 60 BPM from quarter 2, bar 3 in 3/8.
    CHECK(positions[0] == 0);
    CHECK(positions[1] == 24000);
    CHECK(positions[2] == 48000);         // bar 2, quarter 2: the tempo halves here
    CHECK(positions[3] == 48000 + 48000); // bar 2 beat 2
    CHECK(positions[4] == 48000 + 96000); // bar 3 (3/8): eighths of 24000
    CHECK(positions[5] == 48000 + 120000);
    CHECK(where[4] == std::pair{2, 0});
    CHECK(where[6] == std::pair{2, 2});
    CHECK(where[7] == std::pair{3, 0});
    CHECK(map.beatAtOrAfter(1.0).position == 24000);
    CHECK(map.beatAtOrAfter(-30000.0).position == -24000); // before zero: the first tempo
}

TEST_CASE("Changing the tempo keeps MIDI, markers and automation on their beats", "[core][tempo]")
{
    Project p;
    p.sampleRate = 48000.0;
    p.tempoBpm = 120.0;
    Track t;
    t.id = p.allocateId();
    MidiClip clip;
    clip.id = p.allocateId();
    clip.start = 96000; // bar 2
    clip.length = 96000;
    clip.notes.push_back({p.allocateId(), 60, 100, 24000, 12000}); // beat 2, an eighth
    clip.controllers.push_back({p.allocateId(), MidiControllerType::controller, 1, 64, 48000}); // beat 3
    t.midiClips.push_back(clip);
    AudioClip fixed{p.allocateId(), "fixed", "a.wav", 48000, 48000};
    AudioClip musical{p.allocateId(), "loop", "b.wav", 96000, 96000};
    musical.followTempo = true;
    t.clips = {fixed, musical};
    AutomationLane lane;
    lane.id = p.allocateId();
    lane.points = {{0, 1.0f}, {192000, 0.5f}};
    t.automation.push_back(lane);
    p.tracks.push_back(t);
    p.markers.push_back({p.allocateId(), "Chorus", 192000, 96000});
    const auto original = p;

    UndoStack stack;
    stack.execute(p, std::make_unique<SetTempo>(60.0));
    const auto& mt = p.tracks[0];
    CHECK(mt.midiClips[0].start == 192000);
    CHECK(mt.midiClips[0].length == 192000);
    CHECK(mt.midiClips[0].notes[0].start == 48000);
    CHECK(mt.midiClips[0].notes[0].length == 24000);
    CHECK(mt.midiClips[0].controllers[0].time == 96000); // controllers keep their beat too
    CHECK(mt.findClip(fixed.id)->start == 48000);        // audio without a time base stays put
    const auto* loop = mt.findClip(musical.id);
    CHECK(loop->start == 192000);
    CHECK(loop->length == 192000);
    CHECK(loop->stretch == Approx(2.0));
    CHECK(mt.automation[0].points[1].position == 384000);
    CHECK(p.markers[0].position == 384000);
    CHECK(p.markers[0].length == 192000);

    stack.undo(p);
    CHECK(p == original);
    stack.redo(p);
    CHECK(p.tracks[0].midiClips[0].start == 192000);
}

TEST_CASE("Editing the tempo track is one undoable command", "[core][tempo]")
{
    Project p;
    p.sampleRate = 48000.0;
    Track t;
    t.id = p.allocateId();
    MidiClip clip;
    clip.id = p.allocateId();
    clip.start = 4 * 96000; // bar 5 at 120 BPM
    clip.length = 96000;
    t.midiClips.push_back(clip);
    p.tracks.push_back(t);
    const auto original = p;

    TempoState state = TempoState::of(p);
    state.tempoChanges = {{8.0, 60.0, false}}; // from bar 3 at half speed
    state.meterChanges = {{4, {3, 4}}};
    UndoStack stack;
    stack.execute(p, std::make_unique<SetTempoMap>(state));
    CHECK(p.tempoChanges.size() == 1);
    CHECK(p.meterChanges.size() == 1);
    // Quarter 16 (bar 5) now sits at 4 s + 8 quarters at 1 s each.
    CHECK(p.tracks[0].midiClips[0].start == 4 * 48000 + 8 * 48000);
    stack.undo(p);
    CHECK(p == original);
}

TEST_CASE("The tempo track and clip stretch survive a save and load", "[core][tempo][json]")
{
    Project p;
    p.tempoChanges = {{4.0, 90.0, true}, {12.5, 140.0, false}};
    p.meterChanges = {{3, {5, 8}}};
    Track t;
    t.id = p.allocateId();
    AudioClip c{p.allocateId(), "loop", "loop.wav", 0, 1000};
    c.stretch = 1.25;
    c.transpose = -3.5;
    c.followTempo = true;
    t.clips.push_back(c);
    p.tracks.push_back(t);
    const auto loaded = projectFromJson(projectToJson(p));
    REQUIRE(loaded.has_value());
    CHECK(loaded->tempoChanges == p.tempoChanges);
    CHECK(loaded->meterChanges == p.meterChanges);
    CHECK(loaded->tracks[0].clips[0].stretch == Approx(1.25));
    CHECK(loaded->tracks[0].clips[0].transpose == Approx(-3.5));
    CHECK(loaded->tracks[0].clips[0].followTempo);
}

TEST_CASE("Splitting a stretched clip keeps the source continuous", "[core][tempo]")
{
    Project p;
    Track t;
    t.id = p.allocateId();
    AudioClip c{p.allocateId(), "loop", "loop.wav", 1000, 2000, 100};
    c.stretch = 2.0;
    t.clips.push_back(c);
    p.tracks.push_back(t);
    UndoStack stack;
    auto split = std::make_unique<SplitClip>(t.id, c.id, 1600);
    auto* command = split.get();
    stack.execute(p, std::move(split));
    const auto* right = p.tracks[0].findClip(command->rightClipId());
    REQUIRE(right != nullptr);
    CHECK(right->sourceOffset == 100 + 300); // 600 timeline samples at 2x
    CHECK(right->stretch == 2.0);
    const auto& left = *p.tracks[0].findClip(c.id);
    CHECK(JoinClips::canJoin(left, *right));
}

TEST_CASE("The tempo map cache rebuilds only when the tempo changed", "[core][tempo]")
{
    Project p;
    p.tempoBpm = 120.0;
    TempoMapCache cache;
    const auto& first = cache.of(p);
    CHECK(first.bpmAt(0) == Approx(120.0));
    p.name = "Renamed"; // not the tempo
    CHECK(&cache.of(p) == &first);
    CHECK(cache.of(p).bpmAt(0) == Approx(120.0));
    p.tempoBpm = 90.0;
    CHECK(cache.of(p).bpmAt(0) == Approx(90.0));
    p.tempoChanges.push_back({8.0, 60.0, false});
    CHECK(cache.of(p).bpmAtQuarter(9.0) == Approx(60.0));
}
