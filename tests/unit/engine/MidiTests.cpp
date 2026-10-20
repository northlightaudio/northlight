#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/Insert.h"
#include "engine/Midi.h"
#include "engine/Recording.h"
#include "engine/Snapshot.h"
#include "plugins/BuiltIns.h"

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
            r[e.sampleOffset] = static_cast<float>(e.note); // which note
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

core::Project keysProject(core::MidiClip clip, const char* instrument = "test.midi")
{
    core::Project p;
    core::Track t;
    t.id = 1;
    t.kind = core::TrackKind::midi;
    core::Insert i;
    i.id = 2;
    i.type = instrument;
    i.params = plugins::defaultParameters(instrument);
    t.inserts.push_back(i);
    clip.id = 3;
    t.midiClips.push_back(clip);
    p.tracks.push_back(t);
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
    aux.auxTrack = 1;
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
    REQUIRE(snapshot->trackLevels.size() == 2);
    CHECK(snapshot->trackLevels[0] == std::vector<int>{1}); // the source first
    CHECK(snapshot->trackLevels[1] == std::vector<int>{0});

    // A source that goes away leaves the aux track silent, not crashing.
    p.tracks[1].inserts.clear();
    h.engine.setProject(p);
    h.render(2);
    CHECK(h.left[10] == Approx(0.0f));
}

TEST_CASE("The built-in synth makes sound for a note and goes quiet after it", "[engine][midi][synth]")
{
    core::MidiClip clip;
    clip.start = 0;
    clip.length = 48000;
    clip.notes.push_back({20, 69, 110, 0, 4800}); // A4 for 100 ms
    Harness h;
    h.engine.setProject(keysProject(clip, plugins::types::synth.c_str()));
    h.engine.play();
    float loud = 0.0f, quiet = 0.0f;
    for (int b = 0; b < 75; ++b) // 4800 samples of note, then release
    {
        h.engine.process(h.outputs, 2, blockSize);
        float peak = 0.0f;
        for (float v : h.left)
            peak = std::max(peak, std::abs(v));
        if (b >= 10 && b < 70)
            loud = std::max(loud, peak);
        if (b == 74)
            quiet = peak;
    }
    for (int b = 0; b < 400; ++b) // well past the release
        h.engine.process(h.outputs, 2, blockSize);
    float tail = 0.0f;
    for (float v : h.left)
        tail = std::max(tail, std::abs(v));
    CHECK(loud > 0.05f);
    CHECK(tail < 1.0e-4f);
    (void)quiet;
}
