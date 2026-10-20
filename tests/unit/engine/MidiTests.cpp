#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/Insert.h"
#include "engine/Midi.h"
#include "engine/Recording.h"
#include "engine/Snapshot.h"
#include "plugins/Aurora.h"
#include "plugins/BuiltIns.h"
#include "plugins/Nebula.h"
#include "plugins/Pulsar.h"
#include "plugins/Starbit.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace aural;
using Catch::Approx;

namespace
{
class NoMedia final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double, std::string& error) override
    {
        error = "no media";
        return nullptr;
    }
};

// "test.midi": an instrument that writes +velocity/127 on the left channel at
// every note-on sample and -1 at every note-off (or all-notes-off) sample, so a
// test can read the schedule straight from the audio.
class MarkerInstrument final : public engine::InsertProcessor
{
public:
    const std::string& typeName() const noexcept override { return type_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return none_; }
    void prepare(double, int) override {}
    void setParameter(int, float) noexcept override {}
    float parameter(int) const noexcept override { return 0.0f; }
    bool acceptsMidi() const noexcept override { return true; }
    // One extra pair: a constant 0.5 on the left and -0.5 on the right while a note
    // is held, so an aux track can be told apart from the main output.
    int auxOutputCount() const noexcept override { return 1; }
    const float* auxOutput(int index, int channel) const noexcept override
    {
        return index == 0 ? (channel == 0 ? auxLeft_.data() : auxRight_.data()) : nullptr;
    }
    void process(float* l, float* r, int n) noexcept override
    {
        std::fill(l, l + n, 0.0f);
        std::fill(r, r + n, 0.0f);
        std::fill(auxLeft_.begin(), auxLeft_.end(), 0.5f);
        std::fill(auxRight_.begin(), auxRight_.end(), -0.5f);
    }
    void processMidi(const engine::MidiEventList& events, float* l, float* r, int n) noexcept override
    {
        process(l, r, n);
        for (const auto& e : events)
        {
            if (e.sampleOffset < 0 || e.sampleOffset >= n)
                continue;
            if (e.type == engine::MidiEvent::Type::noteOn)
                l[e.sampleOffset] += static_cast<float>(e.velocity) / 127.0f;
            else if (e.type == engine::MidiEvent::Type::noteOff ||
                     e.type == engine::MidiEvent::Type::allNotesOff)
                l[e.sampleOffset] -= 1.0f;
            r[e.sampleOffset] = static_cast<float>(e.note + 128 * e.channel); // which note, on which channel
        }
    }

private:
    std::string type_ = "test.midi";
    std::vector<engine::ParameterInfo> none_;
    std::vector<float> auxLeft_ = std::vector<float>(8192), auxRight_ = std::vector<float>(8192);
};

class MidiFactory final : public engine::InsertFactory
{
public:
    std::vector<std::string> typeNames() const override { return {}; }
    std::unique_ptr<engine::InsertProcessor> create(const std::string& type) const override
    {
        if (type == "test.midi")
            return std::make_unique<MarkerInstrument>();
        return builtIns_.create(type);
    }

private:
    plugins::BuiltInFactory builtIns_;
};

const MidiFactory midiFactory;
constexpr int blockSize = 64;

struct Harness
{
    NoMedia loader;
    engine::AudioEngine engine{loader, midiFactory, 0};
    std::vector<float> left = std::vector<float>(blockSize), right = std::vector<float>(blockSize);
    float* outputs[2] = {left.data(), right.data()};
    Harness() { engine.prepare(48000.0, blockSize); }
    // Renders `blocks` blocks from the current position and returns the left
    // channel (the track's stereo output at centre pan is untouched).
    std::vector<float> render(int blocks)
    {
        std::vector<float> out;
        for (int b = 0; b < blocks; ++b)
        {
            engine.process(outputs, 2, blockSize);
            for (float v : left)
                out.push_back(v);
        }
        return out;
    }
};

// A MIDI track (1, with the clip) aimed at an instrument channel (4) whose
// instrument (2) plays its notes: tracks[0] is the MIDI track, tracks[1] the channel.
core::Project keysProject(core::MidiClip clip, const char* instrument = "test.midi")
{
    core::Project p;
    core::Track t;
    t.id = 1;
    t.kind = core::TrackKind::midi;
    t.midiTarget = 4;
    clip.id = 3;
    t.midiClips.push_back(clip);
    p.tracks.push_back(t);
    core::Track channel;
    channel.id = 4;
    channel.kind = core::TrackKind::instrument;
    channel.instrument.id = 2;
    channel.instrument.type = instrument;
    channel.instrument.params = plugins::defaultParameters(instrument);
    p.tracks.push_back(channel);
    p.nextId = 100;
    return p;
}
} // namespace

TEST_CASE("Notes play at their sample positions, across block boundaries", "[engine][midi]")
{
    core::MidiClip clip;
    clip.start = 10;
    clip.length = 1000;
    clip.notes.push_back({20, 60, 127, 5, 100}); // on at 15, off at 115
    clip.notes.push_back({21, 64, 64, 130, 50}); // on at 140, off at 190
    Harness h;
    h.engine.setProject(keysProject(clip));
    h.engine.play();
    const auto out = h.render(4); // 256 samples
    CHECK(out[15] == Approx(1.0f).margin(0.01f));
    CHECK(out[115] == Approx(-1.0f).margin(0.01f));
    CHECK(out[140] == Approx(64.0f / 127.0f).margin(0.01f));
    CHECK(out[190] == Approx(-1.0f).margin(0.01f));
    CHECK(out[0] == Approx(0.0f));
    CHECK(out[100] == Approx(0.0f));
}

TEST_CASE("A note still sounding when the transport stops or jumps gets its note-off", "[engine][midi]")
{
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 10000;
    clip.notes.push_back({20, 60, 127, 0, 5000}); // long note
    Harness h;
    h.engine.setProject(keysProject(clip));
    h.engine.play();
    auto out = h.render(2); // on at 0, still sounding
    CHECK(out[0] == Approx(1.0f).margin(0.01f));
    h.engine.stop();
    out = h.render(1); // the stop flushes: a note-off at the start of the block
    CHECK(out[0] == Approx(-1.0f).margin(0.01f));
    CHECK(out[1] == Approx(0.0f));

    h.engine.locate(0);
    h.engine.play();
    out = h.render(1);
    CHECK(out[0] == Approx(1.0f).margin(0.01f)); // on again from the top
    h.engine.locate(8000);                       // jump past the note's end: its off is flushed
    out = h.render(1);
    CHECK(out[0] == Approx(-1.0f).margin(0.01f));
}

TEST_CASE("A loop wrap ends the notes still sounding and restarts those at the loop start", "[engine][midi]")
{
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 1000;
    clip.notes.push_back({20, 60, 127, 0, 900}); // across the loop end at 128
    Harness h;
    h.engine.setProject(keysProject(clip));
    h.engine.setLoop(0, 128, true);
    h.engine.play();
    const auto out = h.render(5);
    CHECK(out[0] == Approx(1.0f).margin(0.01f));
    // At the wrap (sample 128) the note is cut off and started again.
    CHECK(out[128] == Approx(0.0f).margin(0.01f)); // -1 (off) + 1 (on) at the same sample
    CHECK(out[256] == Approx(0.0f).margin(0.01f));
    CHECK(out[64] == Approx(0.0f));
}

TEST_CASE("Live MIDI input reaches an armed MIDI track's instrument in the same block", "[engine][midi]")
{
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 10;
    auto p = keysProject(clip);
    p.tracks[0].armed = true;
    Harness h;
    h.engine.setProject(p);
    h.engine.pushMidiInput({7, engine::MidiEvent::Type::noteOn, 0, 72, 100}, "");
    const auto out = h.render(1); // stopped: monitoring is on while armed
    CHECK(out[7] == Approx(100.0f / 127.0f).margin(0.01f));
    CHECK(h.right[7] == Approx(72.0f).margin(0.01f));
}

TEST_CASE("A track that plays the MIDI output hands its events on, timed on the sample clock",
          "[engine][midi][midiout]")
{
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 1000;
    clip.notes.push_back({20, 60, 100, 70, 100}); // on at 70 (second block), off at 170
    auto p = keysProject(clip);
    p.tracks[0].midiOut = true;
    p.tracks[0].midiOutChannel = 10;
    Harness h;
    h.engine.setProject(p);
    h.engine.play();
    h.render(1);
    engine::MidiOutEvent e;
    CHECK_FALSE(h.engine.popMidiOutput(e)); // nothing in the first block
    h.render(3);
    std::vector<engine::MidiOutEvent> out;
    while (h.engine.popMidiOutput(e))
        out.push_back(e);
    REQUIRE(out.size() == 2);
    CHECK(out[0].event.type == engine::MidiEvent::Type::noteOn);
    CHECK(out[0].event.channel == 9); // channel 10, zero-based
    CHECK(out[0].event.note == 60);
    CHECK(out[0].clock == 70);
    CHECK(out[1].clock == 170);
    // The clock sync names a block start on the same clock.
    const auto sync = h.engine.clockSync();
    CHECK(sync.clock == 3 * 64);
    CHECK(sync.steadyNanos > 0);

    // A track that does not play the output hands on nothing.
    p.tracks[0].midiOut = false;
    h.engine.setProject(p);
    h.engine.locate(0);
    h.render(4);
    CHECK_FALSE(h.engine.popMidiOutput(e));
}

TEST_CASE("An audition reaches only its own track's instrument, armed or not", "[engine][midi]")
{
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 10;
    auto p = keysProject(clip); // not armed, so device input is not heard
    Harness h;
    h.engine.setProject(p);
    h.engine.pushMidiInput({5, engine::MidiEvent::Type::noteOn, 0, 64, 100}, "");
    h.engine.pushMidiPreview({7, engine::MidiEvent::Type::noteOn, 0, 72, 100}, 99); // another track
    auto out = h.render(1);
    CHECK(out[7] == Approx(0.0f));
    h.engine.pushMidiPreview({7, engine::MidiEvent::Type::noteOn, 0, 72, 100}, p.tracks[0].id);
    out = h.render(1);
    CHECK(out[7] == Approx(100.0f / 127.0f).margin(0.01f));
    CHECK(h.right[7] == Approx(72.0f).margin(0.01f));
}

TEST_CASE("Recording an armed MIDI track captures its live notes at their timeline positions, per take",
          "[engine][midi][record]")
{
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 10;
    auto p = keysProject(clip);
    p.tracks[0].armed = true;
    core::Track other; // an un-armed MIDI track is not captured
    other.id = 5;
    other.kind = core::TrackKind::midi;
    p.tracks.push_back(other);
    Harness h;
    h.engine.setProject(p);
    h.engine.setLoop(0, 256, true);
    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    session->addMidiTrack(1);
    std::string error;
    REQUIRE(h.engine.startRecording(std::move(session), error));
    h.render(1); // the record command applies: capture from 0
    h.engine.pushMidiInput({3, engine::MidiEvent::Type::noteOn, 0, 60, 100}, "");
    h.render(1); // block [64, 128): on at 67
    h.engine.pushMidiInput({0, engine::MidiEvent::Type::noteOff, 0, 60, 0}, "");
    h.render(3); // off at 128; then the wrap at 256 starts take 2
    h.engine.pushMidiInput({10, engine::MidiEvent::Type::noteOn, 0, 62, 90}, "");
    h.render(1); // block [64, 128) of pass 2: on at 74
    h.engine.stop();
    h.render(1);
    while (!h.engine.hasRecordingToCollect())
        h.render(1);
    auto finished = h.engine.collectRecording();
    REQUIRE(finished.has_value());
    const auto events = finished->session->capturedMidi();
    REQUIRE(events.size() == 3);
    CHECK(events[0].trackId == 1);
    CHECK(events[0].position == 67);
    CHECK(events[0].take == 0);
    CHECK(events[1].position == 128);
    CHECK(events[2].position == 74);
    CHECK(events[2].take == 1); // after the wrap
    const auto spans = finished->session->takeSpans();
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].start == 0);
    CHECK(spans[0].end == 256);
    CHECK(spans[1].start == 0);
    CHECK(spans[1].end == 128);
    CHECK(finished->session->takeSpans().size() == 2); // kept: a second call sees the same
    CHECK(finished->session->capturedMidi().size() == 3);

    // Takes become clips: the second pass's note is still held when the
    // recording stops, so it ends there; everything moves 10 samples early.
    const auto takes = engine::buildMidiTakes(events, spans, 10);
    REQUIRE(takes.size() == 2);
    REQUIRE(takes[0].clip.notes.size() == 1);
    CHECK(takes[0].clip.start == 0);
    CHECK(takes[0].clip.length == 246);
    CHECK(takes[0].clip.notes[0].start == 57);
    CHECK(takes[0].clip.notes[0].length == 61);
    CHECK(takes[0].clip.notes[0].velocity == 100);
    REQUIRE(takes[1].clip.notes.size() == 1);
    CHECK(takes[1].clip.start == 0);
    CHECK(takes[1].clip.notes[0].start == 64);
    CHECK(takes[1].clip.notes[0].note == 62);
    CHECK(takes[1].clip.notes[0].length == 54);
    CHECK(takes[1].clip.length == 118);
}

TEST_CASE("MIDI takes pair note-ons with their offs, end held notes at the take end and shift for latency",
          "[engine][midi][record]")
{
    using Captured = engine::RecordSession::CapturedMidi;
    const auto on = [](core::Id track, int note, int vel, core::SamplePosition at, int take)
    {
        return Captured{track,
                        {0,
                         engine::MidiEvent::Type::noteOn,
                         0,
                         static_cast<std::uint8_t>(note),
                         static_cast<std::uint8_t>(vel)},
                        at,
                        take};
    };
    const auto off = [](core::Id track, int note, core::SamplePosition at, int take)
    {
        return Captured{
            track, {0, engine::MidiEvent::Type::noteOff, 0, static_cast<std::uint8_t>(note), 0}, at, take};
    };
    const std::vector<Captured> events = {
        on(1, 60, 100, 1100, 0),
        off(1, 60, 1300, 0),    // take 0 [1000, 2000): a note of 200
        on(1, 64, 80, 1250, 0), // held past the take: ends at its end
        on(2, 48, 70, 1500, 0), // another track, same take
        on(1, 67, 90, 2010, 1),
        off(1, 67, 2050, 1), // take 1 [2000, 2500)
        on(1, 67, 90, 2060, 1),
        on(1, 67, 95, 2070, 1), // a retrigger ends the first
        off(1, 67, 2090, 1),
    };
    const std::vector<engine::RecordSession::TakeSpan> spans = {{1000, 2000}, {2000, 2500}};
    const auto takes = engine::buildMidiTakes(events, spans, 100);
    REQUIRE(takes.size() == 3);
    const auto& first = takes[0];
    CHECK(first.trackId == 1);
    CHECK(first.clip.start == 900);
    CHECK(first.clip.length == 1000);
    REQUIRE(first.clip.notes.size() == 2);
    CHECK(first.clip.notes[0].start == 100);
    CHECK(first.clip.notes[0].length == 200);
    CHECK(first.clip.notes[1].note == 64);
    CHECK(first.clip.notes[1].start == 250);
    CHECK(first.clip.notes[1].length == 750); // held to the take's end
    const auto& secondTrack = takes[1];
    CHECK(secondTrack.trackId == 2);
    CHECK(secondTrack.clip.notes.size() == 1);
    const auto& second = takes[2];
    CHECK(second.clip.start == 1900);
    REQUIRE(second.clip.notes.size() == 3);
    CHECK(second.clip.notes[0].start == 10);
    CHECK(second.clip.notes[0].length == 40);
    CHECK(second.clip.notes[1].start == 60);
    CHECK(second.clip.notes[1].length == 10); // retriggered at 70
    CHECK(second.clip.notes[2].start == 70);
    CHECK(second.clip.notes[2].velocity == 95);
    CHECK(second.clip.notes[2].length == 20);
    CHECK(second.clip.length == 500);
    // No notes: no clip.
    CHECK(engine::buildMidiTakes({off(1, 60, 10, 0)}, spans, 0).empty());
}

TEST_CASE("An aux track carries an instrument's extra output pair through its own chain",
          "[engine][midi][aux]")
{
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 1000;
    auto p = keysProject(clip);
    // The aux track comes first in the project: it must still render after its source.
    core::Track aux;
    aux.id = 5;
    aux.kind = core::TrackKind::aux;
    aux.name = "Keys out 2";
    aux.auxTrack = 4; // the instrument channel
    aux.auxInsert = 2;
    aux.auxOutput = 1;
    aux.gain = 0.5f;
    p.tracks.insert(p.tracks.begin(), aux);
    Harness h;
    h.engine.setProject(p);
    h.engine.play();
    h.render(2);
    // The main track's instrument writes nothing to its main output here; the aux
    // track adds the extra pair through its own fader (0.5).
    CHECK(h.left[10] == Approx(0.25f).margin(0.01f));
    CHECK(h.right[10] == Approx(-0.25f).margin(0.01f));
    const auto* snapshot = h.engine.snapshotForTest();
    REQUIRE(snapshot != nullptr);
    REQUIRE(snapshot->trackLevels.size() == 3);
    CHECK(snapshot->trackLevels[0] == std::vector<int>{1}); // the MIDI track schedules first,
    CHECK(snapshot->trackLevels[1] == std::vector<int>{2}); // then its instrument channel plays,
    CHECK(snapshot->trackLevels[2] == std::vector<int>{0}); // then the output reads it

    // A source that goes away leaves the aux track silent, not crashing.
    p.tracks[2].instrument = {};
    h.engine.setProject(p);
    h.render(2);
    CHECK(h.left[10] == Approx(0.0f));
}

TEST_CASE("Several MIDI tracks play one instrument channel, on their channels, scaled and muted",
          "[engine][midi][routing]")
{
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 1000;
    clip.notes.push_back({20, 60, 127, 5, 50}); // track A: on at 5
    auto p = keysProject(clip);
    // Track B, channel 3 at half volume: a note at 20.
    core::Track b;
    b.id = 10;
    b.kind = core::TrackKind::midi;
    b.midiTarget = 4;
    b.midiChannel = 3;
    b.gain = 0.5f;
    core::MidiClip bc;
    bc.id = 11;
    bc.start = 0;
    bc.length = 1000;
    bc.notes.push_back({12, 64, 100, 20, 50});
    b.midiClips.push_back(bc);
    // Track C, muted: a note at 30 that must not sound.
    core::Track c = b;
    c.id = 13;
    c.mute = true;
    c.gain = 1.0f;
    c.midiChannel = 1;
    c.midiClips[0].id = 14;
    c.midiClips[0].notes = {{15, 67, 100, 30, 50}};
    p.tracks.push_back(b);
    p.tracks.push_back(c);
    Harness h;
    h.engine.setProject(p);
    h.engine.play();
    const auto out = h.render(1);
    CHECK(out[5] == Approx(1.0f).margin(0.01f));
    CHECK(h.right[5] == Approx(60.0f).margin(0.01f)); // channel 1
    CHECK(out[20] == Approx(50.0f / 127.0f).margin(0.01f));
    CHECK(h.right[20] == Approx(64.0f + 128.0f * 2.0f).margin(0.01f)); // channel 3
    CHECK(out[30] == Approx(0.0f));
    // A MIDI track aimed nowhere plays nothing.
    p.tracks[0].midiTarget = core::invalidId;
    h.engine.setProject(p);
    h.engine.locate(0);
    const auto again = h.render(1);
    CHECK(again[5] == Approx(0.0f));
}

TEST_CASE("Aurora sounds for a chord, goes quiet after the release, and every preset stays finite",
          "[engine][midi][aurora]")
{
    for (const auto& preset : plugins::aurora::presets())
    {
        INFO(preset.name);
        core::MidiClip clip;
        clip.start = 0;
        clip.length = 96000;
        for (const int note : {48, 55, 60, 64})
            clip.notes.push_back({core::invalidId, note, 100, 0, 9600}); // 200 ms
        for (size_t i = 0; i < clip.notes.size(); ++i)
            clip.notes[i].id = 20 + static_cast<core::Id>(i);
        auto project = keysProject(clip, plugins::aurora::type.c_str());
        project.tracks[1].instrument.params = preset.values;
        Harness h;
        h.engine.setProject(project);
        h.engine.play();
        float loud = 0.0f;
        bool finite = true;
        for (int b = 0; b < 150; ++b) // the notes and a little after
        {
            h.engine.process(h.outputs, 2, blockSize);
            for (size_t i = 0; i < h.left.size(); ++i)
            {
                finite = finite && std::isfinite(h.left[i]) && std::isfinite(h.right[i]);
                loud = std::max(loud, std::abs(h.left[i]));
            }
        }
        for (int b = 0; b < 6000; ++b) // past the longest release (8 s)
            h.engine.process(h.outputs, 2, blockSize);
        float tail = 0.0f;
        for (float v : h.left)
            tail = std::max(tail, std::abs(v));
        CHECK(finite);
        CHECK(loud > 0.01f);
        CHECK(loud < 4.0f);
        CHECK(tail < 1.0e-3f);
    }
}

TEST_CASE("Pulsar's pads sound on their notes, play out without note-offs and every kit stays finite",
          "[engine][midi][pulsar]")
{
    namespace pu = plugins::pulsar;
    CHECK(pu::padForNote(36) == pu::kick);
    CHECK(pu::padForNote(42) == pu::closedHat);
    CHECK(pu::padForNote(60) == pu::kick); // C3 up plays the pads in order
    CHECK(pu::padForNote(30) == -1);
    for (const auto& kit : pu::kits())
        for (int pad = 0; pad < pu::padCount; ++pad)
        {
            INFO(kit.name << " " << pu::padName(pad));
            // The pad's own sound, offline: loud at first, silent by the end.
            const auto hit = pu::renderPad(kit.values.data(), pad, 48000.0, 48000 * 4);
            float early = 0.0f, late = 0.0f;
            bool finite = true;
            for (size_t i = 0; i < hit.size(); ++i)
            {
                finite = finite && std::isfinite(hit[i]);
                if (i < 4800)
                    early = std::max(early, std::abs(hit[i]));
                if (i > hit.size() - 4800)
                    late = std::max(late, std::abs(hit[i]));
            }
            CHECK(finite);
            CHECK(early > 0.05f);
            CHECK(late < 1.0e-3f);
        }

    // Through the engine: a kick and a snare, each a 1-sample note (the note-off
    // does not cut a drum short).
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 48000;
    clip.notes.push_back({20, 36, 120, 0, 1});
    clip.notes.push_back({21, 38, 100, 12000, 1});
    Harness h;
    h.engine.setProject(keysProject(clip, pu::type.c_str()));
    h.engine.play();
    float kick = 0.0f, snare = 0.0f;
    for (int b = 0; b < 375; ++b) // 0.5 s
    {
        h.engine.process(h.outputs, 2, blockSize);
        for (float v : h.left)
            (b * blockSize < 12000 ? kick : snare) =
                std::max(b * blockSize < 12000 ? kick : snare, std::abs(v));
    }
    CHECK(kick > 0.05f);
    CHECK(snare > 0.02f);
}

TEST_CASE("Nebula swells for a chord, every preset stays finite, and a dry note dies away",
          "[engine][midi][nebula]")
{
    namespace ne = plugins::nebula;
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 480000;
    for (const int note : {48, 55, 60, 64})
        clip.notes.push_back({core::invalidId, note, 100, 0, 48000}); // a second
    for (size_t i = 0; i < clip.notes.size(); ++i)
        clip.notes[i].id = 20 + static_cast<core::Id>(i);
    for (const auto& preset : ne::presets())
    {
        INFO(preset.name);
        auto project = keysProject(clip, ne::type.c_str());
        project.tracks[1].instrument.params = preset.values;
        Harness h;
        h.engine.setProject(project);
        h.engine.play();
        float loud = 0.0f;
        bool finite = true;
        for (int b = 0; b < 1500; ++b) // two seconds: the swell and the start of the release
        {
            h.engine.process(h.outputs, 2, blockSize);
            for (size_t i = 0; i < h.left.size(); ++i)
            {
                finite = finite && std::isfinite(h.left[i]) && std::isfinite(h.right[i]);
                loud = std::max(loud, std::abs(h.left[i]));
            }
        }
        CHECK(finite);
        CHECK(loud > 0.01f);
        CHECK(loud < 4.0f);
    }

    // Without the space and with a short release, the voices end.
    auto values = ne::presets().front().values;
    values[ne::spaceMix] = 0.0f;
    values[ne::attack] = 5.0f;
    values[ne::release] = 200.0f;
    auto project = keysProject(clip, ne::type.c_str());
    project.tracks[1].instrument.params = values;
    Harness h;
    h.engine.setProject(project);
    h.engine.play();
    for (int b = 0; b < 1500; ++b) // the note (1 s) and the release
        h.engine.process(h.outputs, 2, blockSize);
    float tail = 0.0f;
    for (float v : h.left)
        tail = std::max(tail, std::abs(v));
    CHECK(tail < 1.0e-3f);

    // The spectrum the editor draws: brighter lifts the upper partials; odd only
    // empties the even ones.
    auto dark = values, bright = values, hollow = values;
    dark[ne::brightness] = 0.0f;
    bright[ne::brightness] = 1.0f;
    hollow[ne::oddEven] = -1.0f;
    hollow[ne::formant] = 0.0f;
    const auto d = ne::spectrum(dark.data(), 220.0f), b = ne::spectrum(bright.data(), 220.0f);
    CHECK(b[8].level > d[8].level);
    const auto o = ne::spectrum(hollow.data(), 220.0f);
    CHECK(o[1].level == Approx(0.0f).margin(1.0e-6f)); // the 2nd partial
    CHECK(o[2].level > 0.05f);                         // the 3rd
}

TEST_CASE("Starbit sounds for every preset, goes quiet after the release, and draws its waves",
          "[engine][midi][starbit]")
{
    namespace sb = plugins::starbit;
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 96000;
    for (const int note : {60, 64, 67})
        clip.notes.push_back({core::invalidId, note, 110, 0, 24000}); // half a second
    for (size_t i = 0; i < clip.notes.size(); ++i)
        clip.notes[i].id = 20 + static_cast<core::Id>(i);
    for (const auto& preset : sb::presets())
    {
        INFO(preset.name);
        auto project = keysProject(clip, sb::type.c_str());
        project.tracks[1].instrument.params = preset.values;
        Harness h;
        h.engine.setProject(project);
        h.engine.play();
        float loud = 0.0f;
        bool finite = true;
        for (int b = 0; b < 375; ++b) // the notes
        {
            h.engine.process(h.outputs, 2, blockSize);
            for (size_t i = 0; i < h.left.size(); ++i)
            {
                finite = finite && std::isfinite(h.left[i]) && std::isfinite(h.right[i]);
                loud = std::max(loud, std::abs(h.left[i]));
            }
        }
        for (int b = 0; b < 3000; ++b) // the release and the echo's tail
            h.engine.process(h.outputs, 2, blockSize);
        float tail = 0.0f;
        for (float v : h.left)
            tail = std::max(tail, std::abs(v));
        CHECK(finite);
        CHECK(loud > 0.01f);
        CHECK(loud < 2.0f);
        CHECK(tail < 1.0e-3f);
    }

    // The waves as the editor draws them.
    auto values = sb::presets().front().values;
    values[sb::wave] = sb::pulse;
    values[sb::duty] = 2.0f; // 50%
    auto shape = sb::waveShape(values.data());
    CHECK(shape[0] == 15);
    CHECK(shape[16] == 0);
    values[sb::wave] = sb::custom;
    values[sb::firstWaveStep + 5] = 3.0f;
    shape = sb::waveShape(values.data());
    CHECK(shape[5] == 3);
    CHECK(sb::arpSteps(sb::arpMajor) == std::vector<int>{0, 4, 7});
    CHECK(sb::arpSteps(sb::arpChord).empty());
}
