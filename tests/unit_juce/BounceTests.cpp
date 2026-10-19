#include "app/Bounce.h"
#include "engine/WavFile.h"
#include "hosting/HostFactory.h"
#include "hosting/PluginRegistry.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace aural;
using Catch::Approx;

namespace
{
// A mono WAV of `seconds` at 48 kHz holding a constant value.
juce::File constantWav(const char* name, float value, double seconds)
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
    file.deleteFile();
    engine::WavWriter writer;
    REQUIRE(writer.open(file.getFullPathName().toStdString(), 1, 48000.0));
    std::vector<float> chunk(4800, value);
    for (int i = 0; i < static_cast<int>(seconds * 10); ++i)
        REQUIRE(writer.writeInterleaved(chunk.data(), 4800));
    REQUIRE(writer.close());
    return file;
}

const float centre = std::cos(3.14159265f / 4.0f);
} // namespace

TEST_CASE("Bouncing the mix renders the range to a stereo float WAV, compensated for insert latency",
          "[app][bounce]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto a = constantWav("bounce-a.wav", 0.5f, 2.0);
    const auto b = constantWav("bounce-b.wav", 0.25f, 2.0);
    core::Project p;
    core::Track ta;
    ta.id = 1;
    ta.name = "A";
    ta.clips.push_back({10, "a", a.getFullPathName().toStdString(), 0, 96000, 0, 1.0f});
    core::Insert limiter;
    limiter.id = 11;
    limiter.type = plugins::types::limiter;
    limiter.params = plugins::defaultParameters(plugins::types::limiter); // 1.5 ms lookahead: latency 72
    ta.inserts.push_back(limiter);
    core::Track tb;
    tb.id = 2;
    tb.name = "B";
    tb.clips.push_back({20, "b", b.getFullPathName().toStdString(), 48000, 48000, 0, 1.0f});
    p.tracks = {ta, tb};
    p.nextId = 100;

    hosting::PluginRegistry registry;
    hosting::HostFactory factory(registry);
    const auto out = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bounce-mix.wav");
    out.deleteFile();
    BounceJob::Options options;
    options.output = out;
    BounceJob job(p, factory, options);
    const auto result = job.runSynchronously();
    INFO(result.error);
    REQUIRE(result.error.empty());
    REQUIRE(result.files.size() == 1);
    CHECK(result.length == 96000);
    const auto info = engine::readWavInfo(out.getFullPathName().toStdString());
    REQUIRE(info.has_value());
    CHECK(info->numChannels == 2);
    CHECK(info->lengthSamples == 96000);
    CHECK(info->sampleRate == 48000.0);
    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(out.getFullPathName().toStdString(), channels));
    // The latency was dropped from the front: sample 0 already carries track A.
    CHECK(channels[0][0] == Approx(0.5f * centre).margin(0.01f));
    CHECK(channels[0][24000] == Approx(0.5f * centre).margin(0.01f));
    CHECK(channels[0][72000] == Approx((0.5f + 0.25f) * centre).margin(0.01f)); // B joins at 1 s
    CHECK(channels[1][72000] == Approx((0.5f + 0.25f) * centre).margin(0.01f));
    // A constant is mostly removed by the K-weighting's high-pass, so the figure is
    // low; what matters is that the mix was measured at all.
    CHECK(result.integratedLufs > -70.0f);
    CHECK(result.integratedLufs < -10.0f);

    // A range: one second from 0.5 s.
    out.deleteFile();
    options.start = 24000;
    options.end = 72000;
    BounceJob ranged(p, factory, options);
    REQUIRE(ranged.runSynchronously().error.empty());
    REQUIRE(engine::readWavSamples(out.getFullPathName().toStdString(), channels));
    CHECK(channels[0].size() == 48000);
    CHECK(channels[0][0] == Approx(0.5f * centre).margin(0.01f));
    CHECK(channels[0][30000] == Approx((0.5f + 0.25f) * centre).margin(0.01f));
    out.deleteFile();
    a.deleteFile();
    b.deleteFile();
}

TEST_CASE("Bouncing stems writes one file per track with the others muted", "[app][bounce]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto a = constantWav("stem-a.wav", 0.5f, 1.0);
    const auto b = constantWav("stem-b.wav", 0.25f, 1.0);
    core::Project p;
    core::Track ta, tb;
    ta.id = 1;
    ta.name = "Drums";
    ta.clips.push_back({10, "a", a.getFullPathName().toStdString(), 0, 48000, 0, 1.0f});
    tb.id = 2;
    tb.name = "Bass";
    tb.clips.push_back({20, "b", b.getFullPathName().toStdString(), 0, 48000, 0, 1.0f});
    p.tracks = {ta, tb};
    p.nextId = 100;
    hosting::PluginRegistry registry;
    hosting::HostFactory factory(registry);
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("aural-stems");
    folder.deleteRecursively();
    BounceJob::Options options;
    options.output = folder;
    options.stems = true;
    BounceJob job(p, factory, options);
    const auto result = job.runSynchronously();
    REQUIRE(result.error.empty());
    REQUIRE(result.files.size() == 2);
    CHECK(result.files[0].getFileName() == "01 Drums.wav");
    CHECK(result.files[1].getFileName() == "02 Bass.wav");
    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(result.files[0].getFullPathName().toStdString(), channels));
    CHECK(channels[0][1000] == Approx(0.5f * centre).margin(0.01f)); // drums only
    REQUIRE(engine::readWavSamples(result.files[1].getFullPathName().toStdString(), channels));
    CHECK(channels[0][1000] == Approx(0.25f * centre).margin(0.01f)); // bass only
    folder.deleteRecursively();
    a.deleteFile();
    b.deleteFile();
}
