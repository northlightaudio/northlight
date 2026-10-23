#include "audio/PeakStore.h"
#include "engine/WavFile.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <thread>
#include <vector>

using namespace northlight;

namespace
{
// A mono file of silence with a +0.8 / -0.4 pair at `spike`.
std::string writeSpike(const char* name, std::int64_t length, std::int64_t spike)
{
    const auto dir = std::filesystem::temp_directory_path() / "northlight-tests";
    std::filesystem::create_directories(dir);
    const auto path = (dir / name).string();
    std::remove(path.c_str());
    std::vector<float> samples(static_cast<size_t>(length));
    samples[static_cast<size_t>(spike)] = 0.8f;
    samples[static_cast<size_t>(spike + 1)] = -0.4f;
    engine::WavWriter writer;
    REQUIRE(writer.open(path, 1, 48000.0));
    const float* channels[] = {samples.data()};
    REQUIRE(writer.write(channels, static_cast<int>(length)));
    REQUIRE(writer.close());
    return path;
}

// Polls the store (its thread fills it in the background) until `ready` holds.
template <typename Ready> engine::WaveformView waitFor(Ready ready)
{
    std::optional<engine::WaveformView> found;
    for (int i = 0; i < 500 && !found.has_value(); ++i)
    {
        found = ready();
        if (!found.has_value())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    INFO("timed out waiting for the peak store");
    REQUIRE(found.has_value());
    return *found;
}
} // namespace

TEST_CASE("The peak store decodes samples for a view zoomed past the peaks", "[audio][peaks]")
{
    const auto path = writeSpike("peakstore-zoom.wav", 200000, 150000);
    audio::PeakStore store;

    // Coarse zoom: peaks only, no samples asked for.
    const auto coarse = waitFor(
        [&]() -> std::optional<engine::WaveformView>
        {
            auto view = store.viewFor(path, 48000.0, 0, 200000, 300.0);
            return view.empty() ? std::nullopt : std::optional(view);
        });
    CHECK_FALSE(coarse.hasSamples(149990, 150010));

    // Fine zoom: samples arrive, and a pixel next to the spike reads silence.
    const auto fine = waitFor(
        [&]() -> std::optional<engine::WaveformView>
        {
            auto view = store.viewFor(path, 48000.0, 149000, 151000, 2.0);
            return view.hasSamples(149000, 151000) ? std::optional(view) : std::nullopt;
        });
    auto r = fine.lookup(0, 150010, 150012);
    CHECK(r.maximum == 0.0f);
    CHECK(r.minimum == 0.0f);
    r = fine.lookup(0, 150000, 150002);
    CHECK(r.maximum == 0.8f);
    CHECK(r.minimum == -0.4f);

    std::remove(path.c_str());
    std::filesystem::remove(path + ".48000.northlight-peaks");
}
