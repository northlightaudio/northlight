#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/Recording.h"
#include "engine/WavFile.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

namespace
{
const aural::plugins::BuiltInFactory builtIns;
} // namespace

using namespace aural;

namespace
{
class NoMediaLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double, std::string& error) override
    {
        error = "no media in this test";
        return nullptr;
    }
};

std::string tempPath(const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / "aural-tests";
    std::filesystem::create_directories(dir);
    const auto path = (dir / name).string();
    std::remove(path.c_str());
    return path;
}

void waitForWriter(engine::AudioEngine& engine)
{
    for (int i = 0; i < 2000 && !engine.hasRecordingToCollect(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
} // namespace

TEST_CASE("Recording placement shifts clips earlier by the latency and trims at zero", "[engine][record]")
{
    auto p = engine::placeRecording(10000, 5000, 208);
    CHECK(p.start == 9792);
    CHECK(p.sourceOffset == 0);
    CHECK(p.length == 5000);

    p = engine::placeRecording(100, 5000, 300); // started before the timeline
    CHECK(p.start == 0);
    CHECK(p.sourceOffset == 200);
    CHECK(p.length == 4800);

    p = engine::placeRecording(0, 100, 300); // shorter than the latency: nothing left
    CHECK(p.length == 0);
}

TEST_CASE("Loop recording splits each pass into its own take file at the exact sample", "[engine][record]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    engine.setProject(core::Project{});
    engine.setLoop(1000, 1500, true);
    engine.locate(900);
    std::vector<float> in0(64);
    const float* inputs[] = {in0.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};
    engine.process(inputs, 1, outputs, 2, 64);

    const auto path = tempPath("takes.partial.wav");
    std::remove(tempPath("takes-take2.partial.wav").c_str());
    std::remove(tempPath("takes-take3.partial.wav").c_str());
    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    session->addInput(1, 0, path);
    std::string error;
    REQUIRE(engine.startRecording(std::move(session), error));

    // Inputs carry the timeline position so take contents can be checked.
    std::int64_t position = 900;
    for (int block = 0; block < 25; ++block) // 900 -> 1500 (take 1), two full passes, part of a third
    {
        for (int i = 0; i < 64; ++i)
        {
            // The engine wraps inside the block; model the same wrapping here.
            auto p = position + i;
            while (p >= 1500)
                p -= 500;
            in0[static_cast<size_t>(i)] = static_cast<float>(p);
        }
        engine.process(inputs, 1, outputs, 2, 64);
        position = engine.position(); // keep in sync with the engine's wrapping
    }
    engine.stop();
    engine.process(inputs, 1, outputs, 2, 64);
    waitForWriter(engine);
    auto finished = engine.collectRecording();
    REQUIRE(finished.has_value());

    // 25 blocks = 1600 samples captured: take 1 = 900..1500 (600), take 2 = 500, take 3 = 500.
    const auto& results = finished->results;
    REQUIRE(results.size() == 3);
    CHECK(results[0].takeIndex == 0);
    CHECK(results[0].startPosition == 900);
    CHECK(results[0].samplesWritten == 600);
    CHECK(results[1].takeIndex == 1);
    CHECK(results[1].startPosition == 1000);
    CHECK(results[1].samplesWritten == 500);
    CHECK(results[2].takeIndex == 2);
    CHECK(results[2].startPosition == 1000);
    CHECK(results[2].samplesWritten == 500);
    CHECK(results[1].path == path.substr(0, path.size() - 12) + "-take2.partial.wav");

    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(results[0].path, channels));
    CHECK(channels[0].front() == 900.0f);
    CHECK(channels[0].back() == 1499.0f);
    REQUIRE(engine::readWavSamples(results[1].path, channels));
    CHECK(channels[0].front() == 1000.0f);
    CHECK(channels[0].back() == 1499.0f);
    REQUIRE(engine::readWavSamples(results[2].path, channels));
    CHECK(channels[0].size() == 500);
    CHECK(channels[0].front() == 1000.0f);
}
