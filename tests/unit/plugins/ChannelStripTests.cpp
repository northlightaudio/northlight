#include "core/Command.h"
#include "core/ProjectJson.h"
#include "plugins/BuiltIns.h"
#include "plugins/ChannelStrip.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;
namespace strip = plugins::strip;

namespace
{
constexpr double rate = 48000.0;

std::unique_ptr<engine::InsertProcessor> makeModule(const std::string& type)
{
    auto p = strip::create(type);
    REQUIRE(p != nullptr);
    p->prepare(rate, 256);
    return p;
}

// A sine of `hz` at `amplitude` through the processor; the output RMS over the
// last half second relative to the input RMS, in dB.
float responseDb(engine::InsertProcessor& p, double hz, float amplitude = 0.25f, double seconds = 1.5)
{
    const int n = static_cast<int>(rate * seconds);
    std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
            amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / rate));
    for (int done = 0; done < n; done += 256)
        p.process(l.data() + done, r.data() + done, std::min(256, n - done));
    double sum = 0.0;
    const int from = n - static_cast<int>(rate / 2);
    for (int i = from; i < n; ++i)
        sum += static_cast<double>(l[static_cast<size_t>(i)]) * l[static_cast<size_t>(i)];
    const double rms = std::sqrt(sum / (n - from));
    return static_cast<float>(20.0 * std::log10(rms / (amplitude / std::numbers::sqrt2)));
}

float peakAfter(engine::InsertProcessor& p, double hz, float amplitude, double seconds = 1.0)
{
    const int n = static_cast<int>(rate * seconds);
    std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
            amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / rate));
    for (int done = 0; done < n; done += 256)
        p.process(l.data() + done, r.data() + done, std::min(256, n - done));
    float peak = 0.0f;
    for (int i = n / 2; i < n; ++i)
        peak = std::max(peak, std::abs(l[static_cast<size_t>(i)]));
    return peak;
}

void enable(engine::InsertProcessor& p)
{
    p.setBypassed(false);
}
} // namespace

TEST_CASE("The default strip has every module, off, with its defaults", "[plugins][strip]")
{
    const auto modules = strip::defaultStrip();
    REQUIRE(modules.size() == strip::moduleTypes().size());
    for (size_t i = 0; i < modules.size(); ++i)
    {
        CHECK(modules[i].type == strip::moduleTypes()[i]);
        CHECK(modules[i].bypass);
        CHECK(modules[i].id == core::invalidId);
        auto p = strip::create(modules[i].type);
        REQUIRE(p != nullptr);
        CHECK(p->parameters().size() == modules[i].params.size());
        CHECK(p->feedback() != nullptr);
        CHECK(plugins::BuiltInFactory().create(modules[i].type) != nullptr);
        CHECK(plugins::BuiltInFactory::displayName(modules[i].type) == strip::displayName(modules[i].type));
    }
    // Strip modules are not offered as inserts.
    for (const auto& type : plugins::BuiltInFactory().typeNames())
        CHECK_FALSE(strip::isStripType(type));
}

TEST_CASE("Strip EQ: flat by default, bands match the curve the editor draws", "[plugins][strip][eq]")
{
    auto eq = makeModule(strip::types::eq);
    enable(*eq);
    CHECK(responseDb(*eq, 1000.0) == Approx(0.0f).margin(0.05f));

    const int band2Gain = strip::eq::paramIndex(strip::eq::band2, strip::eq::gain);
    const int band2Freq = strip::eq::paramIndex(strip::eq::band2, strip::eq::frequency);
    eq->setParameter(band2Freq, 1000.0f);
    eq->setParameter(band2Gain, 6.0f);
    CHECK(responseDb(*eq, 1000.0) == Approx(6.0f).margin(0.1f));

    // A 24 dB/oct low cut at 200 Hz: what runs is what responseDb says.
    eq->setParameter(strip::eq::paramIndex(strip::eq::lowCut, strip::eq::on), 1.0f);
    eq->setParameter(strip::eq::paramIndex(strip::eq::lowCut, strip::eq::type), 3.0f);
    eq->setParameter(strip::eq::paramIndex(strip::eq::lowCut, strip::eq::frequency), 200.0f);
    std::vector<float> params;
    for (int i = 0; i < strip::eq::count; ++i)
        params.push_back(eq->parameter(i));
    for (const double hz : {60.0, 150.0, 1000.0, 5000.0})
        CHECK(responseDb(*eq, hz) == Approx(strip::eq::responseDb(params.data(), rate, hz)).margin(0.3));
    // Two octaves below the corner a 4th-order cut is about 48 dB down.
    CHECK(strip::eq::responseDb(params.data(), rate, 50.0) == Approx(-48.0).margin(1.5));
    // One band on its own: the peak, without the cut.
    CHECK(strip::eq::responseDb(params.data(), rate, 1000.0, strip::eq::band2) == Approx(6.0).margin(0.01));
    CHECK(strip::eq::responseDb(params.data(), rate, 30.0, strip::eq::band2) == Approx(0.0).margin(0.1));

    // The output gain (with the other bands flat again).
    eq->setParameter(band2Gain, 0.0f);
    eq->setParameter(strip::eq::paramIndex(strip::eq::lowCut, strip::eq::on), 0.0f);
    eq->setParameter(strip::eq::output, -6.0f);
    CHECK(responseDb(*eq, 5000.0) == Approx(-6.0f).margin(0.1f));
}

TEST_CASE("Strip EQ: every cut slope falls at its rate", "[plugins][strip][eq]")
{
    std::vector<float> params = strip::defaultStrip()[1].params;
    params[static_cast<size_t>(strip::eq::paramIndex(strip::eq::highCut, strip::eq::on))] = 1.0f;
    params[static_cast<size_t>(strip::eq::paramIndex(strip::eq::highCut, strip::eq::frequency))] = 2000.0f;
    for (size_t s = 0; s < strip::eq::slopes.size(); ++s)
    {
        params[static_cast<size_t>(strip::eq::paramIndex(strip::eq::highCut, strip::eq::type))] =
            static_cast<float>(s);
        // Butterworth: -3 dB at the corner, the slope's rate an octave or more past it.
        CHECK(strip::eq::responseDb(params.data(), rate, 2000.0, strip::eq::highCut) ==
              Approx(-3.0).margin(0.05));
        const double fall = strip::eq::responseDb(params.data(), rate, 4000.0, strip::eq::highCut) -
                            strip::eq::responseDb(params.data(), rate, 8000.0, strip::eq::highCut);
        CHECK(fall == Approx(strip::eq::slopes[s]).margin(strip::eq::slopes[s] * 0.15));
    }
}

TEST_CASE("Strip compressor reduces by the curve and reports it", "[plugins][strip][compressor]")
{
    auto c = makeModule(strip::types::compressor);
    enable(*c);
    c->setParameter(strip::compressor::threshold, -18.0f);
    c->setParameter(strip::compressor::ratio, 4.0f);
    c->setParameter(strip::compressor::knee, 0.0f);
    c->setParameter(strip::compressor::attack, 1.0f);
    c->setParameter(strip::compressor::release, 50.0f);
    // A -6 dBFS sine: 12 dB over, 9 dB of reduction at 4:1 (the peak detector
    // ripples a little at 100 Hz, so the RMS reduction is near, not exact).
    const float db = responseDb(*c, 1000.0, 0.5f);
    CHECK(db == Approx(-9.0f).margin(0.8f));
    const auto frame = c->feedback()->latest();
    CHECK(frame.reduction == Approx(9.0f).margin(1.0f));
    CHECK(frame.input == Approx(0.5f).margin(0.01f));
    CHECK(frame.output < frame.input);

    // Below the threshold nothing happens.
    CHECK(responseDb(*c, 100.0, 0.05f) == Approx(0.0f).margin(0.05f));

    // Auto make-up adds half the full-scale reduction.
    CHECK(strip::compressor::autoMakeupDb(-18.0f, 4.0f) == Approx(6.75f));
    // Mix 0 is the dry signal.
    c->setParameter(strip::compressor::mix, 0.0f);
    CHECK(responseDb(*c, 100.0, 0.5f) == Approx(0.0f).margin(0.05f));
}

TEST_CASE("Strip gate attenuates below the threshold and opens above", "[plugins][strip][gate]")
{
    auto g = makeModule(strip::types::gate);
    enable(*g);
    g->setParameter(strip::gate::threshold, -30.0f);
    g->setParameter(strip::gate::range, 40.0f);
    CHECK(responseDb(*g, 440.0, 0.01f) == Approx(-40.0f).margin(0.5f)); // -40 dBFS: closed
    CHECK(responseDb(*g, 440.0, 0.25f) == Approx(0.0f).margin(0.1f));   // -12 dBFS: open
    // A key high-pass above the tone keeps it closed.
    g->setParameter(strip::gate::keyHighPass, 2000.0f);
    CHECK(responseDb(*g, 100.0, 0.1f) < -30.0f);
}

TEST_CASE("Strip de-esser reduces the sibilance band and leaves the body", "[plugins][strip][deesser]")
{
    auto d = makeModule(strip::types::deesser);
    enable(*d);
    d->setParameter(strip::deesser::frequency, 5000.0f);
    d->setParameter(strip::deesser::threshold, -30.0f);
    d->setParameter(strip::deesser::range, 10.0f);
    // Above the band: reduced by up to the range (the low band leaks a little this near the split).
    const float sibilance = responseDb(*d, 9000.0, 0.25f);
    CHECK(sibilance < -8.0f);
    CHECK(sibilance > -10.2f);
    CHECK(responseDb(*d, 200.0, 0.25f) == Approx(0.0f).margin(0.1f)); // below the band: untouched
    // The crossover sums back flat when nothing is reduced.
    d->setParameter(strip::deesser::threshold, 0.0f);
    CHECK(responseDb(*d, 5000.0, 0.25f) == Approx(0.0f).margin(0.05f));
}

TEST_CASE("Strip saturation: transparent when quiet, bounded when driven", "[plugins][strip][saturation]")
{
    auto s = makeModule(strip::types::saturation);
    enable(*s);
    s->setParameter(strip::saturation::drive, 0.0f);
    CHECK(responseDb(*s, 1000.0, 0.01f) == Approx(0.0f).margin(0.1f));
    for (int type = 0; type < 3; ++type)
    {
        s->setParameter(strip::saturation::type, static_cast<float>(type));
        s->setParameter(strip::saturation::drive, 30.0f);
        s->setParameter(strip::saturation::output, 0.0f);
        // The curve saturates near unity; the trim takes 15 dB back off.
        CHECK(peakAfter(*s, 200.0, 0.9f) < 1.3f * std::pow(10.0f, -15.0f / 20.0f) + 0.05f);
    }
    // The curves: odd for tape and clip, biased for tube.
    CHECK(strip::saturation::shape(strip::saturation::tape, -0.5f) ==
          Approx(-strip::saturation::shape(strip::saturation::tape, 0.5f)));
    CHECK(strip::saturation::shape(strip::saturation::tube, 0.0f) == Approx(0.0f).margin(1e-6));
    CHECK(strip::saturation::shape(strip::saturation::tube, 1.0f) !=
          Approx(-strip::saturation::shape(strip::saturation::tube, -1.0f)));
    CHECK(strip::saturation::shape(strip::saturation::clip, 3.0f) == 1.0f);
}

TEST_CASE("Strip limiter never lets a peak past the ceiling", "[plugins][strip][limiter]")
{
    auto l = makeModule(strip::types::limiter);
    enable(*l);
    CHECK(l->latencySamples() == 0);
    l->setParameter(strip::limiter::ceiling, -6.0f);
    l->setParameter(strip::limiter::input, 12.0f);
    const float ceiling = std::pow(10.0f, -6.0f / 20.0f);
    CHECK(peakAfter(*l, 100.0, 0.9f) <= ceiling + 1e-6f);
    CHECK(l->feedback()->latest().reduction > 10.0f);
}

TEST_CASE("Feedback publishes 200 frames a second and taps on request", "[plugins][strip][feedback]")
{
    auto eq = makeModule(strip::types::eq);
    enable(*eq);
    auto* fb = eq->feedback();
    std::vector<float> l(4800, 0.5f), r(4800, 0.5f);
    for (int done = 0; done < 4800; done += 480)
        eq->process(l.data() + done, r.data() + done, 480);
    std::uint64_t cursor = 0;
    std::vector<engine::ProcessorFeedback::Frame> frames(64);
    CHECK(fb->readHistory(cursor, frames.data(), 64) == 20); // 100 ms
    CHECK(frames[5].input == Approx(0.5f));
    CHECK(fb->readHistory(cursor, frames.data(), 64) == 0); // nothing new
    CHECK(fb->tapPosition(1) == 0);                         // taps are off until asked for
    fb->setTapsEnabled(true);
    eq->process(l.data(), r.data(), 480);
    CHECK(fb->tapPosition(0) == 480);
    std::vector<float> tap(256);
    CHECK(fb->readTap(1, tap.data(), 256));
    CHECK(tap[100] == Approx(0.5f).margin(1e-4));
    CHECK_FALSE(fb->readTap(1, tap.data(), 1024)); // not that much yet
}

TEST_CASE("A switched-off module measures its input only while watched", "[plugins][strip][feedback]")
{
    auto c = makeModule(strip::types::compressor);
    c->setBypassed(true);
    c->prepare(rate, 256); // settles the bypass
    std::vector<float> l(2400, 0.25f), r(2400, 0.25f);
    c->process(l.data(), r.data(), 2400);
    CHECK(c->feedback()->framesWritten() == 0);
    CHECK(l[100] == 0.25f); // untouched
    c->feedback()->setWatched(true);
    c->process(l.data(), r.data(), 2400);
    CHECK(c->feedback()->framesWritten() == 10);
    CHECK(c->feedback()->latest().input == Approx(0.25f));
    CHECK(c->feedback()->latest().reduction == 0.0f);
}

TEST_CASE("A keyed compressor meters its key as its input", "[plugins][strip][compressor][sidechain]")
{
    // The insert compressor: its input meter (and the threshold beside it) shows
    // what the detector hears, so with a key it reads the key, not the audio.
    auto c = plugins::BuiltInFactory().create(plugins::types::compressor);
    REQUIRE(c != nullptr);
    REQUIRE(c->acceptsSidechain());
    c->prepare(rate, 2400);
    c->setBypassed(false);
    c->feedback()->setWatched(true);
    std::vector<float> l(2400, 0.1f), r(2400, 0.1f), keyL(2400, 0.6f), keyR(2400, 0.6f);
    c->processWithSidechain(l.data(), r.data(), keyL.data(), keyR.data(), 2400);
    CHECK(c->feedback()->latest().input == Approx(0.6f));
    std::fill(l.begin(), l.end(), 0.1f);
    std::fill(r.begin(), r.end(), 0.1f);
    c->process(l.data(), r.data(), 2400); // no key: the audio
    CHECK(c->feedback()->latest().input == Approx(0.1f));
}

// ----- model ------------------------------------------------------------------------------------

TEST_CASE("Strip modules are inserts to the commands, in processing order", "[core][strip]")
{
    core::Project p;
    core::UndoStack undo;
    auto add = std::make_unique<core::AddTrack>(core::Track{});
    add->setDefaultStrip(strip::defaultStrip());
    auto* raw = add.get();
    undo.execute(p, std::move(add));
    auto& track = p.tracks.front();
    REQUIRE(track.strip.size() == strip::moduleTypes().size());
    for (const auto& m : track.strip)
        CHECK(m.id != core::invalidId);
    const auto compId = track.strip[2].id;
    REQUIRE(track.findInsert(compId) != nullptr);
    CHECK(track.insertIndex(compId) == -1);
    CHECK(track.stripIndex(compId) == 2);

    // Switching on is the bypass command; a parameter is the parameter command.
    undo.execute(p, std::make_unique<core::SetInsertBypass>(track.id, compId, false));
    undo.execute(p, std::make_unique<core::SetInsertParameter>(track.id, compId, 0, -30.0f));
    CHECK_FALSE(p.tracks[0].findInsert(compId)->bypass);
    CHECK(p.tracks[0].findInsert(compId)->params[0] == -30.0f);

    // Redo of the track brings back the same module ids.
    undo.undo(p);
    undo.undo(p);
    undo.undo(p);
    CHECK(p.tracks.empty());
    undo.redo(p);
    CHECK(p.tracks[0].strip[2].id == compId);
    CHECK(raw->trackId() == p.tracks[0].id);
    undo.redo(p);
    undo.redo(p);
    CHECK(p.tracks[0].findInsert(compId)->params[0] == -30.0f);

    // The chain: inserts then strip, or strip first.
    core::Insert gain;
    gain.type = plugins::types::gain;
    undo.execute(p, std::make_unique<core::AddInsert>(p.tracks[0].id, gain));
    auto chain = p.tracks[0].processingChain();
    REQUIRE(chain.size() == strip::moduleTypes().size() + 1);
    CHECK(chain.front()->type == plugins::types::gain);
    undo.execute(p, std::make_unique<core::SetStripPosition>(p.tracks[0].id, true));
    chain = p.tracks[0].processingChain();
    CHECK(chain.back()->type == plugins::types::gain);
    CHECK(chain.front()->type == strip::types::gate);
    undo.undo(p);
    CHECK_FALSE(p.tracks[0].stripPreInserts);

    // Reordering the strip.
    undo.execute(p, std::make_unique<core::MoveStripModule>(p.tracks[0].id, compId, 0));
    CHECK(p.tracks[0].strip[0].id == compId);
    CHECK(p.tracks[0].strip[1].type == strip::types::gate);
    undo.undo(p);
    CHECK(p.tracks[0].strip[2].id == compId);
}

TEST_CASE("The strip survives a save and older projects get one", "[core][strip]")
{
    core::Project p;
    core::UndoStack undo;
    auto add = std::make_unique<core::AddBus>(core::Bus{});
    add->setDefaultStrip(strip::defaultStrip());
    undo.execute(p, std::move(add));
    p.buses[0].strip[1].bypass = false;
    p.buses[0].strip[1].params[5] = 440.0f;
    p.buses[0].stripPreInserts = true;
    const auto loaded = core::projectFromJson(core::projectToJson(p));
    REQUIRE(loaded.has_value());
    CHECK(loaded->buses[0].strip == p.buses[0].strip);
    CHECK(loaded->buses[0].stripPreInserts);

    // A track without a strip (an older file) gets every module with fresh ids;
    // one missing a module gets just that one.
    core::Project old = *loaded;
    old.tracks.push_back(core::Track{});
    old.tracks[0].id = old.allocateId();
    old.buses[0].strip.pop_back();
    CHECK(strip::ensureChannelStrip(old));
    CHECK(old.tracks[0].strip.size() == strip::moduleTypes().size());
    CHECK(old.buses[0].strip.size() == strip::moduleTypes().size());
    CHECK(old.buses[0].strip.back().type == strip::types::limiter);
    std::vector<core::Id> ids;
    for (const auto& m : old.tracks[0].strip)
        ids.push_back(m.id);
    for (const auto& m : old.buses[0].strip)
        ids.push_back(m.id);
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    CHECK_FALSE(strip::ensureChannelStrip(old));
}

// ----- presets ----------------------------------------------------------------------------------

#include "plugins/StripPresets.h"

TEST_CASE("Every factory strip preset is complete and in range", "[plugins][strip][presets]")
{
    const auto& presets = strip::factoryPresets();
    REQUIRE(presets.size() >= 10);
    for (const char* name : {"Lead vocal",
                             "Acoustic guitar",
                             "Electric guitar",
                             "Bass",
                             "Kick",
                             "Snare",
                             "Drum bus",
                             "Mix bus glue"})
        CHECK(strip::findFactoryPreset(name) != nullptr);
    CHECK(strip::findFactoryPreset("nope") == nullptr);
    for (const auto& preset : presets)
    {
        INFO(preset.name);
        CHECK_FALSE(preset.category.empty());
        CHECK_FALSE(preset.description.empty());
        REQUIRE(preset.modules.size() == strip::moduleTypes().size());
        bool anyOn = false;
        for (size_t m = 0; m < preset.modules.size(); ++m)
        {
            const auto& module = preset.modules[m];
            CHECK(strip::slotOf(module.type) == strip::moduleTypes()[m]);
            anyOn = anyOn || !module.bypass;
            const auto& infos = plugins::parameterInfo(module.type);
            REQUIRE(module.params.size() == infos.size());
            for (size_t i = 0; i < infos.size(); ++i)
            {
                INFO(module.type << " " << infos[i].name);
                CHECK(module.params[i] >= infos[i].minimum);
                CHECK(module.params[i] <= infos[i].maximum);
            }
        }
        CHECK(anyOn);
    }
    // The lead vocal preset: EQ, compressor, de-esser and saturation on; a 24 dB cut at 90 Hz.
    const auto* vocal = strip::findFactoryPreset("Lead vocal");
    CHECK(vocal->modules[0].bypass);       // gate
    CHECK_FALSE(vocal->modules[1].bypass); // EQ
    const auto& eq = vocal->modules[1].params;
    CHECK(eq[static_cast<size_t>(strip::eq::paramIndex(strip::eq::lowCut, strip::eq::frequency))] == 90.0f);
    CHECK(strip::eq::slopes[static_cast<size_t>(
              eq[static_cast<size_t>(strip::eq::paramIndex(strip::eq::lowCut, strip::eq::type))])] == 24);
}

TEST_CASE("Applying a strip preset is one undoable step that keeps the module ids and order",
          "[core][strip][presets]")
{
    core::Project p;
    core::UndoStack undo;
    auto add = std::make_unique<core::AddTrack>(core::Track{});
    add->setDefaultStrip(strip::defaultStrip());
    undo.execute(p, std::move(add));
    const auto trackId = p.tracks[0].id;
    undo.execute(p, std::make_unique<core::MoveStripModule>(trackId, p.tracks[0].strip[5].id, 0));
    const auto before = p.tracks[0].strip;
    const auto* kick = strip::findFactoryPreset("Kick");
    undo.execute(p, std::make_unique<core::SetStripState>(trackId, kick->modules, "Strip preset: Kick"));
    const auto& after = p.tracks[0].strip;
    REQUIRE(after.size() == before.size());
    for (size_t i = 0; i < after.size(); ++i)
    {
        CHECK(after[i].id == before[i].id);
        CHECK(after[i].type == before[i].type);
        for (const auto& m : kick->modules)
            if (m.type == after[i].type)
            {
                CHECK(after[i].bypass == m.bypass);
                CHECK(after[i].params == m.params);
            }
    }
    CHECK(undo.undoName() == "Strip preset: Kick");
    undo.undo(p);
    CHECK(p.tracks[0].strip == before);
}

// ----- opto compressor --------------------------------------------------------------------------

namespace
{
// Runs `seconds` of a sine through the processor (its state carries on).
void play(engine::InsertProcessor& p, double hz, float amplitude, double seconds)
{
    const int n = static_cast<int>(rate * seconds);
    std::vector<float> l(256), r(256);
    for (int done = 0; done < n; done += 256)
    {
        const int count = std::min(256, n - done);
        for (int i = 0; i < count; ++i)
            l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
                amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * (done + i) / rate));
        p.process(l.data(), r.data(), count);
    }
}
} // namespace

TEST_CASE("Opto: the loop settles where the curve says, softer compressing than limiting",
          "[plugins][strip][opto]")
{
    // The steady curve: nothing at low levels, then a soft ratio of 1 + loop gain.
    CHECK(strip::opto::steadyReductionDb(-60.0f, 40.0f, 0) < 0.05f);
    const auto ratio = [](int mode)
    {
        const float a = strip::opto::steadyReductionDb(-10.0f, 80.0f, mode);
        const float b = strip::opto::steadyReductionDb(0.0f, 80.0f, mode);
        return 10.0f / (10.0f - (b - a)); // between -10 and 0 dB in
    };
    CHECK(ratio(0) == Approx(3.0f).margin(0.3f));
    CHECK(ratio(1) == Approx(10.0f).margin(2.0f));
    // More Peak reduction, more reduction.
    CHECK(strip::opto::steadyReductionDb(-12.0f, 70.0f, 0) >
          strip::opto::steadyReductionDb(-12.0f, 40.0f, 0));

    auto o = makeModule(strip::types::opto);
    enable(*o);
    o->setParameter(strip::opto::peakReduction, 60.0f);
    o->setParameter(strip::opto::colour, 0.0f);
    // A -6 dBFS sine settles near the curve's reduction (the detector is a
    // peak envelope, so it hears the sine's peak).
    const float expected = strip::opto::steadyReductionDb(-6.0f, 60.0f, 0);
    REQUIRE(expected > 6.0f);
    const float db = responseDb(*o, 1000.0, 0.5f, 3.0);
    CHECK(db == Approx(-expected).margin(1.0f));
    const auto frame = o->feedback()->latest();
    CHECK(frame.reduction == Approx(expected).margin(1.0f));
    CHECK(frame.extra > 0.3f); // the cell's memory filled

    // Gain is make-up after the cell; Mix 0 is the dry signal.
    o->setParameter(strip::opto::gain, 6.0f);
    CHECK(responseDb(*o, 1000.0, 0.5f, 3.0) == Approx(-expected + 6.0f).margin(1.0f));
    o->setParameter(strip::opto::mix, 0.0f);
    CHECK(responseDb(*o, 1000.0, 0.5f) == Approx(0.0f).margin(0.05f));
}

TEST_CASE("Opto: transparent when quiet; Colour adds harmonics without changing quiet gain",
          "[plugins][strip][opto]")
{
    auto o = makeModule(strip::types::opto);
    enable(*o);
    o->setParameter(strip::opto::peakReduction, 0.0f);
    o->setParameter(strip::opto::colour, 0.0f);
    CHECK(responseDb(*o, 1000.0, 0.01f) == Approx(0.0f).margin(0.05f));
    o->setParameter(strip::opto::colour, 100.0f);
    // Small signals pass at unity through the valve.
    CHECK(responseDb(*o, 1000.0, 0.01f) == Approx(0.0f).margin(0.1f));
    // A loud one is shaped: its peak is held down, and the output stays bounded.
    const float peak = peakAfter(*o, 1000.0, 0.9f);
    CHECK(peak < 0.85f);
    CHECK(peak > 0.3f);
}

TEST_CASE("Opto: the release is quick after a short burst and slow after a long one",
          "[plugins][strip][opto]")
{
    // The reduction 300 ms after the signal falls below the knee.
    struct Result
    {
        float during = 0.0f, memory = 0.0f, after = 0.0f;
    };
    const auto afterBurst = [](double burstSeconds)
    {
        auto o = makeModule(strip::types::opto);
        enable(*o);
        o->setParameter(strip::opto::peakReduction, 80.0f);
        play(*o, 1000.0, 0.5f, burstSeconds);
        Result r;
        r.during = o->feedback()->latest().reduction;
        r.memory = o->feedback()->latest().extra;
        play(*o, 1000.0, 0.001f, 0.3);
        r.after = o->feedback()->latest().reduction;
        return r;
    };
    const auto burst = afterBurst(0.1);
    const auto sustained = afterBurst(6.0);
    CHECK(burst.during > 15.0f);
    CHECK(sustained.during > 15.0f);
    CHECK(burst.memory < 0.2f);
    CHECK(sustained.memory > 0.8f);
    // A short burst lets most of its reduction go in the fast stage; a long one
    // holds most of it.
    CHECK(burst.after < 0.2f * burst.during);
    CHECK(sustained.after > 0.6f * sustained.during);
    // The slow stage: under half a second when fresh, seconds when charged.
    CHECK(strip::opto::slowReleaseSeconds(0.0f) < 0.5f);
    CHECK(strip::opto::slowReleaseSeconds(1.0f) > 3.0f);
}

TEST_CASE("The compressor slot swaps between the clean and the opto models", "[core][strip][opto]")
{
    CHECK(strip::slotOf(strip::types::opto) == strip::types::compressor);
    CHECK(strip::slotOf(strip::types::gate) == strip::types::gate);
    CHECK(strip::alternatives(strip::types::compressor) ==
          std::vector<std::string>{strip::types::compressor, strip::types::opto});
    CHECK(strip::alternatives(strip::types::opto).size() == 2);
    CHECK(strip::alternatives(strip::types::eq) == std::vector<std::string>{strip::types::eq});
    CHECK(strip::isStripType(strip::types::opto));
    // The built-in insert runs the same processor.
    auto insert = plugins::BuiltInFactory().create(plugins::types::opto);
    REQUIRE(insert != nullptr);
    CHECK(insert->parameters().size() == static_cast<size_t>(strip::opto::count));
    CHECK(plugins::BuiltInFactory::displayName(plugins::types::opto) == "Opto Compressor");

    core::Project p;
    core::UndoStack undo;
    auto add = std::make_unique<core::AddTrack>(core::Track{});
    add->setDefaultStrip(strip::defaultStrip());
    undo.execute(p, std::move(add));
    const auto trackId = p.tracks[0].id;
    const auto compId = p.tracks[0].strip[2].id;
    undo.execute(p, std::make_unique<core::SetInsertBypass>(trackId, compId, false));
    core::AutomationLane lane;
    lane.id = p.allocateId();
    lane.target = core::AutomationTarget::insertParameter;
    lane.insert = compId;
    lane.parameter = strip::compressor::threshold;
    lane.points.push_back({0, -20.0f});
    p.tracks[0].automation.push_back(lane);

    undo.execute(
        p,
        std::make_unique<core::SetStripModuleType>(
            trackId, compId, strip::types::opto, plugins::defaultParameters(strip::types::opto), "Swap"));
    const auto& swapped = p.tracks[0].strip[2];
    CHECK(swapped.id == compId);
    CHECK(swapped.type == strip::types::opto);
    CHECK_FALSE(swapped.bypass); // still on
    CHECK(swapped.params.size() == static_cast<size_t>(strip::opto::count));
    CHECK(p.tracks[0].automation.empty()); // the old model's lane went with it

    // A project with the opto keeps it: no second compressor is added, and it saves.
    CHECK_FALSE(strip::ensureChannelStrip(p));
    const auto loaded = core::projectFromJson(core::projectToJson(p));
    REQUIRE(loaded.has_value());
    CHECK(loaded->tracks[0].strip == p.tracks[0].strip);

    undo.undo(p);
    CHECK(p.tracks[0].strip[2].type == strip::types::compressor);
    CHECK(p.tracks[0].strip[2].params.size() == static_cast<size_t>(strip::compressor::count));
    REQUIRE(p.tracks[0].automation.size() == 1);
    CHECK(p.tracks[0].automation[0].insert == compId);
}

TEST_CASE("A preset swaps the compressor slot to the model it names", "[core][strip][presets][opto]")
{
    const auto* vocal = strip::findFactoryPreset("Lead vocal");
    const auto* kick = strip::findFactoryPreset("Kick");
    REQUIRE(vocal != nullptr);
    REQUIRE(kick != nullptr);
    CHECK(vocal->modules[2].type == strip::types::opto);
    CHECK_FALSE(vocal->modules[2].bypass);
    CHECK(kick->modules[2].type == strip::types::compressor);

    core::Project p;
    core::UndoStack undo;
    auto add = std::make_unique<core::AddTrack>(core::Track{});
    add->setDefaultStrip(strip::defaultStrip());
    undo.execute(p, std::move(add));
    const auto before = p.tracks[0].strip;
    const auto apply = [&](const strip::Preset& preset)
    {
        undo.beginGroup("Strip preset: " + preset.name);
        for (auto& command :
             strip::presetCommands(p.tracks[0], preset.modules, "Strip preset: " + preset.name))
            undo.execute(p, std::move(command));
        undo.endGroup();
    };

    apply(*vocal);
    const auto& slot = p.tracks[0].strip[2];
    CHECK(slot.id == before[2].id);
    CHECK(slot.type == strip::types::opto);
    CHECK(slot.params == vocal->modules[2].params);
    CHECK_FALSE(slot.bypass);

    // A clean preset swaps it back.
    apply(*kick);
    CHECK(p.tracks[0].strip[2].type == strip::types::compressor);
    CHECK(p.tracks[0].strip[2].params == kick->modules[2].params);

    undo.undo(p);
    CHECK(p.tracks[0].strip[2].type == strip::types::opto);
    undo.undo(p);
    CHECK(p.tracks[0].strip == before);
}
