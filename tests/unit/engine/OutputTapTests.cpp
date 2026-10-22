#include "engine/OutputTap.h"
#include "engine/WavFile.h"
#include "engine/rt/RealtimeGuard.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using northlight::engine::OutputTap;

namespace
{
constexpr double pi = 3.14159265358979323846;

std::string tapFolder(const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / "northlight-tests" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}

std::string readText(const std::string& path)
{
    std::ifstream in(path);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// Plays `blocks` blocks of a 440 Hz sine through the tap, like the callback does.
// The block numbered `brokenBlock` starts with its phase reset: a block-boundary click.
void play(OutputTap& tap, int blocks, int blockSize, double rate, int brokenBlock = -1, int lateBlock = -1)
{
    std::vector<float> left(static_cast<size_t>(blockSize)), right(static_cast<size_t>(blockSize));
    double phase = 0.0;
    const double step = 2.0 * pi * 440.0 / rate;
    for (int b = 0; b < blocks; ++b)
    {
        if (b == brokenBlock)
            phase += 1.5;
        for (int i = 0; i < blockSize; ++i, phase += step)
            left[static_cast<size_t>(i)] = right[static_cast<size_t>(i)] =
                static_cast<float>(0.5 * std::sin(phase));
        const float* outputs[] = {left.data(), right.data()};
        tap.capture(outputs, 2, blockSize, rate, b == lateBlock ? 1.2 : 0.0, b == lateBlock ? 0.3 : 0.2);
    }
}
} // namespace

TEST_CASE("The output tap writes what it was given to output.wav", "[engine][tap]")
{
    const auto folder = tapFolder("tap-clean");
    {
        OutputTap tap(folder);
        play(tap, 200, 256, 48000.0);
        tap.stop();
        const auto t = tap.totals();
        CHECK(t.frames == 200 * 256);
        CHECK(t.jumps == 0);
        CHECK(t.gaps == 0);
        CHECK(t.droppedFrames == 0);
    }
    std::vector<std::vector<float>> channels;
    northlight::engine::WavInfo info;
    REQUIRE(northlight::engine::readWavSamples(folder + "/output.wav", channels, &info));
    CHECK(info.numChannels == 2);
    CHECK(info.sampleRate == 48000.0);
    CHECK(info.lengthSamples == 200 * 256);
    CHECK(channels[0][1000] == Catch::Approx(0.5 * std::sin(1000 * 2.0 * pi * 440.0 / 48000.0)).margin(1e-4));
    CHECK(std::filesystem::exists(folder + "/summary.txt"));
    CHECK(readText(folder + "/summary.txt").find("0 jumps") != std::string::npos);
}

TEST_CASE("A click at a block boundary is logged with block offset 0", "[engine][tap]")
{
    const auto folder = tapFolder("tap-boundary");
    OutputTap tap(folder);
    play(tap, 200, 256, 48000.0, 120);
    tap.stop();
    const auto t = tap.totals();
    CHECK(t.jumps == 1);
    CHECK(t.jumpsAtBlockStart == 1);
    const auto events = readText(folder + "/events.csv");
    // 120 blocks of 256 frames in: 0.64 s.
    CHECK(events.find("output.wav,0.640000,jump,all,") != std::string::npos);
    CHECK(events.find(",0,256\n") != std::string::npos);
}

TEST_CASE("Late callbacks and overruns are logged with their time", "[engine][tap]")
{
    const auto folder = tapFolder("tap-late");
    OutputTap tap(folder);
    play(tap, 100, 480, 48000.0, -1, 50);
    const float* none[] = {nullptr, nullptr};
    tap.capture(none, 2, 480, 48000.0, 0.0, 1.4); // an overrun (silence)
    tap.stop();
    CHECK(tap.totals().lateCallbacks == 1);
    CHECK(tap.totals().overruns == 1);
    const auto events = readText(folder + "/events.csv");
    CHECK(events.find("output.wav,0.500000,late-callback,all,1.2,0,480") != std::string::npos);
    CHECK(events.find("output.wav,1.000000,overrun,all,1.4,0,480") != std::string::npos);
}

TEST_CASE("A sample rate change starts a new file", "[engine][tap]")
{
    const auto folder = tapFolder("tap-rate");
    OutputTap tap(folder);
    play(tap, 50, 256, 48000.0);
    play(tap, 50, 256, 44100.0);
    tap.stop();
    CHECK(northlight::engine::readWavInfo(folder + "/output.wav")->sampleRate == 48000.0);
    CHECK(northlight::engine::readWavInfo(folder + "/output-2.wav")->sampleRate == 44100.0);
    CHECK(northlight::engine::readWavInfo(folder + "/output-2.wav")->lengthSamples == 50 * 256);
}

TEST_CASE("Audio the tap cannot buffer is dropped and logged, not written", "[engine][tap]")
{
    const auto folder = tapFolder("tap-drop");
    OutputTap tap(folder, 0.001); // 192 frames of room
    play(tap, 1, 1024, 48000.0);  // a block bigger than the whole ring
    play(tap, 1, 128, 48000.0);
    tap.stop();
    CHECK(tap.totals().droppedFrames == 1024);
    CHECK(tap.totals().frames == 128);
    CHECK(readText(folder + "/events.csv").find("tap-dropped,all,1024") != std::string::npos);
}

#if defined(NORTHLIGHT_RT_CHECKS)
namespace
{
std::atomic<int> tapViolations{0};
void countTapViolation(const northlight::rt::Violation&) noexcept
{
    tapViolations.fetch_add(1, std::memory_order_relaxed);
}
} // namespace

TEST_CASE("Capturing into the tap does not allocate on a real-time thread", "[engine][tap][rt]")
{
    const auto folder = tapFolder("tap-rt");
    OutputTap tap(folder);
    std::vector<float> block(256, 0.25f);
    const float* outputs[] = {block.data(), block.data()};
    const auto previous = northlight::rt::setViolationHandler(&countTapViolation);
    tapViolations = 0;
    {
        northlight::rt::ScopedRealtimeSection section;
        for (int i = 0; i < 100; ++i)
            tap.capture(outputs, 2, 256, 48000.0, 0.0, 0.1);
    }
    northlight::rt::setViolationHandler(previous);
    CHECK(tapViolations == 0);
    tap.stop();
}
#endif
