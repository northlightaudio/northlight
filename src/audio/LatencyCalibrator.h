#pragma once

#include "audio/AudioCallback.h"
#include "audio/LatencyDetector.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Runs the loopback latency calibration against a running AudioCallback.
//
// Message thread only. Each trial asks the callback to play the test signal on one
// output and record one input for a second, then analyses the recording here (never
// on the audio thread). Several trials are combined into a median so a single
// glitch cannot skew the result. Pass-through monitoring is switched off for the
// whole run (a loopback cable would otherwise feed the output back into the input)
// and restored afterwards.

namespace aural::audio
{

class LatencyCalibrator final : private juce::Timer
{
public:
    struct Result
    {
        std::optional<LatencyMeasurement> measurement; // empty on failure
        std::string message;                           // human-readable summary or failure reason
        int trialsSucceeded = 0;
        int trialsTotal = 0;
        double sampleRate = 0.0;
    };

    struct Progress
    {
        int trial = 0; // 1-based trial in progress
        int trialsTotal = 0;
    };

    using OnFinished = std::function<void(const Result&)>;

    explicit LatencyCalibrator(AudioCallback& callback);
    ~LatencyCalibrator() override;

    // inputChannel/outputChannel index the device's *active* channels, in the order
    // the callback receives them. `level` is the linear playback level of the test
    // signal. The callback fires on the message thread, after which the calibrator is
    // idle again. Returns false (and does not call `onFinished`) if the device is not
    // running or a calibration is already in progress.
    bool start(int inputChannel, int outputChannel, float level, OnFinished onFinished);

    bool isRunning() const noexcept { return running_; }
    Progress progress() const noexcept { return {currentTrial_, trialsTotal}; }

    // Abandons an in-progress calibration without calling the completion handler.
    void cancel();

    static constexpr int trialsTotal = 5;
    static constexpr double captureSeconds = 1.0;
    static constexpr double emitOffsetSeconds = 0.1;
    static constexpr int trialTimeoutMilliseconds = 4000;

private:
    void timerCallback() override;
    bool beginTrial();
    void completeTrial();
    void finish(Result result);

    AudioCallback& callback_;
    OnFinished onFinished_;
    bool running_ = false;
    int currentTrial_ = 0;
    int inputChannel_ = 0;
    int outputChannel_ = 0;
    float level_ = 0.5f;
    double sampleRate_ = 0.0;
    bool passThroughWasEnabled_ = false;
    std::vector<float> reference_;
    std::vector<LatencyMeasurement> measurements_;
    juce::uint32 trialStartedAt_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LatencyCalibrator)
};

} // namespace aural::audio
