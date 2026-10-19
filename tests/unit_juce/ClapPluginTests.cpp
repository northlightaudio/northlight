#include "engine/Midi.h"
#include "engine/rt/RealtimeGuard.h"
#include "hosting/ClapPlugin.h"
#include "hosting/HostFactory.h"
#include "hosting/PluginRegistry.h"
#include "hosting/PluginScanner.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
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
    REQUIRE(found.size() == 1);
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
