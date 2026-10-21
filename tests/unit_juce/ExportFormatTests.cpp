#include "app/Bounce.h"
#include "engine/ExportDsp.h"
#include "engine/WavFile.h"
#include "hosting/HostFactory.h"
#include "hosting/PluginRegistry.h"
#include "ui/ExportView.h"
#include "ui/Theme.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <numbers>
#include <vector>

using namespace northlight;
using Catch::Approx;

namespace
{
juce::File tempFile(const char* name)
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name);
}

// A mono 48 kHz WAV of a 997 Hz sine.
juce::File sineWav(const char* name, float amplitude, double seconds)
{
    const auto file = tempFile(name);
    file.deleteFile();
    engine::WavWriter writer;
    REQUIRE(writer.open(file.getFullPathName().toStdString(), 1, 48000.0));
    const int n = static_cast<int>(seconds * 48000.0);
    std::vector<float> samples(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        samples[static_cast<size_t>(i)] =
            amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * 997.0 * i / 48000.0));
    REQUIRE(writer.writeInterleaved(samples.data(), n));
    REQUIRE(writer.close());
    return file;
}

core::Project twoTracks(const juce::File& a, const juce::File& b, core::SamplePosition length)
{
    core::Project p;
    core::Track ta, tb;
    ta.id = 1;
    ta.name = "Drums";
    ta.clips.push_back({10, "a", a.getFullPathName().toStdString(), 0, length, 0, 1.0f});
    tb.id = 2;
    tb.name = "Bass";
    tb.clips.push_back({20, "b", b.getFullPathName().toStdString(), 0, length, 0, 1.0f});
    p.tracks = {ta, tb};
    p.nextId = 100;
    return p;
}

struct ReadBack
{
    double rate = 0.0;
    int bits = 0;
    juce::int64 length = 0;
    std::vector<std::vector<float>> channels;
};

ReadBack readBack(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    ReadBack r;
    r.rate = reader->sampleRate;
    r.bits = static_cast<int>(reader->bitsPerSample);
    r.length = reader->lengthInSamples;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), static_cast<int>(r.length));
    REQUIRE(reader->read(&buffer, 0, static_cast<int>(r.length), 0, true, true));
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        r.channels.emplace_back(buffer.getReadPointer(c), buffer.getReadPointer(c) + buffer.getNumSamples());
    return r;
}
} // namespace

TEST_CASE("A normalised 16-bit export at another rate reads back at the target loudness",
          "[app][bounce][export]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto a = sineWav("export-a.wav", 0.05f, 4.0);
    const auto b = sineWav("export-b.wav", 0.02f, 4.0);
    const auto project = twoTracks(a, b, 4 * 48000);
    hosting::PluginRegistry registry;
    hosting::HostFactory factory(registry);
    const auto out = tempFile("export-normalised.wav");
    out.deleteFile();
    BounceJob::Options options;
    options.output = out;
    options.format.format = engine::ExportFormat::wav;
    options.format.depth = engine::ExportDepth::int16;
    options.format.dither = engine::DitherMode::tpdf;
    options.format.sampleRate = 44100;
    options.format.normalise = true;
    options.format.targetLufs = -16.0;
    options.format.ceilingDb = -1.0;
    BounceJob job(project, factory, options);
    const auto result = job.runSynchronously();
    INFO(result.error);
    REQUIRE(result.error.empty());
    CHECK(result.length == 4 * 44100);
    CHECK(result.integratedLufs < -20.0f);
    CHECK(result.gainDb == Approx(-16.0 - result.integratedLufs).margin(0.01));
    CHECK_FALSE(result.limitedByCeiling);
    CHECK(result.truePeak);

    const auto file = readBack(out);
    CHECK(file.rate == 44100.0);
    CHECK(file.bits == 16);
    CHECK(file.length == 4 * 44100);
    CHECK(engine::integratedLoudness(file.channels, 44100.0) == Approx(-16.0).margin(0.15));
    // The toast's sentence carries the measurement and the gain.
    const auto text = BounceJob::describe(result, options, 44100.0);
    CHECK(text.find("LUFS") != std::string::npos);
    CHECK(text.find("dB gain to -16.0 LUFS") != std::string::npos);
    out.deleteFile();
    a.deleteFile();
    b.deleteFile();
}

TEST_CASE("Exports write AIFF, FLAC and Ogg Vorbis at the chosen depth", "[app][bounce][export]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto a = sineWav("export-fa.wav", 0.25f, 1.0);
    const auto b = sineWav("export-fb.wav", 0.1f, 1.0);
    const auto project = twoTracks(a, b, 48000);
    hosting::PluginRegistry registry;
    hosting::HostFactory factory(registry);
    struct Case
    {
        engine::ExportFormat format;
        engine::ExportDepth depth;
        const char* name;
        int bits;
    };
    for (const auto& c : {Case{engine::ExportFormat::aiff, engine::ExportDepth::int16, "export.aiff", 16},
                          Case{engine::ExportFormat::flac, engine::ExportDepth::int24, "export.flac", 24},
                          Case{engine::ExportFormat::wav, engine::ExportDepth::int24, "export24.wav", 24},
                          Case{engine::ExportFormat::ogg, engine::ExportDepth::int24, "export.ogg", 0}})
    {
        INFO(c.name);
        const auto out = tempFile(c.name);
        out.deleteFile();
        BounceJob::Options options;
        options.output = out;
        options.format.format = c.format;
        options.format.depth = c.depth;
        options.format.dither = engine::DitherMode::shaped;
        BounceJob job(project, factory, options);
        const auto result = job.runSynchronously();
        INFO(result.error);
        REQUIRE(result.error.empty());
        const auto file = readBack(out);
        CHECK(file.rate == 48000.0);
        REQUIRE(file.channels.size() == 2);
        if (c.bits > 0)
        {
            CHECK(file.bits == c.bits);
            CHECK(file.length == 48000);
        }
        // The mix of both tracks at the centre pan law, within the dither and codec error.
        const float expected = (0.25f + 0.1f) * std::cos(std::numbers::pi_v<float> / 4.0f);
        const auto peak = engine::samplePeak(file.channels[0].data() + 4800, 38400);
        CHECK(peak == Approx(expected).margin(c.format == engine::ExportFormat::ogg ? 0.02f : 0.002f));
        out.deleteFile();
    }
    a.deleteFile();
    b.deleteFile();
}

TEST_CASE("Normalised stems share the mix's gain, and a selection limits the stems", "[app][bounce][export]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto a = sineWav("export-sa.wav", 0.05f, 2.0);
    const auto b = sineWav("export-sb.wav", 0.02f, 2.0);
    const auto project = twoTracks(a, b, 2 * 48000);
    hosting::PluginRegistry registry;
    hosting::HostFactory factory(registry);
    const auto folder = tempFile("northlight-export-stems");
    folder.deleteRecursively();
    BounceJob::Options options;
    options.output = folder;
    options.stems = true;
    options.format.format = engine::ExportFormat::flac;
    options.format.depth = engine::ExportDepth::int24;
    options.format.normalise = true;
    options.format.targetLufs = -14.0;
    BounceJob job(project, factory, options);
    const auto result = job.runSynchronously();
    INFO(result.error);
    REQUIRE(result.error.empty());
    REQUIRE(result.files.size() == 2);
    CHECK(result.files[0].getFileName() == "01 Drums.flac");
    CHECK(result.files[1].getFileName() == "02 Bass.flac");
    const auto gain = static_cast<float>(engine::dbToGain(result.gainDb));
    CHECK(gain > 1.0f);
    const float centre = std::cos(std::numbers::pi_v<float> / 4.0f);
    const auto drums = readBack(result.files[0]);
    const auto bass = readBack(result.files[1]);
    CHECK(engine::samplePeak(drums.channels[0].data() + 4800, 48000) ==
          Approx(0.05f * centre * gain).margin(0.001f));
    CHECK(engine::samplePeak(bass.channels[0].data() + 4800, 48000) ==
          Approx(0.02f * centre * gain).margin(0.001f));
    // Together the stems are the mix at the target.
    std::vector<std::vector<float>> sum = drums.channels;
    for (size_t c = 0; c < sum.size(); ++c)
        for (size_t i = 0; i < sum[c].size(); ++i)
            sum[c][i] += bass.channels[c][i];
    CHECK(engine::integratedLoudness(sum, 48000.0) == Approx(-14.0).margin(0.15));

    // Only the selected track.
    folder.deleteRecursively();
    options.format.normalise = false;
    options.stemTracks = {2};
    BounceJob selected(project, factory, options);
    const auto one = selected.runSynchronously();
    REQUIRE(one.error.empty());
    REQUIRE(one.files.size() == 1);
    CHECK(one.files[0].getFileName() == "02 Bass.flac");
    folder.deleteRecursively();
    a.deleteFile();
    b.deleteFile();
}

TEST_CASE("The export sheet hands back the settings it was given, made consistent", "[ui][export]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    engine::ExportSettings s;
    s.source = engine::ExportSource::stems;
    s.format = engine::ExportFormat::flac;
    s.depth = engine::ExportDepth::int16;
    s.sampleRate = 96000;
    s.dither = engine::DitherMode::shaped;
    s.normalise = true;
    s.targetLufs = -23.0;
    s.ceilingDb = -2.0;
    s.peakMode = engine::PeakMode::samplePeak;
    ui::ExportView::Context context;
    context.oggQualities = juce::OggVorbisAudioFormat().getQualityOptions();
    ui::ExportView view(ui::Theme::dark(), s, context);
    CHECK(view.settings() == s);

    // A loop range without a loop falls back to the whole project; float FLAC to 24-bit.
    s.range = engine::ExportRange::loop;
    s.depth = engine::ExportDepth::float32;
    ui::ExportView other(ui::Theme::dark(), s, context);
    CHECK(other.settings().range == engine::ExportRange::project);
    CHECK(other.settings().depth == engine::ExportDepth::int24);
}
