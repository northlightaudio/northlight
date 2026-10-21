#include "audio/AudioCallback.h"

#include "audio/PassThrough.h"
#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace northlight::audio
{

namespace
{
float blockPeak(const float* const* channels, int numChannels, int numSamples) noexcept
{
    float peak = 0.0f;
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* data = channels[ch];
        if (data == nullptr)
            continue;
        for (int i = 0; i < numSamples; ++i)
            peak = std::max(peak, std::abs(data[i]));
    }
    return peak;
}

void raisePeak(std::atomic<float>& target, float candidate) noexcept
{
    float current = target.load(std::memory_order_relaxed);
    while (candidate > current &&
           !target.compare_exchange_weak(current, candidate, std::memory_order_relaxed))
    {
    }
}
} // namespace

AudioCallback::AudioCallback() = default;
AudioCallback::~AudioCallback() = default;

// ----- control -------------------------------------------------------------------

void AudioCallback::setEngine(engine::AudioEngine* engine) noexcept
{
    engine_.store(engine, std::memory_order_release);
}

void AudioCallback::setMasterGain(float linearGain) noexcept
{
    targetGain_.store(std::clamp(linearGain, 0.0f, 8.0f), std::memory_order_relaxed);
}

float AudioCallback::masterGain() const noexcept
{
    return targetGain_.load(std::memory_order_relaxed);
}

void AudioCallback::setPassThroughEnabled(bool enabled) noexcept
{
    passThrough_.store(enabled, std::memory_order_relaxed);
}

bool AudioCallback::isPassThroughEnabled() const noexcept
{
    return passThrough_.load(std::memory_order_relaxed);
}

// ----- observation ---------------------------------------------------------------

std::uint64_t AudioCallback::callbackCount() const noexcept
{
    return callbackCount_.load(std::memory_order_relaxed);
}

bool AudioCallback::isDeviceRunning() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

int AudioCallback::numActiveInputs() const noexcept
{
    return numInputs_.load(std::memory_order_relaxed);
}

int AudioCallback::numActiveOutputs() const noexcept
{
    return numOutputs_.load(std::memory_order_relaxed);
}

double AudioCallback::currentSampleRate() const noexcept
{
    return sampleRate_.load(std::memory_order_relaxed);
}

float AudioCallback::takeInputPeak() noexcept
{
    return inputPeak_.exchange(0.0f, std::memory_order_relaxed);
}

float AudioCallback::takeOutputPeak() noexcept
{
    return outputPeak_.exchange(0.0f, std::memory_order_relaxed);
}

bool AudioCallback::takeErrorFlag() noexcept
{
    return errorFlag_.exchange(false, std::memory_order_acq_rel);
}

// ----- calibration ----------------------------------------------------------------

AudioCallback::CalibrationPhase AudioCallback::calibrationPhase() const noexcept
{
    return calibrationPhase_.load(std::memory_order_acquire);
}

bool AudioCallback::prepareCalibration(CalibrationPlan plan)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioCallback::prepareCalibration");
    if (calibrationPhase() != CalibrationPhase::idle)
        return false;
    if (plan.reference.empty() || plan.captureLength <= 0 || plan.emitSample < 0 ||
        plan.emitSample + static_cast<int>(plan.reference.size()) > plan.captureLength)
        return false;

    plan_ = std::move(plan);
    capture_.assign(static_cast<size_t>(plan_.captureLength), 0.0f);
    capturePosition_ = 0;
    abortRequested_.store(false, std::memory_order_relaxed);
    return true;
}

bool AudioCallback::startCalibration() noexcept
{
    if (capture_.empty())
        return false;
    auto expected = CalibrationPhase::idle;
    return calibrationPhase_.compare_exchange_strong(
        expected, CalibrationPhase::running, std::memory_order_acq_rel);
}

void AudioCallback::requestCalibrationAbort() noexcept
{
    abortRequested_.store(true, std::memory_order_release);
}

std::span<const float> AudioCallback::calibrationCapture() const noexcept
{
    if (calibrationPhase() != CalibrationPhase::done)
        return {};
    return capture_;
}

void AudioCallback::finishCalibration() noexcept
{
    auto expected = CalibrationPhase::done;
    calibrationPhase_.compare_exchange_strong(expected, CalibrationPhase::idle, std::memory_order_acq_rel);
}

// ----- device callback --------------------------------------------------------------

void AudioCallback::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioCallback::audioDeviceAboutToStart");
    numInputs_.store(device->getActiveInputChannels().countNumberOfSetBits(), std::memory_order_relaxed);
    numOutputs_.store(device->getActiveOutputChannels().countNumberOfSetBits(), std::memory_order_relaxed);
    sampleRate_.store(device->getCurrentSampleRate(), std::memory_order_relaxed);
    currentGain_ = targetGain_.load(std::memory_order_relaxed);
    secondsPerSample_ = device->getCurrentSampleRate() > 0.0 ? 1.0 / device->getCurrentSampleRate() : 0.0;
    stats_.reset();
    inputPeak_.store(0.0f, std::memory_order_relaxed);
    outputPeak_.store(0.0f, std::memory_order_relaxed);
    // AudioDeviceManager splits oversized blocks, so the device buffer size bounds
    // numSamples.
    if (auto* engine = engine_.load(std::memory_order_acquire))
        engine->prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    running_.store(true, std::memory_order_release);
}

void AudioCallback::audioDeviceStopped()
{
    // Called after the device thread has stopped, so no callback runs concurrently:
    // a calibration in flight is simply dropped.
    running_.store(false, std::memory_order_release);
    if (auto* engine = engine_.load(std::memory_order_acquire))
        engine->release();
    calibrationPhase_.store(CalibrationPhase::idle, std::memory_order_release);
    abortRequested_.store(false, std::memory_order_relaxed);
    numInputs_.store(0, std::memory_order_relaxed);
    numOutputs_.store(0, std::memory_order_relaxed);
}

void AudioCallback::audioDeviceError(const juce::String& /*errorMessage*/)
{
    // May arrive on the device thread; just raise a flag for the message thread.
    errorFlag_.store(true, std::memory_order_release);
}

void AudioCallback::audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                                     int numInputChannels,
                                                     float* const* outputChannelData,
                                                     int numOutputChannels,
                                                     int numSamples,
                                                     const juce::AudioIODeviceCallbackContext& /*context*/)
{
    rt::ScopedRealtimeSection realtime;
    juce::ScopedNoDenormals noDenormals; // FTZ/DAZ for the rest of the callback

    const auto startedAt = std::chrono::steady_clock::now();
    callbackCount_.fetch_add(1, std::memory_order_relaxed);

    auto phase = calibrationPhase_.load(std::memory_order_acquire);
    if (phase == CalibrationPhase::running && abortRequested_.exchange(false, std::memory_order_acq_rel))
    {
        calibrationPhase_.store(CalibrationPhase::idle, std::memory_order_release);
        phase = CalibrationPhase::idle;
    }

    raisePeak(inputPeak_, blockPeak(inputChannelData, numInputChannels, numSamples));

    if (phase == CalibrationPhase::running)
    {
        // Pass-through is muted during calibration: with a physical loopback it would
        // feed the output straight back into the input and howl.
        renderCalibration(
            inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
    }
    else
    {
        const float gainEnd = targetGain_.load(std::memory_order_relaxed);
        const float gainStart = currentGain_;
        currentGain_ = gainEnd;

        if (auto* engine = engine_.load(std::memory_order_acquire))
            engine->process(
                inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
        else
            renderSilence(outputChannelData, numOutputChannels, numSamples);

        if (passThrough_.load(std::memory_order_relaxed))
            renderPassThrough(inputChannelData,
                              numInputChannels,
                              outputChannelData,
                              numOutputChannels,
                              numSamples,
                              gainStart,
                              gainEnd,
                              true);
    }

    if (outputMuted_.load(std::memory_order_relaxed))
        renderSilence(outputChannelData, numOutputChannels, numSamples);

    raisePeak(outputPeak_, blockPeak(outputChannelData, numOutputChannels, numSamples));

    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count();
    const double deadline = secondsPerSample_ * numSamples;
    if (deadline > 0.0)
        stats_.record(elapsed / deadline);
}

void AudioCallback::renderCalibration(
    const float* const* inputs, int numInputs, float* const* outputs, int numOutputs, int numSamples) noexcept
{
    renderSilence(outputs, numOutputs, numSamples);

    const float* in = plan_.inputChannel < numInputs ? inputs[plan_.inputChannel] : nullptr;
    float* out = plan_.outputChannel < numOutputs ? outputs[plan_.outputChannel] : nullptr;
    const int captureLength = plan_.captureLength;
    const int referenceLength = static_cast<int>(plan_.reference.size());

    for (int i = 0; i < numSamples; ++i)
    {
        const int index = capturePosition_ + i;
        if (index >= captureLength)
            break;

        capture_[static_cast<size_t>(index)] = in != nullptr ? in[i] : 0.0f;

        const int r = index - plan_.emitSample;
        if (out != nullptr && r >= 0 && r < referenceLength)
            out[i] = plan_.reference[static_cast<size_t>(r)] * plan_.level;
    }

    capturePosition_ += numSamples;
    if (capturePosition_ >= captureLength)
        calibrationPhase_.store(CalibrationPhase::done, std::memory_order_release);
}

} // namespace northlight::audio
