#pragma once

#include <optional>
#include <span>
#include <vector>

// Loopback latency detection.
//
// The calibration plays a short, known test signal on one output and records one
// input at the same time, with both streams indexed by the same running sample
// counter. The round-trip latency is the offset at which the recording best matches
// the test signal, minus the sample at which the signal was emitted. That offset
// includes the driver's reported input and output latency plus anything the driver
// does not report (converter group delay, USB packetisation, external gear).
//
// Everything here is pure, allocation-free in the analysis loops once the buffers
// exist, and has no JUCE dependency; the device-side orchestration lives in
// LatencyCalibrator.

namespace northlight::audio
{

struct LatencyMeasurement
{
    int roundTripSamples = 0;
    double roundTripMilliseconds = 0.0;
    float peakLevel = 0.0f;  // peak absolute sample value of the detected signal, 0..1
    float confidence = 0.0f; // correlation peak over correlation RMS; higher is more certain
};

// A 10 ms Hann-windowed linear chirp (400 Hz to 6 kHz) at the given sample rate.
// Chirps survive band-limiting far better than impulses and have a sharp
// autocorrelation peak, so the match is sample-accurate on real converters.
std::vector<float> makeCalibrationSignal(double sampleRate);

// Minimum confidence for a measurement to be accepted. Random noise over a one
// second capture produces a peak-to-RMS ratio of roughly 4 to 5; a real match on a
// typical interface is 20 or higher.
constexpr float defaultMinimumConfidence = 8.0f;

// Minimum peak level for the recording to count as "signal present" (-60 dBFS).
constexpr float defaultMinimumPeakLevel = 0.001f;

// Finds `reference` inside `captured`. `captured[i]` was recorded at the same
// running sample index at which output sample `i` was played, and the reference
// signal was written to the output starting at `emitSample`.
std::optional<LatencyMeasurement> detectLatency(std::span<const float> captured,
                                                std::span<const float> reference,
                                                int emitSample,
                                                double sampleRate,
                                                float minimumConfidence = defaultMinimumConfidence,
                                                float minimumPeakLevel = defaultMinimumPeakLevel);

// Combines several trials into one result: the median round trip, the lowest
// confidence and the highest peak. Returns nullopt when `trials` is empty.
std::optional<LatencyMeasurement> combineMeasurements(std::span<const LatencyMeasurement> trials,
                                                      double sampleRate);

} // namespace northlight::audio
