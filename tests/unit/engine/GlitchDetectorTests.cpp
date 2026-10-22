#include "engine/GlitchDetector.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using northlight::engine::Glitch;
using northlight::engine::GlitchDetector;

namespace
{
constexpr double rate = 48000.0;
constexpr double pi = 3.14159265358979323846;

// A chord of three sines with a slow amplitude swell: smooth, but not trivial.
std::vector<float> music(int frames, int channels)
{
    std::vector<float> out(static_cast<size_t>(frames * channels));
    for (int n = 0; n < frames; ++n)
    {
        const double t = n / rate;
        const double env = 0.3 + 0.2 * std::sin(2.0 * pi * 0.5 * t);
        const double v = env * (std::sin(2.0 * pi * 220.0 * t) + 0.5 * std::sin(2.0 * pi * 331.0 * t) +
                                0.25 * std::sin(2.0 * pi * 1250.0 * t));
        for (int ch = 0; ch < channels; ++ch)
            out[static_cast<size_t>(n * channels + ch)] = static_cast<float>(v * (ch == 0 ? 1.0 : 0.8));
    }
    return out;
}

std::vector<Glitch> detect(const std::vector<float>& audio, int channels, int chunk = 512)
{
    GlitchDetector detector(channels, rate);
    std::vector<Glitch> found;
    const int frames = static_cast<int>(audio.size()) / channels;
    for (int start = 0; start < frames; start += chunk)
        detector.process(
            audio.data() + static_cast<size_t>(start * channels), std::min(chunk, frames - start), found);
    return found;
}
} // namespace

TEST_CASE("Smooth music has no glitches", "[engine][glitch]")
{
    CHECK(detect(music(48000 * 3, 2), 2).empty());
}

TEST_CASE("Noise-like signal has no glitches", "[engine][glitch]")
{
    // Uniform noise (a deterministic generator) at a moderate level, like cymbals.
    std::vector<float> audio(48000 * 2);
    std::uint32_t state = 12345;
    for (auto& s : audio)
    {
        state = state * 1664525u + 1013904223u;
        s = (static_cast<float>(state >> 8) / 16777216.0f - 0.5f) * 0.4f;
    }
    CHECK(detect(audio, 1).empty());
}

TEST_CASE("A step in the signal is one jump at its frame", "[engine][glitch]")
{
    auto audio = music(48000, 1);
    const int at = 20000;
    for (size_t i = at; i < audio.size(); ++i)
        audio[i] += 0.2f; // a DC step, as when a block starts from the wrong state
    const auto found = detect(audio, 1);
    REQUIRE(found.size() == 1);
    CHECK(found[0].kind == Glitch::Kind::jump);
    CHECK(found[0].frame == at);
    CHECK(found[0].channel == 0);
}

TEST_CASE("A click on both channels is one jump on all channels", "[engine][glitch]")
{
    auto audio = music(48000, 2);
    const int at = 30001;
    audio[static_cast<size_t>(at * 2)] += 0.5f;
    audio[static_cast<size_t>(at * 2 + 1)] -= 0.5f;
    const auto found = detect(audio, 2);
    REQUIRE(found.size() == 1);
    CHECK(found[0].frame == at);
    CHECK(found[0].channel == -1);
}

TEST_CASE("A waveform with hard edges is not a glitch", "[engine][glitch]")
{
    // Naive (aliasing) square waves, a high one whose period is not a whole number
    // of samples and a low one: an edge every half period, all part of the sound.
    // Only the first full edge can report, as there is no period before it to
    // compare with (like a note with no attack).
    for (const double hz : {2349.3, 110.0})
    {
        std::vector<float> audio(48000);
        for (size_t n = 0; n < audio.size(); ++n)
            audio[n] = std::fmod(static_cast<double>(n) * hz / rate, 1.0) < 0.5 ? 0.3f : -0.3f;
        const auto found = detect(audio, 1);
        CHECK(found.size() <= 1);
        for (const auto& g : found)
            CHECK(static_cast<double>(g.frame) < 1.5 * rate / hz);
    }
}

TEST_CASE("A click that repeats every device block is found every time", "[engine][glitch]")
{
    // A block-boundary bug repeats at the block size, which would otherwise pass as
    // a waveform's period.
    const int block = 256;
    auto audio = music(48000, 1);
    for (size_t n = 4096; n < 40000; n += block)
        audio[n] += 0.2f;
    GlitchDetector detector(1, rate);
    detector.setBlockSize(block);
    std::vector<Glitch> found;
    detector.process(audio.data(), static_cast<int>(audio.size()), found);
    CHECK(found.size() == (40000 - 4096 + block - 1) / block);
    for (const auto& g : found)
        CHECK(g.frame % block == 0);
}

TEST_CASE("A short run of digital silence inside a signal is a gap", "[engine][glitch]")
{
    auto audio = music(48000, 2);
    const int at = 24000, length = 256;
    for (int n = at; n < at + length; ++n)
        audio[static_cast<size_t>(n * 2)] = audio[static_cast<size_t>(n * 2 + 1)] = 0.0f;
    const auto found = detect(audio, 2);
    REQUIRE(found.size() == 1);
    CHECK(found[0].kind == Glitch::Kind::gap);
    CHECK(found[0].frame == at);
    CHECK(found[0].size == length);
}

TEST_CASE("Starting from silence and long silences are not glitches", "[engine][glitch]")
{
    // Half a second of silence, the signal, a second of silence (the transport
    // stopped), the signal again.
    std::vector<float> audio(24000, 0.0f);
    const auto part = music(24000, 1);
    audio.insert(audio.end(), part.begin(), part.end());
    audio.insert(audio.end(), 48000, 0.0f);
    audio.insert(audio.end(), part.begin(), part.end());
    CHECK(detect(audio, 1).empty());
}

TEST_CASE("The block size the audio arrives in does not change what is found", "[engine][glitch]")
{
    auto audio = music(48000, 1);
    audio[33333] += 0.3f;
    for (const int chunk : {1, 64, 1000, 4096})
    {
        const auto found = detect(audio, 1, chunk);
        REQUIRE(found.size() == 1);
        CHECK(found[0].frame == 33333);
    }
}

TEST_CASE("Reset keeps frame numbers counting and forgets the signal", "[engine][glitch]")
{
    GlitchDetector detector(1, rate);
    std::vector<Glitch> found;
    const auto a = music(10000, 1);
    detector.process(a.data(), 10000, found);
    detector.reset();
    CHECK(detector.framesProcessed() == 10000);
    // After a reset, audio that does not continue the previous signal is not a jump.
    std::vector<float> b(20000);
    for (size_t i = 0; i < b.size(); ++i)
        b[i] = static_cast<float>(0.4 * std::sin(2.0 * pi * 97.0 * static_cast<double>(i) / rate + 1.0));
    detector.process(b.data(), 20000, found);
    CHECK(found.empty());
}
