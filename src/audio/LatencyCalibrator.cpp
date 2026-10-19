#include "audio/LatencyCalibrator.h"

#include "engine/rt/RealtimeGuard.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace aural::audio
{

namespace
{
constexpr int pollIntervalMilliseconds = 30;
constexpr int minimumSuccessfulTrials = 3;

std::string formatMilliseconds(double ms)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", ms);
    return buffer;
}
} // namespace

LatencyCalibrator::LatencyCalibrator(AudioCallback& callback) : callback_(callback) {}

LatencyCalibrator::~LatencyCalibrator()
{
    cancel();
}

bool LatencyCalibrator::start(int inputChannel, int outputChannel, float level, OnFinished onFinished)
{
    AURAL_ASSERT_NOT_REALTIME("LatencyCalibrator::start");
    JUCE_ASSERT_MESSAGE_THREAD;

    if (running_ || !callback_.isDeviceRunning())
        return false;
    if (callback_.calibrationPhase() != AudioCallback::CalibrationPhase::idle)
        return false;

    sampleRate_ = callback_.currentSampleRate();
    if (sampleRate_ <= 0.0)
        return false;

    inputChannel_ = inputChannel;
    outputChannel_ = outputChannel;
    level_ = level;
    onFinished_ = std::move(onFinished);
    reference_ = makeCalibrationSignal(sampleRate_);
    measurements_.clear();
    currentTrial_ = 0;
    running_ = true;
    passThroughWasEnabled_ = callback_.isPassThroughEnabled();
    callback_.setPassThroughEnabled(false);

    if (!beginTrial())
    {
        running_ = false;
        callback_.setPassThroughEnabled(passThroughWasEnabled_);
        return false;
    }
    startTimer(pollIntervalMilliseconds);
    return true;
}

void LatencyCalibrator::cancel()
{
    if (!running_)
        return;
    stopTimer();
    callback_.requestCalibrationAbort();
    // If the device has already stopped, the callback reset its phase itself; if a
    // trial finished between our last poll and now, release the capture buffer.
    callback_.finishCalibration();
    callback_.setPassThroughEnabled(passThroughWasEnabled_);
    running_ = false;
    onFinished_ = nullptr;
}

bool LatencyCalibrator::beginTrial()
{
    ++currentTrial_;

    AudioCallback::CalibrationPlan plan;
    plan.reference = reference_;
    plan.captureLength = static_cast<int>(std::lround(sampleRate_ * captureSeconds));
    plan.emitSample = static_cast<int>(std::lround(sampleRate_ * emitOffsetSeconds));
    plan.inputChannel = inputChannel_;
    plan.outputChannel = outputChannel_;
    plan.level = level_;

    if (!callback_.prepareCalibration(std::move(plan)))
        return false;
    if (!callback_.startCalibration())
        return false;

    trialStartedAt_ = juce::Time::getMillisecondCounter();
    return true;
}

void LatencyCalibrator::completeTrial()
{
    const auto capture = callback_.calibrationCapture();
    const auto& plan = callback_.calibrationPlan();
    if (const auto m = detectLatency(capture, plan.reference, plan.emitSample, sampleRate_))
        measurements_.push_back(*m);
    callback_.finishCalibration();
}

void LatencyCalibrator::timerCallback()
{
    switch (callback_.calibrationPhase())
    {
        case AudioCallback::CalibrationPhase::done:
            completeTrial();
            break;

        case AudioCallback::CalibrationPhase::running:
            if (juce::Time::getMillisecondCounter() - trialStartedAt_ <
                static_cast<juce::uint32>(trialTimeoutMilliseconds))
                return;
            callback_.requestCalibrationAbort();
            finish({std::nullopt,
                    "The audio device stopped delivering audio during calibration.",
                    static_cast<int>(measurements_.size()),
                    currentTrial_,
                    sampleRate_});
            return;

        case AudioCallback::CalibrationPhase::idle:
            // The device stopped (or was aborted) under us.
            if (!callback_.isDeviceRunning())
            {
                finish({std::nullopt,
                        "The audio device stopped during calibration.",
                        static_cast<int>(measurements_.size()),
                        currentTrial_,
                        sampleRate_});
                return;
            }
            break;
    }

    if (currentTrial_ < trialsTotal)
    {
        if (!beginTrial())
            finish({std::nullopt,
                    "Could not start the next calibration trial.",
                    static_cast<int>(measurements_.size()),
                    currentTrial_,
                    sampleRate_});
        return;
    }

    Result result;
    result.trialsTotal = trialsTotal;
    result.trialsSucceeded = static_cast<int>(measurements_.size());
    result.sampleRate = sampleRate_;

    if (result.trialsSucceeded < minimumSuccessfulTrials)
    {
        result.message = "No loopback signal detected. Connect the selected output to the selected input "
                         "with a cable, make sure the input gain is up, and try again.";
        finish(std::move(result));
        return;
    }

    result.measurement = combineMeasurements(measurements_, sampleRate_);
    if (result.measurement.has_value())
        result.message = "Round trip " + std::to_string(result.measurement->roundTripSamples) + " samples (" +
                         formatMilliseconds(result.measurement->roundTripMilliseconds) + " ms) from " +
                         std::to_string(result.trialsSucceeded) + " of " + std::to_string(trialsTotal) +
                         " trials.";
    finish(std::move(result));
}

void LatencyCalibrator::finish(Result result)
{
    stopTimer();
    running_ = false;
    callback_.setPassThroughEnabled(passThroughWasEnabled_);
    auto handler = std::move(onFinished_);
    onFinished_ = nullptr;
    if (handler)
        handler(result);
}

} // namespace aural::audio
