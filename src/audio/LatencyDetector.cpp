#include "audio/LatencyDetector.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace aural::audio
{

std::vector<float> makeCalibrationSignal(double sampleRate)
{
    constexpr double durationSeconds = 0.010;
    constexpr double startHz = 400.0;
    constexpr double endHz = 6000.0;

    const auto length = static_cast<size_t>(std::max(1.0, std::round(sampleRate * durationSeconds)));
    std::vector<float> signal(length);

    const double chirpRate = (endHz - startHz) / durationSeconds; // Hz per second
    for (size_t i = 0; i < length; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        const double phase = 2.0 * std::numbers::pi * (startHz * t + 0.5 * chirpRate * t * t);
        const double window = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / length);
        signal[i] = static_cast<float>(window * std::sin(phase));
    }
    return signal;
}

std::optional<LatencyMeasurement> detectLatency(std::span<const float> captured,
                                                std::span<const float> reference,
                                                int emitSample,
                                                double sampleRate,
                                                float minimumConfidence,
                                                float minimumPeakLevel)
{
    if (reference.empty() || sampleRate <= 0.0 || emitSample < 0)
        return std::nullopt;
    if (captured.size() < reference.size() + static_cast<size_t>(emitSample))
        return std::nullopt;

    const size_t firstLag = static_cast<size_t>(emitSample);
    const size_t lastLag = captured.size() - reference.size(); // inclusive
    const size_t lagCount = lastLag - firstLag + 1;

    // Cross-correlate. O(N * M) with M around 480 samples: a few tens of millions of
    // multiply-adds for a one second capture, well under a frame on the message thread.
    double peak = 0.0;
    double sumSquares = 0.0;
    size_t peakLag = firstLag;
    for (size_t lag = firstLag; lag <= lastLag; ++lag)
    {
        double acc = 0.0;
        const float* c = captured.data() + lag;
        for (size_t j = 0; j < reference.size(); ++j)
            acc += static_cast<double>(c[j]) * static_cast<double>(reference[j]);

        // A loopback cable can invert polarity, so match on magnitude.
        const double magnitude = std::abs(acc);
        sumSquares += magnitude * magnitude;
        if (magnitude > peak)
        {
            peak = magnitude;
            peakLag = lag;
        }
    }

    if (peak <= 0.0)
        return std::nullopt;

    const double rms = std::sqrt(sumSquares / static_cast<double>(lagCount));
    const float confidence = rms > 0.0 ? static_cast<float>(peak / rms) : 0.0f;

    float peakLevel = 0.0f;
    for (size_t j = 0; j < reference.size(); ++j)
        peakLevel = std::max(peakLevel, std::abs(captured[peakLag + j]));

    if (peakLevel < minimumPeakLevel || confidence < minimumConfidence)
        return std::nullopt;

    LatencyMeasurement m;
    m.roundTripSamples = static_cast<int>(peakLag) - emitSample;
    m.roundTripMilliseconds = 1000.0 * static_cast<double>(m.roundTripSamples) / sampleRate;
    m.peakLevel = peakLevel;
    m.confidence = confidence;
    return m;
}

std::optional<LatencyMeasurement> combineMeasurements(std::span<const LatencyMeasurement> trials,
                                                      double sampleRate)
{
    if (trials.empty() || sampleRate <= 0.0)
        return std::nullopt;

    std::vector<int> samples;
    samples.reserve(trials.size());
    LatencyMeasurement combined;
    combined.confidence = trials[0].confidence;
    for (const auto& t : trials)
    {
        samples.push_back(t.roundTripSamples);
        combined.confidence = std::min(combined.confidence, t.confidence);
        combined.peakLevel = std::max(combined.peakLevel, t.peakLevel);
    }

    std::sort(samples.begin(), samples.end());
    const size_t n = samples.size();
    // For an even count take the lower middle so the result stays an observed value.
    combined.roundTripSamples = samples[(n - 1) / 2];
    combined.roundTripMilliseconds = 1000.0 * static_cast<double>(combined.roundTripSamples) / sampleRate;
    return combined;
}

} // namespace aural::audio
