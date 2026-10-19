#include "engine/rt/RealtimeGuard.h"
#include "hosting/HostFactory.h"
#include "hosting/HostedPlugin.h"
#include "hosting/PluginRegistry.h"
#include "hosting/PluginScanner.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <numbers>
#include <vector>

using namespace aural;

// These tests use whatever VST3 plugin is installed in the system folder. On a
// machine without one they report what they skipped and pass.

namespace
{
juce::File anyInstalledVst3()
{
    juce::VST3PluginFormat format;
    const auto found = format.searchPathsForPlugins(format.getDefaultLocationsToSearch(), true, false);
    return found.isEmpty() ? juce::File() : juce::File(found[0]);
}

juce::File scannerFile()
{
    const auto scanner = hosting::PluginScanner::scannerExecutable();
    INFO("scanner: " << scanner.getFullPathName());
    return scanner;
}

std::atomic<int> violations{0};
void countViolation(const rt::Violation&) noexcept
{
    violations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("The scanner process describes an installed plugin, and a non-plugin yields nothing",
          "[hosting][scan]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto scanner = scannerFile();
    REQUIRE(scanner.existsAsFile());

    // A text file is not a plugin: a clean empty result, not a failure.
    const auto text =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("aural-not-a-plugin.txt");
    REQUIRE(text.replaceWithText("hello"));
    std::vector<hosting::PluginInfo> found;
    std::string error;
    CHECK(hosting::PluginScanner::inspect(scanner, text, found, error));
    CHECK(found.empty());

    const auto plugin = anyInstalledVst3();
    if (plugin == juce::File())
    {
        WARN("no VST3 plugin installed; skipping the real scan");
        return;
    }
    REQUIRE(hosting::PluginScanner::inspect(scanner, plugin, found, error));
    INFO(error);
    REQUIRE_FALSE(found.empty());
    CHECK_FALSE(found[0].name.empty());
    CHECK_FALSE(found[0].id.empty());
    CHECK(found[0].path == plugin.getFullPathName().toStdString());
}

TEST_CASE("A hosted plugin loads, processes a block in place and round-trips its state", "[hosting][plugin]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto plugin = anyInstalledVst3();
    if (plugin == juce::File() || !scannerFile().existsAsFile())
    {
        WARN("no VST3 plugin installed; skipping the hosting test");
        return;
    }
    std::vector<hosting::PluginInfo> found;
    std::string error;
    REQUIRE(hosting::PluginScanner::inspect(scannerFile(), plugin, found, error));
    REQUIRE_FALSE(found.empty());
    hosting::PluginRegistry registry;
    registry.setFileResults(plugin.getFullPathName().toStdString(), 1, found);
    const auto& info = registry.plugins()[0];
    INFO("plugin: " << info.name << " (" << info.manufacturer << ")");

    hosting::HostFactory factory(registry);
    CHECK(factory.displayName(hosting::PluginRegistry::typeFor(info)) == info.name);
    CHECK(factory.displayName("vst3:unknown") == "Missing plugin");
    CHECK(factory.displayName("aural.eq") == plugins::BuiltInFactory::displayName("aural.eq"));
    CHECK(factory.create("vst3:unknown") == nullptr);
    CHECK(factory.create("aural.gain") != nullptr);

    auto processor = factory.create(hosting::PluginRegistry::typeFor(info));
    INFO(factory.lastError());
    REQUIRE(processor != nullptr);
    auto* hosted = dynamic_cast<hosting::HostedPlugin*>(processor.get());
    REQUIRE(hosted != nullptr);
    CHECK(processor->parameters().empty()); // the plugin owns them
    processor->prepare(48000.0, 256);
    CHECK(processor->latencySamples() >= 0);

    // A sine through the plugin at its defaults: the output must be finite and the
    // plugin must have written something (an effect at defaults passes audio).
    std::vector<float> l(256), r(256);
    for (int i = 0; i < 256; ++i)
        l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
            0.25f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 * i / 48000.0));
    const auto previous = rt::setViolationHandler(&countViolation);
    violations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int block = 0; block < 20; ++block)
            processor->process(l.data(), r.data(), 256);
    }
    rt::setViolationHandler(previous);
    INFO("real-time violations inside the plugin: " << violations.load());
    float energy = 0.0f;
    for (int i = 0; i < 256; ++i)
    {
        REQUIRE(std::isfinite(l[static_cast<size_t>(i)]));
        energy += l[static_cast<size_t>(i)] * l[static_cast<size_t>(i)];
    }
    CHECK(energy > 0.0f);

    // Bypassed: the input passes untouched.
    std::fill(l.begin(), l.end(), 0.5f);
    processor->setBypassed(true);
    processor->process(l.data(), r.data(), 256);
    CHECK(l[100] == 0.5f);
    processor->setBypassed(false);

    const auto state = processor->state();
    CHECK_FALSE(state.empty());
    auto second = factory.create(hosting::PluginRegistry::typeFor(info));
    REQUIRE(second != nullptr);
    second->prepare(48000.0, 256);
    second->setState(state);
    CHECK(second->state().size() == state.size());
    CHECK(hosted->instance().getPluginDescription().name.toStdString() == info.name);
}

TEST_CASE("A hosted plugin lists its programs and round-trips a .vstpreset file",
          "[hosting][plugin][presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto plugin = anyInstalledVst3();
    if (plugin == juce::File() || !scannerFile().existsAsFile())
    {
        WARN("no VST3 plugin installed; skipping the preset test");
        return;
    }
    std::vector<hosting::PluginInfo> found;
    std::string error;
    REQUIRE(hosting::PluginScanner::inspect(scannerFile(), plugin, found, error));
    REQUIRE_FALSE(found.empty());
    hosting::PluginRegistry registry;
    registry.setFileResults(plugin.getFullPathName().toStdString(), 1, found);
    hosting::HostFactory factory(registry);
    auto processor = factory.create(hosting::PluginRegistry::typeFor(registry.plugins()[0]));
    REQUIRE(processor != nullptr);
    auto* hosted = dynamic_cast<hosting::HostedPlugin*>(processor.get());
    REQUIRE(hosted != nullptr);
    hosted->prepare(48000.0, 256);

    const auto programs = hosted->factoryPrograms();
    INFO("programs: " << programs.size());
    if (!programs.empty())
    {
        hosted->setProgram(static_cast<int>(programs.size()) - 1);
        CHECK(hosted->currentProgram() == static_cast<int>(programs.size()) - 1);
    }
    REQUIRE(hosted->supportsPresetFiles());
    CHECK(hosted->userPresetFolder().getFullPathName().contains("VST3 Presets"));

    const auto file =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("aural-test.vstpreset");
    file.deleteFile();
    REQUIRE(hosted->savePresetFile(file));
    CHECK(file.getSize() > 0);
    CHECK(hosted->loadPresetFile(file));
    CHECK_FALSE(hosted->loadPresetFile(
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("aural-missing.vstpreset")));
    file.deleteFile();
}
