#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/AudioSource.h"
#include "engine/Recording.h"
#include "engine/WavFile.h"
#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

namespace
{
const northlight::plugins::BuiltInFactory builtIns;
} // namespace

using namespace northlight;
using Catch::Approx;

namespace
{
std::string tempPath(const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / "northlight-tests";
    std::filesystem::create_directories(dir);
    const auto path = (dir / name).string();
    std::remove(path.c_str());
    return path;
}

class NoMediaLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double, std::string& error) override
    {
        error = "no media in this test";
        return nullptr;
    }
};

void waitForWriter(engine::AudioEngine& engine)
{
    for (int i = 0; i < 2000 && !engine.hasRecordingToCollect(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
} // namespace

TEST_CASE("WAV files round-trip float samples and report their header", "[engine][wav]")
{
    const auto path = tempPath("roundtrip.wav");
    std::vector<float> a(1000), b(1000);
    for (int i = 0; i < 1000; ++i)
    {
        a[static_cast<size_t>(i)] = static_cast<float>(i) / 1000.0f;
        b[static_cast<size_t>(i)] = -a[static_cast<size_t>(i)];
    }
    {
        engine::WavWriter writer;
        REQUIRE(writer.open(path, 2, 48000.0));
        const float* channels[] = {a.data(), b.data()};
        REQUIRE(writer.write(channels, 600));
        REQUIRE(writer.write(channels + 0, 0));
        const float* rest[] = {a.data() + 600, b.data() + 600};
        REQUIRE(writer.write(rest, 400));
        CHECK(writer.samplesWritten() == 1000);
        REQUIRE(writer.close());
    }
    const auto info = engine::readWavInfo(path);
    REQUIRE(info.has_value());
    CHECK(info->numChannels == 2);
    CHECK(info->sampleRate == 48000.0);
    CHECK(info->lengthSamples == 1000);
    CHECK(info->isFloat);

    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(path, channels));
    REQUIRE(channels.size() == 2);
    CHECK(channels[0] == a);
    CHECK(channels[1] == b);
}

TEST_CASE("A 24-bit recording format writes PCM that reads back within one step", "[engine][wav]")
{
    const auto path = tempPath("pcm24.wav");
    std::vector<float> a(1000);
    for (int i = 0; i < 1000; ++i)
        a[static_cast<size_t>(i)] = static_cast<float>(i - 500) / 400.0f; // past full scale at both ends
    {
        engine::WavWriter writer;
        CHECK_FALSE(writer.open(path, 1, 48000.0, 16)); // only float and 24-bit
        REQUIRE(writer.open(path, 1, 48000.0, 24));
        const float* channels[] = {a.data()};
        REQUIRE(writer.write(channels, 1000));
        REQUIRE(writer.close());
    }
    const auto info = engine::readWavInfo(path);
    REQUIRE(info.has_value());
    CHECK(info->bitsPerSample == 24);
    CHECK_FALSE(info->isFloat);
    CHECK(info->lengthSamples == 1000);
    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(path, channels));
    REQUIRE(channels.size() == 1);
    for (size_t i = 0; i < a.size(); ++i)
    {
        const float expected = std::clamp(a[i], -1.0f, 1.0f); // clipped at full scale
        CHECK(std::abs(channels[0][i] - expected) <= 3.0f / 8388608.0f);
    }
    // A repaired 24-bit file keeps whole three-byte frames.
    CHECK(engine::WavWriter::repair(path));
    CHECK(engine::readWavInfo(path)->lengthSamples == 1000);
}

TEST_CASE("A file left without a final header is repaired from its size", "[engine][wav]")
{
    const auto path = tempPath("crashed.wav");
    std::vector<float> samples(1500, 0.25f);
    {
        engine::WavWriter writer;
        REQUIRE(writer.open(path, 1, 44100.0));
        const float* channels[] = {samples.data()};
        REQUIRE(writer.write(channels, 1000));
        REQUIRE(writer.updateHeader()); // the periodic update: header says 1000
        // Simulate frames written after the last update, then a crash: append raw data.
        REQUIRE(writer.close());
    }
    {
        std::FILE* f = std::fopen(path.c_str(), "ab");
        REQUIRE(f != nullptr);
        std::fwrite(samples.data(), sizeof(float), 500, f);
        const char partial[2] = {0, 0}; // a torn frame at the very end
        std::fwrite(partial, 1, 2, f);
        std::fclose(f);
    }
    CHECK(engine::readWavInfo(path)->lengthSamples == 1000);
    REQUIRE(engine::WavWriter::repair(path));
    CHECK(engine::readWavInfo(path)->lengthSamples == 1500);
    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(path, channels));
    CHECK(channels[0].size() == 1500);
    CHECK(channels[0][1499] == 0.25f);
}

TEST_CASE("Record ring moves samples from producer to consumer in order and reports overruns",
          "[engine][record]")
{
    engine::RecordRing ring(2048);
    std::vector<float> in(1000);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<float>(i);
    CHECK(ring.push(in.data(), 1000));
    CHECK(ring.push(in.data(), 1000));
    CHECK_FALSE(ring.push(in.data(), 100)); // only 48 free
    CHECK(ring.available() == 2000);

    std::vector<float> out(1500);
    CHECK(ring.pop(out.data(), 1500) == 1500);
    CHECK(out[0] == 0.0f);
    CHECK(out[999] == 999.0f);
    CHECK(out[1000] == 0.0f);
    CHECK(out[1499] == 499.0f);
    CHECK(ring.push(in.data(), 1000)); // wraps around
    CHECK(ring.pop(out.data(), 1500) == 1500);
    CHECK(out[0] == 500.0f);
    CHECK(out[499] == 999.0f);
    CHECK(out[500] == 0.0f);
    CHECK(ring.pop(out.data(), 1500) == 0);
}

TEST_CASE("Recording captures armed inputs sample-exactly from the record position", "[engine][record]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    core::Project p;
    core::Track t;
    t.id = 1;
    p.tracks.push_back(t);
    engine.setProject(p);

    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    const auto pathA = tempPath("rec-a.wav");
    const auto pathB = tempPath("rec-b.wav");
    session->addInput(1, 0, pathA);
    session->addInput(1, 2, pathB); // input 2 is beyond what the device offers: silence

    std::vector<float> in0(64), in1(64);
    const float* inputs[] = {in0.data(), in1.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};

    engine.locate(1000);
    engine.process(inputs, 2, outputs, 2, 64); // applies the locate while stopped
    CHECK(engine.position() == 1000);

    std::string error;
    REQUIRE(engine.startRecording(std::move(session), error));
    CHECK(error.empty());

    // 100 blocks of a ramp on input 0; the record command applies in the first one.
    std::int64_t sample = 0;
    for (int block = 0; block < 100; ++block)
    {
        for (int i = 0; i < 64; ++i)
        {
            in0[static_cast<size_t>(i)] = static_cast<float>(sample + i) / 10000.0f;
            in1[static_cast<size_t>(i)] = 1.0f;
        }
        engine.process(inputs, 2, outputs, 2, 64);
        sample += 64;
    }
    CHECK(engine.isRecording());
    CHECK(engine.isPlaying());
    CHECK(engine.position() == 1000 + 6400);

    engine.stop();
    engine.process(inputs, 2, outputs, 2, 64);
    CHECK_FALSE(engine.isRecording());
    CHECK_FALSE(engine.isPlaying());

    waitForWriter(engine);
    auto finished = engine.collectRecording();
    REQUIRE(finished.has_value());
    CHECK(finished->session->startPosition() == 1000);
    CHECK(finished->session->totalDropped() == 0);
    REQUIRE(finished->results.size() == 2);
    CHECK(finished->results[0].trackId == 1);
    CHECK(finished->results[0].samplesWritten == 6400);
    CHECK(finished->results[0].ok);
    CHECK(finished->results[1].samplesWritten == 6400);

    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(pathA, channels));
    REQUIRE(channels[0].size() == 6400);
    for (int i = 0; i < 6400; ++i)
        REQUIRE(channels[0][static_cast<size_t>(i)] == static_cast<float>(i) / 10000.0f);
    REQUIRE(engine::readWavSamples(pathB, channels));
    CHECK(channels[0][100] == 0.0f);
    CHECK_FALSE(engine.hasRecordingToCollect());
}

TEST_CASE("The writer keeps live peaks of a take as it writes, handed out incrementally",
          "[engine][record][live]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    engine.setProject(core::Project{});
    engine.locate(1000);
    std::vector<float> in0(64);
    const float* inputs[] = {in0.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};
    engine.process(inputs, 1, outputs, 2, 64);

    std::vector<engine::LiveTake> live;
    engine.updateLiveTakes(live);
    CHECK(live.empty()); // nothing recording

    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    session->addInput(7, 0, tempPath("live.partial.wav"));
    std::string error;
    REQUIRE(engine.startRecording(std::move(session), error));

    // A ramp, one block at a time; the writer catches up within a few ms.
    const auto runBlocks = [&](int blocks, std::int64_t& sample)
    {
        for (int block = 0; block < blocks; ++block)
        {
            for (int i = 0; i < 64; ++i)
                in0[static_cast<size_t>(i)] = static_cast<float>(sample + i) / 10000.0f;
            engine.process(inputs, 1, outputs, 2, 64);
            sample += 64;
        }
    };
    const auto waitForWritten = [&](std::int64_t count)
    {
        for (int i = 0; i < 2000; ++i)
        {
            engine.updateLiveTakes(live);
            if (!live.empty() && live[0].samplesWritten >= count)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    std::int64_t sample = 0;
    runBlocks(50, sample); // 3200 samples: 12 full bins and a half one
    waitForWritten(3200);
    REQUIRE(live.size() == 1);
    CHECK(live[0].trackId == 7);
    CHECK(live[0].takeIndex == 0);
    CHECK(live[0].startPosition == 1000);
    CHECK(live[0].samplesWritten == 3200);
    REQUIRE(live[0].levels.size() == 2);
    CHECK(live[0].levels[0].samplesPerBin == 256);
    CHECK(live[0].levels[1].samplesPerBin == 256 * 64);
    REQUIRE(live[0].levels[0].minimum.size() == 12);
    CHECK(live[0].levels[0].minimum[0] == 0.0f);
    CHECK(live[0].levels[0].maximum[0] == Approx(255.0f / 10000.0f));
    CHECK(live[0].levels[0].minimum[11] == Approx(2816.0f / 10000.0f));
    CHECK(live[0].levels[1].minimum.empty()); // the coarse level needs 16384 samples

    // A lookup reads the written span and nothing past it.
    float minimum = 0.0f, maximum = 0.0f;
    live[0].lookup(256, 768, minimum, maximum);
    CHECK(minimum == Approx(256.0f / 10000.0f));
    CHECK(maximum == Approx(767.0f / 10000.0f));
    live[0].lookup(3000, 4000, minimum, maximum); // bin 11 and the unfinished 12th: just bin 11
    CHECK(maximum == Approx(3071.0f / 10000.0f));

    // More blocks: the same vector gains the new bins and keeps the old ones.
    const auto firstBins = live[0].levels[0].maximum;
    runBlocks(50, sample);
    waitForWritten(6400);
    CHECK(live[0].samplesWritten == 6400);
    REQUIRE(live[0].levels[0].maximum.size() == 25);
    for (size_t b = 0; b < firstBins.size(); ++b)
        CHECK(live[0].levels[0].maximum[b] == firstBins[b]);
    CHECK(live[0].levels[0].maximum[24] == Approx(6399.0f / 10000.0f));

    // A fresh vector gets everything.
    std::vector<engine::LiveTake> fresh;
    engine.updateLiveTakes(fresh);
    REQUIRE(fresh.size() == 1);
    CHECK(fresh[0].levels[0].maximum == live[0].levels[0].maximum);

    // Enough for the coarse level: 16384 samples = 256 blocks in all.
    runBlocks(156, sample);
    waitForWritten(16384);
    REQUIRE(live[0].levels[1].minimum.size() == 1);
    CHECK(live[0].levels[1].minimum[0] == 0.0f);
    CHECK(live[0].levels[1].maximum[0] == Approx(16383.0f / 10000.0f));
    live[0].lookup(0, 16384, minimum, maximum); // served by the coarse level
    CHECK(maximum == Approx(16383.0f / 10000.0f));

    engine.stop();
    engine.process(inputs, 1, outputs, 2, 64);
    waitForWriter(engine);
    engine.updateLiveTakes(live);
    CHECK(live.size() == 1); // still there until the recording is collected
    REQUIRE(engine.collectRecording().has_value());
    engine.updateLiveTakes(live);
    CHECK(live.empty());
}

TEST_CASE("Stopping the device mid-recording finishes the files", "[engine][record]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    engine.setProject(core::Project{});
    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    session->addInput(1, 0, tempPath("rec-c.wav"));
    std::string error;
    REQUIRE(engine.startRecording(std::move(session), error));

    std::vector<float> in0(64, 0.5f);
    const float* inputs[] = {in0.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};
    for (int block = 0; block < 10; ++block)
        engine.process(inputs, 1, outputs, 2, 64);

    engine.release(); // device stopped
    CHECK_FALSE(engine.isRecording());
    waitForWriter(engine);
    auto finished = engine.collectRecording();
    REQUIRE(finished.has_value());
    CHECK(finished->results[0].samplesWritten == 640);
}

TEST_CASE("Input monitoring follows the monitor mode and transport state", "[engine][monitor]")
{
    engine::TrackNode track;
    track.inputChannel = 0;
    track.armed = true;
    track.monitor = core::MonitorMode::automatic;
    CHECK(engine::Renderer::monitorsInput(track, false, false));
    CHECK_FALSE(engine::Renderer::monitorsInput(track, true, false)); // playback: hear the tape
    CHECK(engine::Renderer::monitorsInput(track, true, true));
    track.armed = false;
    CHECK_FALSE(engine::Renderer::monitorsInput(track, false, false));
    track.monitor = core::MonitorMode::on;
    CHECK(engine::Renderer::monitorsInput(track, true, false));
    track.monitor = core::MonitorMode::off;
    track.armed = true;
    CHECK_FALSE(engine::Renderer::monitorsInput(track, false, false));
    track.monitor = core::MonitorMode::on;
    track.inputChannel = -1;
    CHECK_FALSE(engine::Renderer::monitorsInput(track, false, false));
}

TEST_CASE("A monitored input is heard through the track gain and pan while stopped", "[engine][monitor]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    core::Project p;
    core::Track t;
    t.id = 1;
    t.inputChannel = 1;
    t.monitor = core::MonitorMode::on;
    t.gain = 0.5f;
    t.pan = -1.0f; // hard left
    p.tracks.push_back(t);
    engine.setProject(p);

    std::vector<float> in0(64, 1.0f), in1(64, 0.8f);
    const float* inputs[] = {in0.data(), in1.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};
    engine.process(inputs, 2, outputs, 2, 64);
    CHECK_FALSE(engine.isPlaying());
    CHECK(l[10] == Approx(0.4f).margin(1e-6f));
    CHECK(r[10] == Approx(0.0f).margin(1e-6f));
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> recordViolations{0};
void countRecordViolation(const rt::Violation&) noexcept
{
    recordViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Capturing and monitoring do not allocate on the real-time thread", "[engine][record][rt]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    engine.setInputBuses({{"Pair", engine::InputPair{0, 1}}});
    core::Project p;
    core::Track t;
    t.id = 1;
    t.inputChannel = 0;
    t.monitor = core::MonitorMode::on;
    p.tracks.push_back(t);
    core::Track stereo;
    stereo.id = 2;
    stereo.inputBus = "Pair";
    stereo.monitor = core::MonitorMode::on;
    p.tracks.push_back(stereo);
    engine.setProject(p);
    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    session->addInput(1, 0, tempPath("rec-rt.wav"));
    session->addStereoInput(2, 0, 1, tempPath("rec-rt-stereo.wav"));
    std::string error;
    REQUIRE(engine.startRecording(std::move(session), error));

    std::vector<float> in0(64, 0.1f), in1(64, 0.2f);
    const float* inputs[] = {in0.data(), in1.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};

    const auto previous = rt::setViolationHandler(&countRecordViolation);
    recordViolations = 0;
    {
        rt::ScopedRealtimeSection section;
        for (int block = 0; block < 200; ++block)
            engine.process(inputs, 2, outputs, 2, 64);
        engine.stop();
        engine.process(inputs, 2, outputs, 2, 64);
    }
    rt::setViolationHandler(previous);
    CHECK(recordViolations == 0);
    waitForWriter(engine);
    CHECK(engine.collectRecording().has_value());
}
#endif

TEST_CASE("The record ring interleaves stereo frames and keeps their order across the wrap",
          "[engine][record]")
{
    engine::RecordRing ring(1024);
    std::vector<float> left(300), right(300);
    for (size_t i = 0; i < left.size(); ++i)
    {
        left[i] = static_cast<float>(i);
        right[i] = -static_cast<float>(i);
    }
    const float* sides[] = {left.data(), right.data()};
    CHECK(ring.pushFrames(sides, 2, 300));
    std::vector<float> out(800);
    CHECK(ring.pop(out.data(), 400) == 400); // 200 frames
    CHECK(out[0] == 0.0f);
    CHECK(out[1] == -0.0f);
    CHECK(out[398] == 199.0f);
    CHECK(out[399] == -199.0f);
    CHECK(ring.pushFrames(sides, 2, 300));       // wraps around the end
    CHECK_FALSE(ring.pushFrames(sides, 2, 300)); // no room for a whole block: dropped whole
    CHECK(ring.pop(out.data(), 800) == 800);
    CHECK(out[0] == 200.0f);
    CHECK(out[1] == -200.0f);
    CHECK(out[200] == 0.0f); // the second block starts after the first's 100 remaining frames
    CHECK(out[201] == -0.0f);
    CHECK(out[798] == 299.0f);
    CHECK(out[799] == -299.0f);

    const float* silent[] = {left.data(), nullptr}; // a missing side records silence
    CHECK(ring.pushFrames(silent, 2, 2));
    CHECK(ring.pop(out.data(), 4) == 4);
    CHECK(out[2] == 1.0f);
    CHECK(out[3] == 0.0f);
}

TEST_CASE("A stereo input records both sides into one two-channel file", "[engine][record]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    core::Project p;
    core::Track t;
    t.id = 1;
    p.tracks.push_back(t);
    engine.setProject(p);

    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    const auto path = tempPath("rec-stereo.wav");
    session->addStereoInput(1, 1, 0, path); // left from input 1, right from input 0

    std::vector<float> in0(64), in1(64);
    const float* inputs[] = {in0.data(), in1.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};
    std::string error;
    REQUIRE(engine.startRecording(std::move(session), error));
    std::int64_t sample = 0;
    for (int block = 0; block < 100; ++block)
    {
        for (int i = 0; i < 64; ++i)
        {
            in0[static_cast<size_t>(i)] = static_cast<float>(sample + i) / 10000.0f;
            in1[static_cast<size_t>(i)] = -0.25f;
        }
        engine.process(inputs, 2, outputs, 2, 64);
        sample += 64;
    }
    engine.stop();
    engine.process(inputs, 2, outputs, 2, 64);

    waitForWriter(engine);
    auto finished = engine.collectRecording();
    REQUIRE(finished.has_value());
    REQUIRE(finished->results.size() == 1);
    CHECK(finished->results[0].ok);
    CHECK(finished->results[0].samplesWritten == 6400);
    CHECK(finished->session->totalDropped() == 0);

    std::vector<std::vector<float>> channels;
    engine::WavInfo info;
    REQUIRE(engine::readWavSamples(path, channels, &info));
    CHECK(info.numChannels == 2);
    REQUIRE(channels.size() == 2);
    REQUIRE(channels[0].size() == 6400);
    for (int i = 0; i < 6400; ++i)
    {
        REQUIRE(channels[0][static_cast<size_t>(i)] == -0.25f);
        REQUIRE(channels[1][static_cast<size_t>(i)] == static_cast<float>(i) / 10000.0f);
    }
}

TEST_CASE("A track on a stereo input bus monitors both sides through its balance", "[engine][monitor]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    engine.setInputBuses({{"Pair", engine::InputPair{1, 0}}});
    core::Project p;
    core::Track t;
    t.id = 1;
    t.inputBus = "Pair";
    t.monitor = core::MonitorMode::on;
    t.gain = 0.5f;
    p.tracks.push_back(t);
    engine.setProject(p);

    std::vector<float> in0(64, 1.0f), in1(64, 0.8f);
    const float* inputs[] = {in0.data(), in1.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};
    engine.process(inputs, 2, outputs, 2, 64);
    CHECK(l[10] == Approx(0.4f).margin(1e-6f)); // input 1 on the left
    CHECK(r[10] == Approx(0.5f).margin(1e-6f)); // input 0 on the right

    // Panned hard left, the balance keeps the left side and drops the right.
    p.tracks[0].pan = -1.0f;
    engine.setProject(p);
    for (int block = 0; block < 4; ++block) // past the pan ramp
        engine.process(inputs, 2, outputs, 2, 64);
    CHECK(l[10] == Approx(0.4f).margin(1e-6f));
    CHECK(r[10] == Approx(0.0f).margin(1e-6f));

    // A bus the device does not offer is no input at all.
    p.tracks[0].inputBus = "Elsewhere";
    engine.setProject(p);
    engine.process(inputs, 2, outputs, 2, 64);
    CHECK(l[10] == 0.0f);
    CHECK(r[10] == 0.0f);
}
