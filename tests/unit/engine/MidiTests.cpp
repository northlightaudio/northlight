#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/Insert.h"
#include "engine/Midi.h"
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
    void process(float* l, float* r, int n) noexcept override
    {
        std::fill(l, l + n, 0.0f);
        std::fill(r, r + n, 0.0f);
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
