#include "audio/JuceStreamReader.h"
#include "engine/WavFile.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <numbers>
#include <vector>

using namespace aural;

namespace
{
std::string tempPath(const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / "aural-tests";
    std::filesystem::create_directories(dir);
    const auto path = (dir / name).string();
    std::remove(path.c_str());
    return path;
}

// Writes a 1 kHz sine at `rate` for `seconds`.
std::string writeSine(const char* name, double rate, double seconds)
{
    const auto path = tempPath(name);
    const auto n = static_cast<int>(rate * seconds);
    std::vector<float> samples(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        samples[static_cast<size_t>(i)] =
            0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * i / rate));
    engine::WavWriter writer;
    REQUIRE(writer.open(path, 1, rate));
    const float* channels[] = {samples.data()};
    REQUIRE(writer.write(channels, n));
    REQUIRE(writer.close());
    return path;
}

// Correlation of `data` with a 1 kHz sine at `rate` starting at target sample `start`.
double correlation(const std::vector<float>& data, double rate, std::int64_t start)
{
    double acc = 0.0, energy = 0.0;
    for (size_t i = 0; i < data.size(); ++i)
    {
        const double expected =
            0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 *
                           static_cast<double>(start + static_cast<std::int64_t>(i)) / rate);
        acc += data[i] * expected;
        energy += expected * expected;
    }
    return energy > 0.0 ? acc / energy : 0.0;
}

juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager manager;
    static bool registered = false;
    if (!registered)
    {
        manager.registerBasicFormats();
        registered = true;
    }
    return manager;
}
} // namespace

TEST_CASE("JuceStreamReader resamples 44.1 kHz to 48 kHz sequentially and after a seek", "[audio][stream]")
{
    const auto path = writeSine("sine441.wav", 44100.0, 1.0);
    std::unique_ptr<juce::AudioFormatReader> reader(
        formats().createReaderFor(juce::File{juce::String(path)}));
    REQUIRE(reader != nullptr);
    audio::JuceStreamReader stream(std::move(reader), 48000.0);
    CHECK(stream.sampleRate() == 48000.0);
    CHECK(stream.numChannels() == 1);
    CHECK(stream.lengthSamples() == 48000);

    // Sequential chunks: the resampled signal is a 1 kHz sine at 48 kHz.
    std::vector<float> chunk(1024);
    float* dest[] = {chunk.data()};
    for (std::int64_t start = 1024; start < 20000; start += 1024)
    {
        REQUIRE(stream.read(start, 1024, dest));
        CHECK(correlation(chunk, 48000.0, start) > 0.999);
    }

    // A seek backwards, then forwards, each still aligned to the timeline.
    REQUIRE(stream.read(5000, 1024, dest));
    CHECK(correlation(chunk, 48000.0, 5000) > 0.999);
    REQUIRE(stream.read(30000, 1024, dest));
    CHECK(correlation(chunk, 48000.0, 30000) > 0.999);

    // Beyond the end reads as silence.
    REQUIRE(stream.read(47900, 1024, dest));
    float tail = 0.0f;
    for (size_t i = 200; i < chunk.size(); ++i)
        tail = std::max(tail, std::abs(chunk[i]));
    CHECK(tail < 0.05f);
}

TEST_CASE("JuceStreamReader passes samples through unchanged at the file's own rate", "[audio][stream]")
{
    const auto path = writeSine("sine48.wav", 48000.0, 0.5);
    std::unique_ptr<juce::AudioFormatReader> reader(
        formats().createReaderFor(juce::File{juce::String(path)}));
    REQUIRE(reader != nullptr);
    audio::JuceStreamReader stream(std::move(reader), 48000.0);
    CHECK(stream.lengthSamples() == 24000);
    std::vector<float> chunk(480);
    float* dest[] = {chunk.data()};
    REQUIRE(stream.read(960, 480, dest));
    CHECK(correlation(chunk, 48000.0, 960) > 0.999);
}
