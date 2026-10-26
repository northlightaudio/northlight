#pragma once

#include "audio/AudioCallback.h"
#include "audio/AudioSettings.h"
#include "audio/LatencyCalibrator.h"
#include "audio/LatencyDetector.h"
#include "audio/MidiDeviceLayer.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <optional>
#include <string>
#include <vector>

// The audio device layer: a thin, message-thread-only wrapper around
// juce::AudioDeviceManager that
//
//   - enumerates ASIO and WASAPI device types and their devices,
//   - opens a device with a chosen sample rate, buffer size and channel set,
//   - persists what the user chose through AudioSettingsStore,
//   - reports driver latency and run-time status,
//   - recovers from device loss (unplugged interface, driver reset) by reopening the
//     preferred device when it returns and falling back to the system default
//     meanwhile,
//   - owns the real-time AudioCallback, the LatencyCalibrator and the MIDI
//     device layer (whose device choices are persisted with the audio settings).
//
// Listeners (the Audio Setup screen) register as juce::ChangeListener and re-read
// status() whenever anything changes.

namespace northlight::audio
{

class AudioDeviceLayer final : public juce::ChangeBroadcaster,
                               private juce::ChangeListener,
                               private juce::Timer
{
public:
    explicit AudioDeviceLayer(AudioSettingsStore& store);
    ~AudioDeviceLayer() override;

    // Loads the saved settings and opens that device, or the system default when
    // nothing is saved or the saved device is unavailable.
    void initialise();
    // Closes the device. Settings are saved as they change, not here.
    void shutdown();

    // ----- enumeration ------------------------------------------------------------
    struct DeviceType
    {
        std::string name;
        bool separateInputsAndOutputs = false;
    };
    std::vector<DeviceType> deviceTypes();

    struct DeviceNames
    {
        std::vector<std::string> outputs;
        std::vector<std::string> inputs; // same as outputs for ASIO
        int defaultOutput = -1;
        int defaultInput = -1;
    };
    DeviceNames deviceNames(const std::string& type);

    // Capabilities of the device that is currently open (empty when none is).
    std::vector<double> availableSampleRates() const;
    std::vector<int> availableBufferSizes() const;
    // The buffer sizes a picker offers: the open device's own (the usual 32 to 1024
    // when it reports none, or none is open), always with the size it runs at; sorted.
    std::vector<int> bufferSizeChoices() const;
    // `settings` with the driver `type` and the output `output`; the input is the
    // same device, or for a driver with separate inputs its default input.
    static AudioSettings withDevice(AudioSettings settings,
                                    const DeviceType& type,
                                    const DeviceNames& names,
                                    const std::string& output);
    // Where `name` is in `names`; -1 when it is not there.
    static int indexOf(const std::vector<std::string>& names, const std::string& name);
    std::vector<std::string> inputChannelNames() const;
    std::vector<std::string> outputChannelNames() const;

    // ----- state --------------------------------------------------------------------
    struct Status
    {
        bool open = false;
        bool running = false;
        std::string deviceType;
        std::string outputDevice;
        std::string inputDevice;
        double sampleRate = 0.0;
        int bufferSize = 0;
        int inputLatencySamples = 0;
        int outputLatencySamples = 0;
        std::vector<int> activeInputs;  // device channel indices, in callback order
        std::vector<int> activeOutputs; // device channel indices, in callback order
        bool hasControlPanel = false;
        // Dropouts the driver itself reported, or -1 when it cannot report them
        // (most ASIO drivers). The callback's own overruns and late starts are in
        // AudioCallback::stats().
        int driverXruns = -1;
        std::string message; // last error or recovery note, empty when all is well

        double inputLatencyMilliseconds() const noexcept;
        double outputLatencyMilliseconds() const noexcept;
    };
    Status status() const;

    // What the user asked for (persisted). May differ from status() after a fallback.
    // The master gain and pass-through in it are the callback's, which holds them.
    AudioSettings settings() const;

    // The stored calibration, if it was made with the currently open configuration.
    std::optional<LatencyCalibration> currentCalibration() const;

    // ----- changes ----------------------------------------------------------------
    // Opens the device described by `settings`. On success the settings are saved and
    // nullopt is returned; on failure the error is returned and the previous device is
    // reopened where possible.
    std::optional<std::string> apply(const AudioSettings& settings);
    // The same without saving: the device runs with `settings` until the app quits,
    // and the saved settings stay as they were (--buffer-size).
    std::optional<std::string> applyForThisRun(const AudioSettings& settings);

    void setMasterGain(float linearGain);
    void setPassThroughEnabled(bool enabled);
    void storeCalibration(const LatencyMeasurement& measurement);
    void clearCalibration();

    // The stereo input buses of the open input device (empty when none is open),
    // and replacing them. Setting them saves and broadcasts a change.
    std::vector<InputBus> inputBuses() const;
    void setInputBuses(std::vector<InputBus> buses);

    bool showControlPanel();

    // ----- collaborators --------------------------------------------------------
    AudioCallback& callback() noexcept { return callback_; }
    MidiDeviceLayer& midi() noexcept { return midi_; }
    LatencyCalibrator& calibrator() noexcept { return calibrator_; }
    juce::AudioDeviceManager& manager() noexcept { return manager_; }

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    juce::String open(const AudioSettings& settings);
    std::optional<std::string> apply(const AudioSettings& settings, bool persist);
    juce::String openDefault();
    void save();
    void setMessage(std::string message);
    void handleDeviceLost();

    AudioSettingsStore& store_;
    juce::AudioDeviceManager manager_;
    AudioCallback callback_;
    MidiDeviceLayer midi_;
    LatencyCalibrator calibrator_;
    AudioSettings settings_; // its masterGain and passThroughEnabled are unused: callback_ holds them
    std::string message_;
    bool initialised_ = false;
    bool hadDevice_ = false;
    int recoveryAttempts_ = 0;
    int recoveryAttemptsBeforeFallback_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDeviceLayer)
};

} // namespace northlight::audio
