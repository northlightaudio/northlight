#include "app/Bounce.h"
#include "app/DemoSong.h"
#include "engine/WavFile.h"
#include "hosting/HostFactory.h"
#include "hosting/PluginRegistry.h"
#include "plugins/Aurora.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include <vector>

using namespace northlight;
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

TEST_CASE("A bounce destroyed before its completion is delivered reports nothing", "[app][bounce]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto a = constantWav("bounce-gone.wav", 0.5f, 0.5);
    core::Project p;
    core::Track t;
    t.id = 1;
    t.clips.push_back({10, "a", a.getFullPathName().toStdString(), 0, 24000, 0, 1.0f});
    p.tracks = {t};
    p.nextId = 100;
    hosting::PluginRegistry registry;
    hosting::HostFactory factory(registry);
    const auto out =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bounce-gone-mix.wav");
    BounceJob::Options options;
    options.output = out;
    bool reported = false;
    {
        auto job = std::make_unique<BounceJob>(p, factory, options);
        job->onFinished = [&reported](const BounceJob::Result&) { reported = true; };
        job->start();
        while (job->isRunning())
            juce::Thread::sleep(5);
        // The completion is queued now; the job goes before it is delivered (a new
        // bounce replacing it does this).
    }
#if defined(_WIN32)
    // Deliver what the job queued (the app's message loop would).
    for (int i = 0; i < 10; ++i)
    {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        juce::Thread::sleep(5);
    }
    CHECK_FALSE(reported);
#else
    SKIP("delivers the message queue with Win32 calls");
#endif
    out.deleteFile();
    a.deleteFile();
}

TEST_CASE("Bouncing a MIDI track renders its instrument", "[app][bounce][midi]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    core::Project p;
    core::Track keys;
    keys.id = 1;
    keys.name = "Keys";
    keys.kind = core::TrackKind::instrument; // the channel; "notes" below plays it
    keys.instrument.id = 11;
    keys.instrument.type = plugins::aurora::type;
    keys.instrument.params = plugins::defaultParameters(keys.instrument.type);
    // One centred voice per note, no chorus and a short release: the checks
    // below want the same signal on both sides and silence soon after the note.
    namespace au = plugins::aurora;
    keys.instrument.params[au::unison] = 1.0f;
    keys.instrument.params[au::width] = 0.0f;
    keys.instrument.params[au::shimmer] = 0.0f;
    keys.instrument.params[au::ampRelease] = 30.0f;
    core::MidiClip clip;
    clip.id = 12;
    clip.name = "note";
    clip.start = 0;
    clip.length = 48000;
    clip.notes.push_back({13, 69, 110, 24000, 12000}); // A4 from 0.5 s for 0.25 s
    core::Track notes;
    notes.id = 2;
    notes.kind = core::TrackKind::midi;
    notes.midiTarget = keys.id;
    notes.midiClips.push_back(clip);
    p.tracks = {notes, keys};
    p.nextId = 100;

    hosting::PluginRegistry registry;
    hosting::HostFactory factory(registry);
    const auto out =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bounce-midi.wav");
    out.deleteFile();
    BounceJob::Options options;
    options.output = out;
    BounceJob job(p, factory, options);
    const auto result = job.runSynchronously();
    INFO(result.error);
    REQUIRE(result.error.empty());
    CHECK(result.length == 48000);
    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(out.getFullPathName().toStdString(), channels));
    REQUIRE(channels.size() == 2);
    REQUIRE(channels[0].size() == 48000);
    const auto peakIn = [&](size_t from, size_t to)
    {
        float peak = 0.0f;
        for (size_t i = from; i < to; ++i)
            peak = std::max(peak, std::abs(channels[0][i]));
        return peak;
    };
    CHECK(peakIn(0, 23990) == 0.0f);                         // silence before the note
    CHECK(peakIn(25000, 35000) > 0.05f);                     // the note sounds
    CHECK(peakIn(47000, 48000) < 1.0e-3f);                   // and has released by the end
    CHECK(channels[1][30000] == Approx(channels[0][30000])); // the synth is mono, centred
    out.deleteFile();
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
    const auto folder =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("northlight-stems");
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

TEST_CASE("The demo song bounces to a full, unclipped mix with every section sounding", "[app][bounce][demo]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto song = makeDemoSong();
    REQUIRE(song.tracks.size() >= 6);
    REQUIRE(song.buses.size() == 2);

    hosting::PluginRegistry registry;
    hosting::HostFactory factory(registry);
    // NORTHLIGHT_DEMO_BOUNCE names where to keep the mix (to listen to it).
    const auto kept = juce::SystemStats::getEnvironmentVariable("NORTHLIGHT_DEMO_BOUNCE", {});
    const auto out =
        kept.isNotEmpty()
            ? juce::File(kept)
            : juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getNonexistentChildFile("bounce-demo", ".wav", false); // parallel runs must not share it
    out.deleteFile();
    BounceJob::Options options;
    options.output = out;
    BounceJob job(song, factory, options);
    const auto result = job.runSynchronously();
    INFO(result.error);
    REQUIRE(result.error.empty());
    CHECK(result.length >= 32 * 4 * 24000);
    CHECK(result.integratedLufs > -24.0f);
    CHECK(result.integratedLufs < -8.0f);
    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(out.getFullPathName().toStdString(), channels));
    REQUIRE(channels.size() == 2);
    float peak = 0.0f;
    for (const auto& ch : channels)
        for (float v : ch)
            peak = std::max(peak, std::abs(v));
    CHECK(peak < 1.0f);
    // Each section (intro, verse, chorus, breakdown, finale) is audible.
    const auto rmsOfBars = [&](int from, int to)
    {
        double sum = 0.0;
        const auto a = static_cast<size_t>(from) * 96000,
                   b = std::min(channels[0].size(), static_cast<size_t>(to) * 96000);
        for (size_t i = a; i < b; ++i)
            sum += channels[0][i] * channels[0][i];
        return std::sqrt(sum / std::max<double>(1.0, static_cast<double>(b - a)));
    };
    for (const auto& [from, to] :
         {std::pair{1, 4}, std::pair{4, 12}, std::pair{12, 20}, std::pair{20, 24}, std::pair{24, 32}})
    {
        INFO("bars " << from << " to " << to);
        CHECK(rmsOfBars(from, to) > 0.01);
    }
    // The choruses are louder than the intro.
    CHECK(rmsOfBars(12, 20) > rmsOfBars(1, 4));
    if (kept.isEmpty())
        out.deleteFile();
}

TEST_CASE("A cancelled bounce reports it and leaves no file", "[app][bounce]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto a = constantWav("bounce-cancel.wav", 0.5f, 1.0);
    core::Project p;
    core::Track t;
    t.id = 1;
    t.name = "A";
    t.clips.push_back({10, "a", a.getFullPathName().toStdString(), 0, 48000, 0, 1.0f});
    p.tracks = {t};
    p.nextId = 100;

    hosting::PluginRegistry registry;
    hosting::HostFactory factory(registry);
    const auto out =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("bounce-cancel-out.wav");
    out.deleteFile();
    BounceJob::Options options;
    options.output = out;
    options.format.sampleRate = 44100; // through the conversion too
    BounceJob job(p, factory, options);
    job.cancel();
    const auto result = job.runSynchronously();
    CHECK(result.cancelled);
    CHECK(result.error.empty());
    CHECK(result.files.empty());
    CHECK_FALSE(out.existsAsFile());
}
