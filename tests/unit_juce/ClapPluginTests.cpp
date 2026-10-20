#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/Insert.h"
#include "engine/Midi.h"
#include "engine/rt/RealtimeGuard.h"
#include "hosting/ClapPlugin.h"
#include "hosting/HostFactory.h"
#include "hosting/PluginRegistry.h"
#include "hosting/PluginScanner.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace aural;
using Catch::Approx;

// The CLAP host against the test effect built beside the tests (AuralTestGain.clap:
// a gain with 32 samples of latency, a key input and a one-value state).

namespace
{
juce::File testClap()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getSiblingFile("AuralTestGain.clap");
}

std::atomic<int> clapViolations{0};
void countClapViolation(const rt::Violation&) noexcept
{
    clapViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("The scanner lists a CLAP file's plugins with their format", "[hosting][clap][scan]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    const auto scanner = hosting::PluginScanner::scannerExecutable();
    REQUIRE(scanner.existsAsFile());
    std::vector<hosting::PluginInfo> found;
    std::string error;
    REQUIRE(hosting::PluginScanner::inspect(scanner, testClap(), found, error));
    REQUIRE(found.size() == 5);
    CHECK(found[0].format == "CLAP");
    CHECK(found[0].id == "io.leafshade.aural.testgain");
    CHECK(found[0].name == "Aural Test Gain");
    CHECK(found[0].manufacturer == "Leafshade");
    CHECK_FALSE(found[0].isInstrument);
    CHECK(found[0].category.find("audio-effect") != std::string::npos);
    hosting::PluginRegistry registry;
    registry.setFileResults(testClap().getFullPathName().toStdString(), 1, found);
    CHECK(hosting::PluginRegistry::typeFor(registry.plugins()[0]) == "clap:io.leafshade.aural.testgain");
    CHECK(registry.findByType("clap:io.leafshade.aural.testgain") != nullptr);
    CHECK(registry.findByType("vst3:io.leafshade.aural.testgain") == nullptr);
    const auto back = hosting::PluginRegistry::fromJson(registry.toJson());
    REQUIRE(back.has_value());
    CHECK(back->plugins()[0].format == "CLAP");
}

TEST_CASE("A CLAP plugin loads, delays by its latency, takes parameters and keeps its state",
          "[hosting][clap][plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    hosting::PluginRegistry registry;
    hosting::PluginInfo info;
    info.format = "CLAP";
    info.id = "io.leafshade.aural.testgain";
    info.name = "Aural Test Gain";
    registry.setFileResults(testClap().getFullPathName().toStdString(), 1, {info});
    hosting::HostFactory factory(registry);
    CHECK(factory.displayName("clap:io.leafshade.aural.testgain") == "Aural Test Gain");

    auto processor = factory.create("clap:io.leafshade.aural.testgain");
    INFO(factory.lastError());
    REQUIRE(processor != nullptr);
    auto* clap = dynamic_cast<hosting::HostedClapPlugin*>(processor.get());
    REQUIRE(clap != nullptr);
    REQUIRE(processor->parameters().size() == 1);
    CHECK(processor->parameters()[0].name == "Gain");
    CHECK(processor->acceptsSidechain());
    CHECK_FALSE(clap->hasEditor());
    processor->prepare(48000.0, 256);
    CHECK(processor->latencySamples() == 32);

    // An impulse comes out 32 samples later at unity gain.
    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    l[0] = r[0] = 1.0f;
    processor->process(l.data(), r.data(), 256);
    CHECK(l[0] == 0.0f);
    CHECK(l[32] == Approx(1.0f));
    CHECK(r[32] == Approx(1.0f));

    // A parameter change travels through the event queue into the next block.
    processor->setParameter(0, 0.5f);
    CHECK(processor->parameter(0) == 0.5f);
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    l[0] = r[0] = 1.0f;
    processor->process(l.data(), r.data(), 256);
    CHECK(l[32] == Approx(0.5f));

    // The key input replaces the audio when the host feeds one.
    std::vector<float> key(256, 0.25f);
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    processor->processWithSidechain(l.data(), r.data(), key.data(), key.data(), 256);
    CHECK(l[100] == Approx(0.125f)); // 0.25 through the gain of 0.5

    // State round trip: a second instance restored from the first has its gain.
    const auto state = processor->state();
    REQUIRE_FALSE(state.empty());
    auto second = factory.create("clap:io.leafshade.aural.testgain");
    REQUIRE(second != nullptr);
    second->prepare(48000.0, 256);
    second->setState(state);
    CHECK(second->parameter(0) == Approx(0.5f));
    CHECK(second->state() == state);

    // Presets as files under the CLAP folder layout.
    CHECK(clap->supportsPresetFiles());
    CHECK(clap->userPresetFolder().getFullPathName().contains("CLAP Presets"));
    const auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("aural-test.clappreset");
    file.deleteFile();
    REQUIRE(clap->savePresetFile(file));
    second->setParameter(0, 1.5f);
    REQUIRE(dynamic_cast<hosting::HostedClapPlugin*>(second.get())->loadPresetFile(file));
    CHECK(second->parameter(0) == Approx(0.5f));
    file.deleteFile();

    // Bypass crossfades and settles on the dry input; the latency stays.
    processor->setBypassed(true);
    for (int block = 0; block < 10; ++block)
    {
        std::fill(l.begin(), l.end(), 0.5f);
        std::fill(r.begin(), r.end(), 0.5f);
        processor->process(l.data(), r.data(), 256);
    }
    CHECK(l[200] == Approx(0.5f));
    CHECK(processor->latencySamples() == 32);
    processor->setBypassed(false);
}

#if defined(AURAL_RT_CHECKS)
TEST_CASE("A CLAP plugin with a note port gets the block's notes at their offsets", "[hosting][clap][midi]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    hosting::PluginRegistry registry;
    hosting::PluginInfo info;
    info.format = "CLAP";
    info.id = "io.leafshade.aural.testgain";
    info.name = "Aural Test Gain";
    registry.setFileResults(testClap().getFullPathName().toStdString(), 1, {info});
    hosting::HostFactory factory(registry);
    auto processor = factory.create("clap:io.leafshade.aural.testgain");
    REQUIRE(processor != nullptr);
    CHECK(processor->acceptsMidi());
    processor->prepare(48000.0, 256);

    engine::MidiEventList events;
    events.noteOn(10, 60, 127);
    events.noteOn(40, 64, 64);
    events.noteOff(100, 60);
    events.allNotesOff(200);
    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    processor->processMidi(events, l.data(), r.data(), 256);
    CHECK(l[10] == Approx(1.0f));
    CHECK(l[40] == Approx(64.0f / 127.0f).margin(0.01f));
    CHECK(l[100] == Approx(-1.0f));
    CHECK(l[0] == 0.0f);
    CHECK(l[200] == 0.0f); // a choke is not a note-off marker
    CHECK(r[10] == 0.0f);

    // Without events the block is plain audio again.
    std::fill(l.begin(), l.end(), 0.0f);
    processor->process(l.data(), r.data(), 256);
    CHECK(l[10] == 0.0f);
}

TEST_CASE("Processing a CLAP plugin allocates nothing on the real-time thread", "[hosting][clap][rt]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    std::string error;
    auto plugin = hosting::HostedClapPlugin::create("clap:io.leafshade.aural.testgain",
                                                    testClap().getFullPathName().toStdString(),
                                                    "io.leafshade.aural.testgain",
                                                    error);
    REQUIRE(plugin != nullptr);
    plugin->prepare(48000.0, 256);
    std::vector<float> l(256, 0.1f), r(256, 0.1f), key(256, 0.2f);
    const auto previous = rt::setViolationHandler(&countClapViolation);
    clapViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int block = 0; block < 50; ++block)
        {
            if (block == 10)
                plugin->setParameter(0, 0.75f);
            if (block == 20)
                plugin->setBypassed(true);
            if (block % 2 == 0)
                plugin->process(l.data(), r.data(), 256);
            else
                plugin->processWithSidechain(l.data(), r.data(), key.data(), key.data(), 256);
        }
    }
    rt::setViolationHandler(previous);
    CHECK(clapViolations == 0);
}
#endif

namespace
{
// A registry holding every plugin of the test CLAP, and a factory over it.
hosting::PluginRegistry testRegistry()
{
    hosting::PluginRegistry registry;
    std::vector<hosting::PluginInfo> infos;
    for (const char* id : {"io.leafshade.aural.testgain",
                           "io.leafshade.aural.testgain.quad",
                           "io.leafshade.aural.testgain.multiout",
                           "io.leafshade.aural.testgain.noinplace",
                           "io.leafshade.aural.testgain.callback"})
    {
        hosting::PluginInfo info;
        info.format = "CLAP";
        info.id = id;
        info.name = id;
        info.path = testClap().getFullPathName().toStdString();
        infos.push_back(info);
    }
    registry.setFileResults(testClap().getFullPathName().toStdString(), 1, infos);
    return registry;
}

// The test plugin's state: one double (its gain), base64 like the host stores it.
std::string gainState(double gain)
{
    return juce::Base64::toBase64(&gain, sizeof(gain)).toStdString();
}

class NoMedia final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double, std::string& error) override
    {
        error = "no media";
        return nullptr;
    }
};
} // namespace

TEST_CASE("Every port layout the plugin declares is honoured or refused, never indexed past",
          "[hosting][clap][ports]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    const auto registry = testRegistry();
    hosting::HostFactory factory(registry);

    SECTION("a four-channel main port is refused with a message the user can read")
    {
        auto quad = factory.create("clap:io.leafshade.aural.testgain.quad");
        CHECK(quad == nullptr);
        CHECK(factory.lastError().find("channels") != std::string::npos);
    }
    SECTION("a second output port is drained to scratch and the main pair still processes")
    {
        auto processor = factory.create("clap:io.leafshade.aural.testgain.multiout");
        INFO(factory.lastError());
        REQUIRE(processor != nullptr);
        auto* clap = dynamic_cast<hosting::HostedClapPlugin*>(processor.get());
        REQUIRE(clap != nullptr);
        CHECK(clap->outputPortCount() == 2);
        CHECK(clap->inputPortCount() == 1);
        CHECK(clap->outputIsInPlace());
        processor->prepare(48000.0, 256);
        std::vector<float> l(256, 0.0f), r(256, 0.0f);
        l[0] = r[0] = 1.0f;
        processor->process(l.data(), r.data(), 256);
        CHECK(l[32] == Approx(1.0f)); // the plugin processed (an error would pass through)
        CHECK(l[0] == 0.0f);
        CHECK(r[32] == Approx(1.0f));
    }
    SECTION("a plugin without an in-place pair gets its own output buffers")
    {
        auto processor = factory.create("clap:io.leafshade.aural.testgain.noinplace");
        INFO(factory.lastError());
        REQUIRE(processor != nullptr);
        auto* clap = dynamic_cast<hosting::HostedClapPlugin*>(processor.get());
        REQUIRE(clap != nullptr);
        CHECK_FALSE(clap->outputIsInPlace());
        processor->prepare(48000.0, 64);
        std::vector<float> l(64, 1.0f), r(64, 0.5f);
        processor->process(l.data(), r.data(), 64);
        // Swapped: an aliased left would have been overwritten before the right read it.
        CHECK(l[10] == Approx(0.5f));
        CHECK(r[10] == Approx(1.0f));
    }
    SECTION("the plain plugin is paired in place")
    {
        auto processor = factory.create("clap:io.leafshade.aural.testgain");
        REQUIRE(processor != nullptr);
        auto* clap = dynamic_cast<hosting::HostedClapPlugin*>(processor.get());
        REQUIRE(clap != nullptr);
        CHECK(clap->outputIsInPlace());
        CHECK(clap->mainInputChannels() == 2);
        CHECK(clap->mainOutputChannels() == 2);
    }
}

TEST_CASE("Two instances of one CLAP file share its entry point", "[hosting][clap][entry]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    const auto registry = testRegistry();
    hosting::HostFactory factory(registry);
    auto first = factory.create("clap:io.leafshade.aural.testgain");
    REQUIRE(first != nullptr);
    // The test plugin refuses a second entry init: a per-instance init would fail here.
    auto second = factory.create("clap:io.leafshade.aural.testgain.noinplace");
    INFO(factory.lastError());
    REQUIRE(second != nullptr);
    first.reset(); // the survivor keeps the entry alive
    second->prepare(48000.0, 64);
    std::vector<float> l(64, 1.0f), r(64, 0.0f);
    second->process(l.data(), r.data(), 64);
    CHECK(r[5] == Approx(1.0f));
    second.reset();
    auto third = factory.create("clap:io.leafshade.aural.testgain"); // a fresh init after deinit
    INFO(factory.lastError());
    CHECK(third != nullptr);
}

TEST_CASE("A rescan of values from a callback requested on the audio thread is serviced while processing",
          "[hosting][clap][rescan]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    std::string error;
    auto plugin = hosting::HostedClapPlugin::create("clap:io.leafshade.aural.testgain.callback",
                                                    testClap().getFullPathName().toStdString(),
                                                    "io.leafshade.aural.testgain.callback",
                                                    error);
    INFO(error);
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->parameters().size() == 2);
    plugin->prepare(48000.0, 64);
    CHECK(plugin->parameter(0) == Approx(1.0f));
    plugin->setParameter(1, 1.0f); // Trigger: ask for a callback from process()

    // The audio thread processes under the RT hook while the main thread polls.
    const auto previous = rt::setViolationHandler(&countClapViolation);
    clapViolations = 0;
    std::atomic<bool> stop{false};
    std::vector<float> l(64, 0.1f), r(64, 0.1f);
    std::thread audio(
        [&]
        {
            rt::ScopedRealtimeSection section; // everything inside is checked
            while (!stop.load(std::memory_order_acquire))
            {
                plugin->process(l.data(), r.data(), 64);
                for (int spin = 0; spin < 2000; ++spin) // a block's worth of time, without a wait
                    std::atomic_signal_fence(std::memory_order_seq_cst);
            }
        });
    bool serviced = false;
    for (int i = 0; i < 500 && !serviced; ++i)
    {
        hosting::HostedClapPlugin::serviceAll(true); // the session's poll
        serviced = plugin->parameter(0) == Approx(0.5f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    rt::setViolationHandler(previous);
    CHECK(serviced); // the callback ran and the rescan refreshed the host's view
    CHECK(clapViolations == 0);
    CHECK(plugin->parameters().size() == 2); // the tables were not rebuilt under the audio thread
    CHECK_FALSE(plugin->needsRestart());
}

TEST_CASE("A restart request, or a rescan of everything while active, is answered with a fresh instance",
          "[hosting][clap][restart]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    const auto registry = testRegistry();
    hosting::HostFactory factory(registry);
    engine::InsertCache cache(factory);
    cache.prepare(48000.0, 64);
    core::Insert insert;
    insert.id = 7;
    insert.type = "clap:io.leafshade.aural.testgain.callback";
    auto first = cache.get(insert);
    REQUIRE(first != nullptr);
    std::vector<float> l(64, 0.1f), r(64, 0.1f);
    first->setParameter(0, 0.7f);
    first->process(l.data(), r.data(), 64); // delivers the value
    CHECK(first->parameter(0) == Approx(0.7f));

    SECTION("request_restart from process()")
    {
        first->setParameter(1, 2.0f);
        first->process(l.data(), r.data(), 64);
        CHECK(first->needsRestart());
        CHECK(cache.restartRequested());
        auto second = cache.get(insert); // what the next compile does
        REQUIRE(second != nullptr);
        CHECK(second.get() != first.get());
        CHECK(second->parameter(0) == Approx(0.7f)); // the state carried over
        CHECK_FALSE(second->needsRestart());
        CHECK_FALSE(cache.restartRequested());
        first->process(l.data(), r.data(), 64); // the old instance still runs for old snapshots
    }
    SECTION("rescan(ALL) from the plugin's callback")
    {
        first->setParameter(1, 3.0f);
        first->process(l.data(), r.data(), 64); // asks for the callback
        hosting::HostedClapPlugin::serviceAll(false);
        CHECK(first->needsRestart());
        auto second = cache.get(insert);
        REQUIRE(second != nullptr);
        CHECK(second.get() != first.get());
        CHECK(second->parameter(0) == Approx(0.7f));
    }
}

TEST_CASE("A bypassed latent CLAP plugin keeps its delay, so a parallel path stays aligned",
          "[hosting][clap][bypass][pdc]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    const auto registry = testRegistry();
    hosting::HostFactory factory(registry);
    NoMedia loader;
    engine::AudioEngine engine(loader, factory, 0);
    engine.prepare(48000.0, 64);
    // Two tracks monitor device input 0: A through the bypassed plugin (latency
    // 32), B plain. The graph delays B to match; A's bypass must keep the delay.
    core::Project p;
    core::Track a, b;
    a.id = 1;
    a.inputChannel = 0;
    a.monitor = core::MonitorMode::on;
    core::Insert plugin;
    plugin.id = 10;
    plugin.type = "clap:io.leafshade.aural.testgain";
    plugin.bypass = true;
    a.inserts.push_back(plugin);
    b.id = 2;
    b.inputChannel = 0;
    b.monitor = core::MonitorMode::on;
    p.tracks = {a, b};
    p.nextId = 100;
    engine.setProject(p);
    CHECK(engine.outputLatencySamples() == 32);

    std::vector<float> in(64, 0.0f), l(64), r(64);
    const float* inputs[] = {in.data()};
    float* outputs[] = {l.data(), r.data()};
    // Settle the bypass crossfade (10 ms) with silence first.
    for (int block = 0; block < 20; ++block)
        engine.process(inputs, 1, outputs, 2, 64);
    in[0] = 1.0f; // an impulse at the start of the next block
    std::vector<float> out;
    for (int block = 0; block < 4; ++block)
    {
        engine.process(inputs, 1, outputs, 2, 64);
        out.insert(out.end(), l.begin(), l.end());
        std::fill(in.begin(), in.end(), 0.0f);
    }
    const float centre = std::cos(3.14159265f / 4.0f);
    // Both paths arrive together, 32 samples late: one impulse of twice the height.
    CHECK(out[32] == Approx(2.0f * centre).margin(0.02f));
    CHECK(out[0] == Approx(0.0f).margin(0.001f));
    float stray = 0.0f;
    for (size_t i = 0; i < out.size(); ++i)
        if (i != 32)
            stray = std::max(stray, std::abs(out[i]));
    CHECK(stray < 0.01f);
}

TEST_CASE("A multi-output CLAP instrument's second port feeds an aux track", "[hosting][clap][aux]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    const auto registry = testRegistry();
    hosting::HostFactory factory(registry);
    NoMedia loader;
    engine::AudioEngine engine(loader, factory, 0);
    engine.prepare(48000.0, 64);
    core::Project p;
    core::Track keys;
    keys.id = 1;
    keys.kind = core::TrackKind::midi;
    core::Insert instrument;
    instrument.id = 10;
    instrument.type = "clap:io.leafshade.aural.testgain.multiout";
    keys.inserts.push_back(instrument);
    core::Track aux;
    aux.id = 2;
    aux.kind = core::TrackKind::aux;
    aux.auxTrack = 1;
    aux.auxInsert = 10;
    aux.auxOutput = 1;
    p.tracks = {keys, aux};
    p.nextId = 100;
    engine.setProject(p);
    const auto live = engine.insertInstance(10);
    REQUIRE(live != nullptr);
    CHECK(live->auxOutputCount() == 1);
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};
    engine.play();
    for (int block = 0; block < 4; ++block)
        engine.process(outputs, 2, 64);
    // The plugin writes 0.25 into its second port; the main port is silent (no input).
    CHECK(l[10] == Approx(0.25f).margin(0.01f));
    CHECK(r[10] == Approx(0.25f).margin(0.01f));
}

TEST_CASE("Host parameter changes reach a CLAP plugin while the device is stopped", "[hosting][clap][flush]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    std::string error;
    auto plugin = hosting::HostedClapPlugin::create("clap:io.leafshade.aural.testgain",
                                                    testClap().getFullPathName().toStdString(),
                                                    "io.leafshade.aural.testgain",
                                                    error);
    REQUIRE(plugin != nullptr);
    plugin->prepare(48000.0, 64);
    plugin->setParameter(0, 0.25f);
    // No process() runs: the poll flushes while the engine is not running.
    hosting::HostedClapPlugin::serviceAll(false);
    CHECK(plugin->state() == gainState(0.25));
    // While the engine runs the poll leaves the queue to process().
    plugin->setParameter(0, 0.75f);
    hosting::HostedClapPlugin::serviceAll(true);
    CHECK(plugin->state() == gainState(0.25));
    std::vector<float> l(64, 0.0f), r(64, 0.0f);
    plugin->process(l.data(), r.data(), 64);
    CHECK(plugin->state() == gainState(0.75));
}

namespace
{
class MemoryStore final : public audio::AudioSettingsStore
{
public:
    audio::AudioSettings loadAudioSettings() override { return settings_; }
    void saveAudioSettings(const audio::AudioSettings& settings) override { settings_ = settings; }

private:
    audio::AudioSettings settings_;
};
} // namespace

TEST_CASE("A preset loaded from the plugin's side survives a fader move and a save and reload",
          "[hosting][clap][session][presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("aural-clap-session")
                            .getChildFile(juce::Uuid().toString());
    REQUIRE(folder.createDirectory().wasOk());
    const auto registryFile = folder.getChildFile("plugins.json");
    REQUIRE(registryFile.replaceWithText(juce::String(testRegistry().toJson())));
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    {
        ProjectSession session(audio, registryFile);
        REQUIRE(session.save(folder.getChildFile("song.aural")).empty());
        core::Track t;
        t.id = 1;
        t.name = "Keys";
        session.execute(std::make_unique<core::AddTrack>(t));
        core::Insert insert;
        insert.id = 50; // clear of the ids the channel strip takes
        insert.type = "clap:io.leafshade.aural.testgain";
        session.execute(std::make_unique<core::AddInsert>(1, insert));
        session.engine().prepare(48000.0, 64);
        const auto live = session.liveInsert(50);
        REQUIRE(live != nullptr);
        // The user loads a preset from the plugin's own menu: the host only sees the state.
        live->setState(gainState(0.5));
        CHECK(live->parameter(0) == Approx(0.5f));
        // A fader move is a live command: it must not push the model's stale gain.
        session.execute(std::make_unique<core::SetTrackGain>(1, 0.5f));
        CHECK(live->parameter(0) == Approx(0.5f));
        // An edit elsewhere recompiles: still no stomp.
        session.execute(std::make_unique<core::SetTrackName>(1, "Pads"));
        CHECK(session.liveInsert(50)->parameter(0) == Approx(0.5f));
        // The generic editor's own edit does reach the plugin, and undo restores it.
        session.execute(std::make_unique<core::SetInsertParameter>(1, 50, 0, 0.8f));
        std::vector<float> l(64, 0.0f), r(64, 0.0f);
        float* outputs[] = {l.data(), r.data()};
        session.engine().process(outputs, 2, 64); // delivers the queued value
        CHECK(session.liveInsert(50)->state() == gainState(static_cast<double>(0.8f)));
        session.undo();
        session.engine().process(outputs, 2, 64);
        CHECK(session.liveInsert(50)->state() == gainState(static_cast<double>(0.5f)));
        CHECK(session.liveInsert(50)->parameter(0) == Approx(0.5f));
        REQUIRE(session.save(folder.getChildFile("song.aural")).empty());
    }
    {
        ProjectSession reopened(audio, registryFile);
        REQUIRE(reopened.open(folder.getChildFile("song.aural")).empty());
        reopened.engine().prepare(48000.0, 64);
        const auto live = reopened.liveInsert(50);
        REQUIRE(live != nullptr);
        CHECK(live->parameter(0) == Approx(0.5f));
        CHECK(live->state() == gainState(0.5));
    }
    folder.deleteRecursively();
}

TEST_CASE("A CLAP plugin's parameter lane plays from the session and is written by touch",
          "[hosting][clap][session][automation]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    REQUIRE(testClap().existsAsFile());
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("aural-clap-automation")
                            .getChildFile(juce::Uuid().toString());
    REQUIRE(folder.createDirectory().wasOk());
    const auto registryFile = folder.getChildFile("plugins.json");
    REQUIRE(registryFile.replaceWithText(juce::String(testRegistry().toJson())));
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    {
        ProjectSession session(audio, registryFile);
        core::Track t;
        t.id = 1;
        session.execute(std::make_unique<core::AddTrack>(t));
        core::Insert insert;
        insert.id = 50;
        insert.type = "clap:io.leafshade.aural.testgain";
        session.execute(std::make_unique<core::AddInsert>(1, insert));
        session.engine().prepare(48000.0, 64);
        const auto live = session.liveInsert(50);
        REQUIRE(live != nullptr);
        REQUIRE_FALSE(live->parameters().empty());

        core::AutomationLane lane;
        lane.target = core::AutomationTarget::insertParameter;
        lane.insert = 50;
        lane.parameter = 0;
        lane.points = {{0, 0.2f}, {48000, 0.8f}};
        session.execute(std::make_unique<core::AddAutomationLane>(1, lane));
        session.playHostedAutomation();
        CHECK(live->parameter(0) == Approx(0.2f));

        // Mode off: the lane is left alone.
        session.execute(std::make_unique<core::SetAutomationMode>(1, core::AutomationMode::off));
        live->setParameter(0, 0.5f);
        session.playHostedAutomation();
        CHECK(live->parameter(0) == Approx(0.5f));

        // Touch while playing writes the lane through the plugin.
        session.execute(std::make_unique<core::SetAutomationMode>(1, core::AutomationMode::touch));
        session.play();
        std::vector<float> l(64), r(64);
        float* outputs[] = {l.data(), r.data()};
        for (int i = 0; i < 10; ++i)
            session.engine().process(nullptr, 0, outputs, 2, 64);
        session.beginGesture("Gain");
        session.execute(std::make_unique<core::SetInsertParameter>(1, 50, 0, 0.9f));
        CHECK(live->parameter(0) == Approx(0.9f));
        session.playHostedAutomation(); // a lane being written is not played
        CHECK(live->parameter(0) == Approx(0.9f));
        for (int i = 0; i < 10; ++i)
            session.engine().process(nullptr, 0, outputs, 2, 64);
        session.endGesture();
        const auto* written =
            session.project().tracks[0].findLane(core::AutomationTarget::insertParameter, 50, 0);
        REQUIRE(written != nullptr);
        CHECK(written->valueAt(640) == Approx(0.9f)); // the written point (then a glide back to the curve)
    }
    folder.deleteRecursively();
}
