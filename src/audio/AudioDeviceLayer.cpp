#include "audio/AudioDeviceLayer.h"

#include "engine/rt/RealtimeGuard.h"

namespace northlight::audio
{

namespace
{
constexpr int healthTimerMilliseconds = 1000;
constexpr int recoveryIntervalTicks = 2; // health ticks between reopen attempts
constexpr int recoveryAttemptsBeforeDefault = 5;
constexpr int defaultChannelsNeeded = 2;

std::string toStd(const juce::String& s)
{
    return s.toStdString();
}

juce::String toJuce(const std::string& s)
{
    return juce::String(s);
}

std::vector<int> setBits(const juce::BigInteger& bits)
{
    std::vector<int> out;
    for (int i = bits.findNextSetBit(0); i >= 0; i = bits.findNextSetBit(i + 1))
        out.push_back(i);
    return out;
}

juce::BigInteger toBits(const std::vector<int>& channels)
{
    juce::BigInteger bits;
    for (int c : channels)
        if (c >= 0)
            bits.setBit(c);
    return bits;
}

std::vector<std::string> toStdVector(const juce::StringArray& names)
{
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(names.size()));
    for (const auto& n : names)
        out.push_back(toStd(n));
    return out;
}

// Builds the DEVICESETUP element that AudioDeviceManager::initialise understands, so
// device type, names, rate, buffer size and channels are applied in one open.
std::unique_ptr<juce::XmlElement> makeSetupXml(const AudioSettings& s)
{
    auto xml = std::make_unique<juce::XmlElement>("DEVICESETUP");
    xml->setAttribute("deviceType", toJuce(s.deviceType));
    xml->setAttribute("audioOutputDeviceName", toJuce(s.outputDevice));
    xml->setAttribute("audioInputDeviceName", toJuce(s.inputDevice));
    if (s.sampleRate > 0.0)
        xml->setAttribute("audioDeviceRate", s.sampleRate);
    if (s.bufferSize > 0)
        xml->setAttribute("audioDeviceBufferSize", s.bufferSize);
    if (s.inputChannels.has_value())
        xml->setAttribute("audioDeviceInChans", toBits(*s.inputChannels).toString(2));
    if (s.outputChannels.has_value())
        xml->setAttribute("audioDeviceOutChans", toBits(*s.outputChannels).toString(2));
    return xml;
}
} // namespace

double AudioDeviceLayer::Status::inputLatencyMilliseconds() const noexcept
{
    return sampleRate > 0.0 ? 1000.0 * inputLatencySamples / sampleRate : 0.0;
}

double AudioDeviceLayer::Status::outputLatencyMilliseconds() const noexcept
{
    return sampleRate > 0.0 ? 1000.0 * outputLatencySamples / sampleRate : 0.0;
}

AudioDeviceLayer::AudioDeviceLayer(AudioSettingsStore& store) : store_(store), calibrator_(callback_)
{
    manager_.addChangeListener(this);
    midi_.onChanged = [this]
    {
        settings_.midiInputs = midi_.enabledInputNames();
        settings_.midiOutput = midi_.outputName();
        save();
        sendChangeMessage();
    };
    midi_.addChangeListener(this);
}

AudioDeviceLayer::~AudioDeviceLayer()
{
    shutdown();
    midi_.removeChangeListener(this);
    manager_.removeChangeListener(this);
}

// ----- lifecycle ------------------------------------------------------------------------

void AudioDeviceLayer::initialise()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioDeviceLayer::initialise");
    JUCE_ASSERT_MESSAGE_THREAD;

    settings_ = store_.loadAudioSettings();
    callback_.setMasterGain(settings_.masterGain);
    callback_.setPassThroughEnabled(settings_.passThroughEnabled);
    manager_.addAudioCallback(&callback_);

    juce::String error;
    if (settings_.deviceType.empty() && settings_.outputDevice.empty() && settings_.inputDevice.empty())
    {
        error = openDefault();
    }
    else
    {
        error = open(settings_);
        if (error.isNotEmpty())
        {
            const auto preferred = error;
            error = openDefault();
            setMessage("Could not open the saved audio device (" + toStd(preferred) +
                       "); using the system default.");
        }
    }

    if (error.isNotEmpty())
        setMessage("No audio device could be opened: " + toStd(error));

    initialised_ = true;
    hadDevice_ = manager_.getCurrentAudioDevice() != nullptr;
    midi_.initialise(settings_.midiInputs, settings_.midiOutput);
    startTimer(healthTimerMilliseconds);
    sendChangeMessage();
}

void AudioDeviceLayer::shutdown()
{
    if (!initialised_)
        return;
    stopTimer();
    calibrator_.cancel();
    midi_.shutdown();
    manager_.removeAudioCallback(&callback_);
    manager_.closeAudioDevice();
    initialised_ = false;
}

// ----- enumeration ----------------------------------------------------------------------

std::vector<AudioDeviceLayer::DeviceType> AudioDeviceLayer::deviceTypes()
{
    std::vector<DeviceType> out;
    for (auto* type : manager_.getAvailableDeviceTypes())
        out.push_back({toStd(type->getTypeName()), type->hasSeparateInputsAndOutputs()});
    return out;
}

AudioDeviceLayer::DeviceNames AudioDeviceLayer::deviceNames(const std::string& typeName)
{
    DeviceNames names;
    for (auto* type : manager_.getAvailableDeviceTypes())
    {
        if (toStd(type->getTypeName()) != typeName)
            continue;
        type->scanForDevices();
        names.outputs = toStdVector(type->getDeviceNames(false));
        names.inputs = toStdVector(type->getDeviceNames(true));
        names.defaultOutput = type->getDefaultDeviceIndex(false);
        names.defaultInput = type->getDefaultDeviceIndex(true);
        break;
    }
    return names;
}

std::vector<double> AudioDeviceLayer::availableSampleRates() const
{
    std::vector<double> out;
    if (auto* device = manager_.getCurrentAudioDevice())
        for (double rate : device->getAvailableSampleRates())
            out.push_back(rate);
    return out;
}

std::vector<int> AudioDeviceLayer::availableBufferSizes() const
{
    std::vector<int> out;
    if (auto* device = manager_.getCurrentAudioDevice())
        for (int size : device->getAvailableBufferSizes())
            out.push_back(size);
    return out;
}

std::vector<std::string> AudioDeviceLayer::inputChannelNames() const
{
    if (auto* device = manager_.getCurrentAudioDevice())
        return toStdVector(device->getInputChannelNames());
    return {};
}

std::vector<std::string> AudioDeviceLayer::outputChannelNames() const
{
    if (auto* device = manager_.getCurrentAudioDevice())
        return toStdVector(device->getOutputChannelNames());
    return {};
}

// ----- state ----------------------------------------------------------------------------

AudioDeviceLayer::Status AudioDeviceLayer::status() const
{
    Status s;
    s.message = message_;
    s.deviceType = toStd(manager_.getCurrentAudioDeviceType());

    auto* device = manager_.getCurrentAudioDevice();
    if (device == nullptr)
        return s;

    // The device's own count, not AudioDeviceManager's: that one turns "cannot
    // report" into 0 and adds its own callback-time overruns, which CallbackStats
    // already counts.
    s.driverXruns = device->getXRunCount();
    const auto setup = manager_.getAudioDeviceSetup();
    s.open = device->isOpen();
    s.running = device->isPlaying();
    s.outputDevice = toStd(setup.outputDeviceName);
    s.inputDevice = toStd(setup.inputDeviceName);
    s.sampleRate = device->getCurrentSampleRate();
    s.bufferSize = device->getCurrentBufferSizeSamples();
    s.inputLatencySamples = device->getInputLatencyInSamples();
    s.outputLatencySamples = device->getOutputLatencyInSamples();
    s.activeInputs = setBits(device->getActiveInputChannels());
    s.activeOutputs = setBits(device->getActiveOutputChannels());
    s.hasControlPanel = device->hasControlPanel();
    return s;
}

std::optional<LatencyCalibration> AudioDeviceLayer::currentCalibration() const
{
    const auto s = status();
    if (!s.open)
        return std::nullopt;
    if (settings_.calibrationMatches(s.deviceType, s.outputDevice, s.sampleRate, s.bufferSize))
        return settings_.calibration;
    return std::nullopt;
}

// ----- changes --------------------------------------------------------------------------

std::optional<std::string> AudioDeviceLayer::apply(const AudioSettings& requested)
{
    return apply(requested, true);
}

std::optional<std::string> AudioDeviceLayer::applyForThisRun(const AudioSettings& requested)
{
    return apply(requested, false);
}

std::optional<std::string> AudioDeviceLayer::apply(const AudioSettings& requested, bool persist)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("AudioDeviceLayer::apply");
    JUCE_ASSERT_MESSAGE_THREAD;

    calibrator_.cancel();

    const auto previous = settings_;
    const auto error = open(requested);
    if (error.isNotEmpty())
    {
        // Put the previous device back so the user is not left in silence.
        if (manager_.getCurrentAudioDevice() == nullptr)
        {
            if (open(previous).isNotEmpty())
                openDefault();
        }
        setMessage(toStd(error));
        sendChangeMessage();
        return toStd(error);
    }

    setMessage({});
    recoveryAttempts_ = 0;
    hadDevice_ = true;
    if (!persist)
    {
        // settings_ keeps what was saved, so later saves do not store this run's device.
        sendChangeMessage();
        return std::nullopt;
    }

    settings_ = requested;
    settings_.masterGain = callback_.masterGain();
    settings_.passThroughEnabled = callback_.isPassThroughEnabled();
    // Calibration survives only for the configuration it was measured with.
    if (previous.calibration.has_value())
        settings_.calibration = previous.calibration;
    save();
    sendChangeMessage();
    return std::nullopt;
}

void AudioDeviceLayer::setMasterGain(float linearGain)
{
    callback_.setMasterGain(linearGain);
    settings_.masterGain = callback_.masterGain();
    save();
}

void AudioDeviceLayer::setPassThroughEnabled(bool enabled)
{
    callback_.setPassThroughEnabled(enabled);
    settings_.passThroughEnabled = enabled;
    save();
}

void AudioDeviceLayer::storeCalibration(const LatencyMeasurement& measurement)
{
    const auto s = status();
    if (!s.open)
        return;
    LatencyCalibration c;
    c.roundTripSamples = measurement.roundTripSamples;
    c.sampleRate = s.sampleRate;
    c.bufferSize = s.bufferSize;
    c.deviceType = s.deviceType;
    c.deviceName = s.outputDevice;
    settings_.calibration = c;
    save();
    sendChangeMessage();
}

void AudioDeviceLayer::clearCalibration()
{
    settings_.calibration.reset();
    save();
    sendChangeMessage();
}

std::vector<InputBus> AudioDeviceLayer::inputBuses() const
{
    const auto s = status();
    if (!s.open)
        return {};
    return settings_.inputBusesFor(s.deviceType, s.inputDevice);
}

void AudioDeviceLayer::setInputBuses(std::vector<InputBus> buses)
{
    const auto s = status();
    if (!s.open)
        return;
    settings_.setInputBusesFor(s.deviceType, s.inputDevice, std::move(buses));
    save();
    sendChangeMessage();
}

bool AudioDeviceLayer::showControlPanel()
{
    JUCE_ASSERT_MESSAGE_THREAD;
    auto* device = manager_.getCurrentAudioDevice();
    if (device == nullptr || !device->hasControlPanel())
        return false;
    calibrator_.cancel();
    // ASIO drivers may request a reset from inside the panel; JUCE's ASIO device
    // restarts itself on a timer and we see it as a change message.
    return device->showControlPanel();
}

// ----- internals ------------------------------------------------------------------------

juce::String AudioDeviceLayer::open(const AudioSettings& s)
{
    const auto xml = makeSetupXml(s);
    return manager_.initialise(defaultChannelsNeeded, defaultChannelsNeeded, xml.get(), false);
}

juce::String AudioDeviceLayer::openDefault()
{
    return manager_.initialiseWithDefaultDevices(defaultChannelsNeeded, defaultChannelsNeeded);
}

void AudioDeviceLayer::save()
{
    store_.saveAudioSettings(settings_);
}

void AudioDeviceLayer::setMessage(std::string message)
{
    message_ = std::move(message);
}

void AudioDeviceLayer::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &midi_)
    {
        sendChangeMessage(); // a MIDI device came or went: listeners re-read the lists
        return;
    }
    // AudioDeviceManager sends this on device open/close, device-list changes and
    // driver-initiated restarts (ASIO reset requests, sample-rate changes).
    if (initialised_ && manager_.getCurrentAudioDevice() == nullptr && hadDevice_)
    {
        handleDeviceLost();
    }
    else if (initialised_ && manager_.getCurrentAudioDevice() != nullptr && !settings_.outputDevice.empty())
    {
        // AudioDeviceManager may have reopened a different device on its own when the
        // list changed; say so rather than silently showing the wrong name.
        const auto s = status();
        if (s.deviceType != settings_.deviceType || s.outputDevice != settings_.outputDevice)
            setMessage("The preferred audio device is unavailable; using " + s.outputDevice + ".");
    }
    sendChangeMessage();
}

void AudioDeviceLayer::handleDeviceLost()
{
    hadDevice_ = false;
    calibrator_.cancel();
    recoveryAttempts_ = 0;
    recoveryAttemptsBeforeFallback_ = recoveryAttemptsBeforeDefault;
    setMessage("The audio device was lost. Waiting for it to return...");
}

void AudioDeviceLayer::timerCallback()
{
    if (callback_.takeErrorFlag())
    {
        setMessage("The audio driver reported an error; restarting the device.");
        manager_.closeAudioDevice();
        if (open(settings_).isNotEmpty())
            handleDeviceLost();
        else
            setMessage({});
        sendChangeMessage();
        return;
    }

    if (manager_.getCurrentAudioDevice() != nullptr)
    {
        if (!hadDevice_)
        {
            // A fallback or recovered device is up.
            hadDevice_ = true;
            sendChangeMessage();
        }
        return;
    }

    // No device: keep trying the preferred one, then fall back to the system default
    // while continuing to wait for the preferred device to return.
    if (++recoveryAttempts_ % recoveryIntervalTicks != 0)
        return;

    if (open(settings_).isEmpty())
    {
        hadDevice_ = true;
        setMessage({});
        sendChangeMessage();
        return;
    }

    if (recoveryAttempts_ / recoveryIntervalTicks >= recoveryAttemptsBeforeFallback_)
    {
        recoveryAttemptsBeforeFallback_ += recoveryAttemptsBeforeDefault;
        if (openDefault().isEmpty())
        {
            hadDevice_ = true;
            setMessage("The preferred audio device is unavailable; using the system default.");
            sendChangeMessage();
        }
    }
}

} // namespace northlight::audio
