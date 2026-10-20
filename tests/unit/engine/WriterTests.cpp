#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/Recording.h"
#include "engine/WavFile.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

using namespace aural;

namespace
{
const aural::plugins::BuiltInFactory builtIns;

std::string tempPath(const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / "aural-tests";
    std::filesystem::create_directories(dir);
    const auto path = (dir / name).string();
    std::remove(path.c_str());
    std::remove((path.substr(0, path.size() - 12) + "-take2.partial.wav").c_str());
    std::remove((path.substr(0, path.size() - 12) + "-take3.partial.wav").c_str());
    return path;
}

void waitFor(engine::RecordWriter& writer)
{
    for (int i = 0; i < 2000 && writer.isWriting(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

class NoMediaLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double, std::string& error) override
    {
        error = "no media";
        return nullptr;
    }
};
} // namespace

TEST_CASE("A take mark followed at once by the next take's samples splits at the mark", "[engine][record]")
{
    // The writer sleeps, so everything below lands in the ring before it drains:
    // the first block and the mark and the second block are all visible together.
    engine::RecordSession session(48000.0, 0.5);
    const auto path = tempPath("marks.partial.wav");
    session.addInput(1, 0, path);
    engine::RecordWriter writer;
    REQUIRE(writer.begin(session));

    std::vector<float> a(100, 1.0f), b(100, 2.0f), c(100, 3.0f);
    const float* ia[] = {a.data()};
    const float* ib[] = {b.data()};
    const float* ic[] = {c.data()};
    session.capture(ia, 1, 100, 5000);
    session.markTake(7000);
    session.capture(ib, 1, 100, 7000);
    session.markTake(7000);
    session.capture(ic, 1, 100, 7000);
    session.finish();
    waitFor(writer);

    const auto results = writer.takeResults();
    REQUIRE(results.size() == 3);
    for (const auto& r : results)
    {
        CHECK(r.ok);
        CHECK(r.samplesWritten == 100);
        CHECK(r.samplesLost == 0);
    }
    CHECK(results[0].startPosition == 5000);
    CHECK(results[1].startPosition == 7000);
    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(results[0].path, channels));
    CHECK(channels[0].front() == 1.0f);
    CHECK(channels[0].back() == 1.0f);
    REQUIRE(engine::readWavSamples(results[1].path, channels));
    CHECK(channels[0].front() == 2.0f);
    REQUIRE(engine::readWavSamples(results[2].path, channels));
    CHECK(channels[0].front() == 3.0f);
}

TEST_CASE("A ring overrun becomes silence in the file so later samples keep their place", "[engine][record]")
{
    engine::RecordSession session(48000.0, 0.01); // a 480-sample ring (minimum 1024 inside)
    const auto path = tempPath("overrun.partial.wav");
    session.addInput(1, 0, path);
    engine::RecordWriter writer;
    REQUIRE(writer.begin(session));
    // Freeze the writer's view: push far more than the ring holds before it drains.
    // Blocks of 512: the ring (1024) takes two, the third is dropped, then we let it
    // drain and push one more with a recognisable value.
    std::vector<float> v(512, 0.25f);
    const float* in[] = {v.data()};
    // Pause the writer by hogging: capture quickly in a burst.
    session.capture(in, 1, 512, 0);
    session.capture(in, 1, 512, 512);
    session.capture(in, 1, 512, 1024); // may be dropped depending on drain timing
    session.capture(in, 1, 512, 1536); // and this one
    const auto droppedAfterBurst = session.totalDropped();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::vector<float> w(512, 0.75f);
    const float* inW[] = {w.data()};
    session.capture(inW, 1, 512, 2048);
    session.finish();
    waitFor(writer);

    const auto results = writer.takeResults();
    REQUIRE(results.size() == 1);
    CHECK(results[0].ok);
    CHECK(results[0].samplesWritten == 2560); // every captured sample has a place in the file
    CHECK(results[0].dropped == droppedAfterBurst);
    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(path, channels));
    REQUIRE(channels[0].size() == 2560);
    CHECK(channels[0][2048] == 0.75f); // the late block is exactly where it was captured
    CHECK(channels[0][2559] == 0.75f);
    // Every dropped sample reads as silence; which blocks drop depends on when the
    // writer drained, so count them rather than assume which.
    const auto silent = std::count(channels[0].begin(), channels[0].begin() + 2048, 0.0f);
    CHECK(static_cast<std::uint64_t>(silent) == static_cast<std::uint64_t>(droppedAfterBurst));
}

TEST_CASE("A take file that cannot be opened marks the input failed and counts the loss", "[engine][record]")
{
    engine::RecordSession session(48000.0, 0.5);
    // The first file opens in the temp folder; the take-2 name is derived from it, so
    // force a failure by making the first path's folder unwritable for takes: use a
    // path whose "-take2" sibling is a directory.
    const auto path = tempPath("failtake.partial.wav");
    const auto take2 = path.substr(0, path.size() - 12) + "-take2.partial.wav";
    std::filesystem::create_directories(take2); // a directory where the file should go
    session.addInput(1, 0, path);
    engine::RecordWriter writer;
    REQUIRE(writer.begin(session));
    std::vector<float> a(100, 1.0f);
    const float* in[] = {a.data()};
    session.capture(in, 1, 100, 0);
    session.markTake(0);
    session.capture(in, 1, 100, 0);
    session.capture(in, 1, 100, 0);
    session.finish();
    waitFor(writer);
    const auto results = writer.takeResults();
    REQUIRE(results.size() == 2);
    CHECK(results[0].ok);
    CHECK(results[0].samplesWritten == 100);
    CHECK_FALSE(results[1].ok);
    CHECK(results[1].samplesWritten == 0);
    CHECK(results[1].samplesLost == 200);
    std::filesystem::remove_all(take2);
}

TEST_CASE("Starting a recording at or past the loop end wraps first and places the take at the loop start",
          "[engine][record]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    engine.setProject(core::Project{});
    engine.setLoop(1000, 1500, true);
    engine.locate(1500); // exactly at the loop end
    std::vector<float> in0(64);
    const float* inputs[] = {in0.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};
    engine.process(inputs, 1, outputs, 2, 64);

    const auto path = tempPath("loopstart.partial.wav");
    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    session->addInput(1, 0, path);
    std::string error;
    REQUIRE(engine.startRecording(std::move(session), error));
    for (int i = 0; i < 64; ++i)
        in0[static_cast<size_t>(i)] = static_cast<float>(1000 + i);
    engine.process(inputs, 1, outputs, 2, 64);
    CHECK(engine.position() == 1064);
    engine.stop();
    engine.process(inputs, 1, outputs, 2, 64);
    for (int i = 0; i < 2000 && !engine.hasRecordingToCollect(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    auto finished = engine.collectRecording();
    REQUIRE(finished.has_value());
    CHECK(finished->session->startPosition() == 1000);
    CHECK(finished->results[0].samplesWritten == 64);
}
