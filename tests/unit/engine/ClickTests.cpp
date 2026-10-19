#include "core/Model.h"
#include "engine/AudioEngine.h"
#include "engine/Click.h"
#include "engine/Recording.h"
#include "engine/WavFile.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
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
using Catch::Approx;

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

float peak(const std::vector<float>& v, size_t from, size_t to)
{
    float p = 0.0f;
    for (size_t i = from; i < to && i < v.size(); ++i)
        p = std::max(p, std::abs(v[i]));
    return p;
}

void waitForWriter(engine::AudioEngine& engine)
{
    for (int i = 0; i < 2000 && !engine.hasRecordingToCollect(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
} // namespace

TEST_CASE("Clicks land on beats, accented on bar starts, and work before zero", "[engine][click]")
{
    engine::ClickGenerator click;
    const double spb = 1000.0; // beat every 1000 samples
    std::vector<float> left(5000, 0.0f), right(5000, 0.0f);
    // Render from -2000 in blocks of 64 so beats fall mid-block too.
    for (int done = 0; done < 5000; done += 64)
    {
        const int n = std::min(64, 5000 - done);
        click.render(-2000 + done, n, 48000.0, spb, 4, 1.0f, left.data() + done, right.data() + done);
    }
    // Beats at -2000, -1000, 0, 1000, 2000 -> indices 0, 1000, 2000, 3000, 4000.
    for (size_t beat : {0u, 1000u, 2000u, 3000u, 4000u})
    {
        CHECK(peak(left, beat, beat + 100) > 0.3f);
        CHECK(peak(left, beat + 1440, beat + 1000 > 5000 ? 5000 : beat + 1000) <
              1e-6f); // burst is 1440 samples
    }
    CHECK(left == right);

    // Bar starts (beat index multiple of 4: -2000 is beat -2, 0 is beat 0) use the
    // higher pitch: more zero crossings in the first 10 ms than a plain beat.
    const auto crossings = [&](size_t from)
    {
        int c = 0;
        for (size_t i = from + 1; i < from + 480; ++i)
            c += (left[i - 1] < 0.0f) != (left[i] < 0.0f);
        return c;
    };
    CHECK(crossings(2000) > crossings(1000));
    CHECK(crossings(2000) > crossings(3000));
}

TEST_CASE("The metronome plays while the transport runs and stays silent when off", "[engine][click]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    core::Project p;
    p.tempoBpm = 120.0; // 24000 samples per beat
    engine.setProject(p);
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};

    engine.play();
    engine.process(outputs, 2, 64); // the beat at position 0 clicks in this block
    CHECK(peak(l, 0, 64) == 0.0f);  // metronome off

    engine.locate(0);
    engine.setMetronome(true, 0.5f);
    engine.process(outputs, 2, 64);
    CHECK(peak(l, 0, 64) > 0.1f);
    engine.process(outputs, 2, 64);
    CHECK(peak(l, 0, 64) > 0.0f); // still inside the burst
    for (int i = 0; i < 40; ++i)
        engine.process(outputs, 2, 64);
    CHECK(peak(l, 0, 64) == 0.0f); // 2688 samples in: between beats
}

TEST_CASE("A count-in holds the position and delays capture", "[engine][record][click]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    engine.setProject(core::Project{});
    engine.setMetronome(true, 0.5f);
    engine.locate(5000);
    std::vector<float> in0(64, 0.25f);
    const float* inputs[] = {in0.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};
    engine.process(inputs, 1, outputs, 2, 64);

    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    session->addInput(1, 0, tempPath("countin.wav"));
    std::string error;
    REQUIRE(engine.startRecording(std::move(session), error, 100)); // 100 samples of count-in

    engine.process(inputs, 1, outputs, 2, 64); // count-in 64
    CHECK(engine.isCountingIn());
    CHECK(engine.position() == 5000);
    engine.process(inputs, 1, outputs, 2, 64); // 36 more count-in, then 28 recorded
    CHECK_FALSE(engine.isCountingIn());
    CHECK(engine.position() == 5028);
    CHECK(engine.isRecording());

    engine.stop();
    engine.process(inputs, 1, outputs, 2, 64);
    waitForWriter(engine);
    auto finished = engine.collectRecording();
    REQUIRE(finished.has_value());
    CHECK(finished->session->startPosition() == 5000);
    CHECK(finished->results[0].samplesWritten == 28);
}

TEST_CASE("Punch in and out bound the captured range exactly", "[engine][record]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.prepare(48000.0, 64);
    engine.setProject(core::Project{});
    engine.setPunch(1000, 1500, true);
    engine.locate(900); // pre-roll of 100 samples before the punch-in
    std::vector<float> in0(64);
    const float* inputs[] = {in0.data()};
    std::vector<float> l(64), r(64);
    float* outputs[] = {l.data(), r.data()};
    engine.process(inputs, 1, outputs, 2, 64);

    const auto path = tempPath("punch.wav");
    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    session->addInput(1, 0, path);
    std::string error;
    REQUIRE(engine.startRecording(std::move(session), error));

    std::int64_t sample = 900;
    for (int block = 0; block < 20; ++block) // 900 .. 2180
    {
        for (int i = 0; i < 64; ++i)
            in0[static_cast<size_t>(i)] = static_cast<float>(sample + i); // the timeline position itself
        engine.process(inputs, 1, outputs, 2, 64);
        sample += 64;
    }
    CHECK(engine.position() == 900 + 20 * 64);
    engine.stop();
    engine.process(inputs, 1, outputs, 2, 64);
    waitForWriter(engine);
    auto finished = engine.collectRecording();
    REQUIRE(finished.has_value());
    CHECK(finished->session->startPosition() == 1000);
    REQUIRE(finished->results[0].samplesWritten == 500);
    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(path, channels));
    CHECK(channels[0].front() == 1000.0f);
    CHECK(channels[0].back() == 1499.0f);
}
