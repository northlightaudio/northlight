#include "audio/LatencyDetector.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace northlight::audio;

namespace
{
constexpr double sampleRate = 48000.0;

// Deterministic white noise in [-1, 1).
struct Noise
{
    uint32_t state = 0x1234567u;
    float next()
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / 8388608.0f - 1.0f;
    }
};

std::vector<float> makeCapture(size_t length, float noiseLevel)
{
    std::vector<float> capture(length);
    Noise noise;
    for (auto& s : capture)
        s = noise.next() * noiseLevel;
    return capture;
}

void addSignal(std::vector<float>& capture, const std::vector<float>& signal, size_t at, float gain)
{
    for (size_t i = 0; i < signal.size() && at + i < capture.size(); ++i)
        capture[at + i] += signal[i] * gain;
}
} // namespace

TEST_CASE("The calibration signal is short, windowed and bounded", "[audio][latency]")
{
    const auto signal = makeCalibrationSignal(sampleRate);
    CHECK(signal.size() == 480);
    CHECK(signal.front() == 0.0f);
    float peak = 0.0f;
    for (float s : signal)
        peak = std::max(peak, std::abs(s));
    CHECK(peak <= 1.0f);
    CHECK(peak > 0.9f);
}

TEST_CASE("A delayed, attenuated copy of the signal in noise is located exactly", "[audio][latency]")
{
    const auto reference = makeCalibrationSignal(sampleRate);
    const int emitSample = 4800;
    const int latency = 437;

    auto capture = makeCapture(48000, 0.003f); // noise at roughly -50 dBFS
    addSignal(capture, reference, static_cast<size_t>(emitSample + latency), 0.3f);

    const auto result = detectLatency(capture, reference, emitSample, sampleRate);
    REQUIRE(result.has_value());
    CHECK(result->roundTripSamples == latency);
    CHECK(std::abs(result->roundTripMilliseconds - 1000.0 * latency / sampleRate) < 1e-9);
    CHECK(result->peakLevel > 0.25f);
    CHECK(result->peakLevel < 0.35f);
    CHECK(result->confidence > defaultMinimumConfidence);
}

TEST_CASE("Polarity-inverted loopback is still detected", "[audio][latency]")
{
    const auto reference = makeCalibrationSignal(sampleRate);
    auto capture = makeCapture(24000, 0.001f);
    addSignal(capture, reference, 1000 + 2000, -0.5f);

    const auto result = detectLatency(capture, reference, 1000, sampleRate);
    REQUIRE(result.has_value());
    CHECK(result->roundTripSamples == 2000);
}

TEST_CASE("A band-limited loopback is located within a couple of samples", "[audio][latency]")
{
    const auto reference = makeCalibrationSignal(sampleRate);
    const int emitSample = 2400;
    const int latency = 1234;

    auto capture = makeCapture(48000, 0.002f);
    addSignal(capture, reference, static_cast<size_t>(emitSample + latency), 0.5f);

    // One-pole low-pass at about 5 kHz, as a crude converter/anti-alias model.
    const float alpha = 0.5f;
    float y = 0.0f;
    for (auto& s : capture)
    {
        y += alpha * (s - y);
        s = y;
    }

    const auto result = detectLatency(capture, reference, emitSample, sampleRate);
    REQUIRE(result.has_value());
    CHECK(std::abs(result->roundTripSamples - latency) <= 2);
}

TEST_CASE("Noise alone is rejected", "[audio][latency]")
{
    const auto reference = makeCalibrationSignal(sampleRate);
    const auto capture = makeCapture(48000, 0.1f);
    CHECK_FALSE(detectLatency(capture, reference, 100, sampleRate).has_value());
}

TEST_CASE("Silence is rejected", "[audio][latency]")
{
    const auto reference = makeCalibrationSignal(sampleRate);
    const std::vector<float> capture(48000, 0.0f);
    CHECK_FALSE(detectLatency(capture, reference, 100, sampleRate).has_value());
}

TEST_CASE("A signal too quiet to trust is rejected", "[audio][latency]")
{
    const auto reference = makeCalibrationSignal(sampleRate);
    auto capture = makeCapture(48000, 0.0f);
    addSignal(capture, reference, 500, 0.0005f); // -66 dBFS, below the floor
    CHECK_FALSE(detectLatency(capture, reference, 100, sampleRate).has_value());
}

TEST_CASE("Signal before the emit point is ignored", "[audio][latency]")
{
    const auto reference = makeCalibrationSignal(sampleRate);
    auto capture = makeCapture(48000, 0.001f);
    addSignal(capture, reference, 100, 0.5f);  // leaked earlier: impossible as a round trip
    addSignal(capture, reference, 9000, 0.2f); // the real one
    const auto result = detectLatency(capture, reference, 8000, sampleRate);
    REQUIRE(result.has_value());
    CHECK(result->roundTripSamples == 1000);
}

TEST_CASE("Captures shorter than emit point plus signal are rejected", "[audio][latency]")
{
    const auto reference = makeCalibrationSignal(sampleRate);
    const std::vector<float> capture(600, 0.5f);
    CHECK_FALSE(detectLatency(capture, reference, 200, sampleRate).has_value());
    CHECK_FALSE(detectLatency(capture, reference, -1, sampleRate).has_value());
    CHECK_FALSE(detectLatency(capture, {}, 0, sampleRate).has_value());
}

TEST_CASE("Combining trials takes the median round trip and the lowest confidence", "[audio][latency]")
{
    const std::vector<LatencyMeasurement> trials{
        {440, 0.0, 0.3f, 30.0f},
        {437, 0.0, 0.4f, 12.0f},
        {900, 0.0, 0.2f, 25.0f}, // outlier
        {438, 0.0, 0.3f, 28.0f},
        {437, 0.0, 0.3f, 29.0f},
    };
    const auto combined = combineMeasurements(trials, sampleRate);
    REQUIRE(combined.has_value());
    CHECK(combined->roundTripSamples == 438);
    CHECK(combined->confidence == 12.0f);
    CHECK(combined->peakLevel == 0.4f);
    CHECK(std::abs(combined->roundTripMilliseconds - 1000.0 * 438 / sampleRate) < 1e-9);

    CHECK_FALSE(combineMeasurements({}, sampleRate).has_value());
}
