#pragma once

#include "engine/AudioEngine.h"
#include "engine/OutputTap.h"
#include "engine/rt/CallbackStats.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

// The audio device callback. Renders the engine (when one is attached), adds the
// Phase 1 input pass-through on top for monitoring, and hosts the loopback
// calibration capture. The entry discipline:
//
//   1. enter northlight::rt::ScopedRealtimeSection (allocation hook becomes live)
//   2. set FTZ/DAZ (juce::ScopedNoDenormals)
//   3. only then touch audio
//
// Everything the callback reads from other threads is an atomic or a buffer whose
// ownership is handed over through the calibration phase state machine below. The
// callback never allocates, locks or posts messages.

namespace northlight::audio
{

class AudioCallback final : public juce::AudioIODeviceCallback
{
public:
    AudioCallback();
    ~AudioCallback() override;

    // ----- control (any thread) -------------------------------------------------
    // Attaches the engine. Message thread, before the device starts; the engine must
    // outlive the callback. The callback calls prepare()/release() around device
    // start/stop and process() from every block.
    void setEngine(engine::AudioEngine* engine) noexcept;

    void setMasterGain(float linearGain) noexcept;
    float masterGain() const noexcept;
    void setPassThroughEnabled(bool enabled) noexcept;
    bool isPassThroughEnabled() const noexcept;

    // ----- observation (any thread) ---------------------------------------------
    std::uint64_t callbackCount() const noexcept;
    bool isDeviceRunning() const noexcept;
    int numActiveInputs() const noexcept;
    int numActiveOutputs() const noexcept;
    double currentSampleRate() const noexcept;

    // Peak absolute sample since the last call; the caller resets by reading.
    float takeInputPeak() noexcept;
    float takeOutputPeak() noexcept;

    // Set by audioDeviceError; cleared by the reader.
    bool takeErrorFlag() noexcept;

    // Callback time as a fraction of each block's deadline, and how late each
    // callback started; reset when the device starts.
    const rt::CallbackStats& stats() const noexcept { return stats_; }

    // Hands every finished output block, with its timing, to the tap (--output-tap).
    // Message thread; the tap must outlive the device or be removed (nullptr) while
    // the device is stopped.
    void setOutputTap(engine::OutputTap* tap) noexcept { tap_.store(tap, std::memory_order_release); }
    engine::OutputTap* outputTap() const noexcept { return tap_.load(std::memory_order_acquire); }

    // ----- loopback calibration ---------------------------------------------------
    // Driven from the message thread by LatencyCalibrator. The phase is a tiny state
    // machine that hands the capture buffer between the two threads:
    //
    //   idle    -> (prepare + start, message thread) -> running
    //   running -> (capture full, audio thread)       -> done
    //   running -> (abort requested, audio thread)    -> idle
    //   done    -> (finish, message thread)           -> idle
    //   any     -> (device stopped)                   -> idle
    //
    // The message thread touches the buffers only in idle or done; the audio thread
    // only in running.
    enum class CalibrationPhase
    {
        idle,
        running,
        done,
    };

    struct CalibrationPlan
    {
        std::vector<float> reference; // test signal, written to outputChannel from emitSample
        int captureLength = 0;        // samples to record from inputChannel
        int emitSample = 0;           // output position at which the reference starts
        int inputChannel = 0;         // index into the active input channels
        int outputChannel = 0;        // index into the active output channels
        float level = 0.5f;           // linear gain applied to the reference
    };

    CalibrationPhase calibrationPhase() const noexcept;

    // Only valid in idle. Allocates (message thread).
    bool prepareCalibration(CalibrationPlan plan);
    // idle -> running. Returns false if not idle.
    bool startCalibration() noexcept;
    // Asks the audio thread to drop a running calibration at its next block.
    void requestCalibrationAbort() noexcept;
    // Only valid in done: the recorded input, captureLength samples long.
    std::span<const float> calibrationCapture() const noexcept;
    const CalibrationPlan& calibrationPlan() const noexcept { return plan_; }
    // done -> idle.
    void finishCalibration() noexcept;

    // Sends silence to the device while the engine still runs, so every meter and
    // display keeps moving (--capture plays the song without it being heard).
    void setOutputMuted(bool muted) noexcept { outputMuted_.store(muted, std::memory_order_relaxed); }

    // ----- juce::AudioIODeviceCallback --------------------------------------------
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String& errorMessage) override;

private:
    void renderCalibration(const float* const* inputs,
                           int numInputs,
                           float* const* outputs,
                           int numOutputs,
                           int numSamples) noexcept;

    std::atomic<engine::AudioEngine*> engine_{nullptr};
    std::atomic<float> targetGain_{1.0f};
    float currentGain_ = 1.0f; // audio thread only
    std::atomic<bool> passThrough_{false};
    std::atomic<bool> outputMuted_{false};
    std::atomic<engine::OutputTap*> tap_{nullptr};

    std::atomic<std::uint64_t> callbackCount_{0};
    rt::CallbackStats stats_;
    double secondsPerSample_ = 0.0; // audio thread only
    std::atomic<bool> running_{false};
    std::atomic<int> numInputs_{0};
    std::atomic<int> numOutputs_{0};
    std::atomic<double> sampleRate_{0.0};
    std::atomic<float> inputPeak_{0.0f};
    std::atomic<float> outputPeak_{0.0f};
    std::atomic<bool> errorFlag_{false};

    std::atomic<CalibrationPhase> calibrationPhase_{CalibrationPhase::idle};
    std::atomic<bool> abortRequested_{false};
    CalibrationPlan plan_;
    std::vector<float> capture_;
    int capturePosition_ = 0; // audio thread while running

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioCallback)
};

} // namespace northlight::audio
