#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// User-chosen audio device configuration and the persisted loopback calibration.
//
// This is a plain value type with no JUCE dependency so it can be unit tested and so
// the app's settings store decides how it is written to disk. Serialisation is a flat
// string map; the app maps that onto its properties file.

namespace northlight::audio
{

// Result of the loopback latency calibration, stored with enough context to tell
// when it is stale (different device, rate or buffer size).
struct LatencyCalibration
{
    int roundTripSamples = 0;
    double sampleRate = 0.0;
    int bufferSize = 0;
    std::string deviceType;
    std::string deviceName;

    double roundTripMilliseconds() const noexcept
    {
        return sampleRate > 0.0 ? 1000.0 * static_cast<double>(roundTripSamples) / sampleRate : 0.0;
    }

    bool operator==(const LatencyCalibration&) const = default;
};

// A stereo input made of two hardware inputs of one device, which tracks choose by
// name (like Cubase's input buses). Channels are device channel indices, as in
// inputChannelNames(), not positions among the enabled channels, so enabling
// another channel does not move the bus.
struct InputBus
{
    std::string deviceType;
    std::string deviceName; // the input device
    std::string name;
    int left = -1;
    int right = -1;

    bool operator==(const InputBus&) const = default;
};

// Where each bus's two channels sit among the device's active inputs (callback
// order, as in AudioDeviceLayer::Status::activeInputs), by bus name. A bus whose
// channels are not both enabled is left out; the first of two equal names wins.
std::map<std::string, std::pair<int, int>> resolveInputBuses(const std::vector<InputBus>& buses,
                                                             const std::vector<int>& activeInputs);

struct AudioSettings
{
    using KeyValues = std::map<std::string, std::string>;

    // Device selection. Empty strings mean "driver default".
    std::string deviceType;
    std::string outputDevice;
    std::string inputDevice;

    // 0 means "device default".
    double sampleRate = 0.0;
    int bufferSize = 0;

    // Enabled channel indices on the device. std::nullopt means "device default"; an
    // empty vector means the user disabled every channel.
    std::optional<std::vector<int>> inputChannels;
    std::optional<std::vector<int>> outputChannels;

    // Pass-through monitoring (Phase 1 only; replaced by the mixer in Phase 4).
    float masterGain = 1.0f; // linear
    // Off by default: with a microphone on an input and speakers on the output,
    // pass-through on first launch is immediate feedback. The user opts in from
    // Audio Setup; tracks monitor through their own monitor mode instead.
    bool passThroughEnabled = false;

    std::optional<LatencyCalibration> calibration;

    // MIDI: the input device names that are enabled (nullopt = every input), and
    // the output device name (empty = none).
    std::optional<std::vector<std::string>> midiInputs;
    std::string midiOutput;

    // Stereo input buses of every device the user has set up, kept when they
    // switch to another device so they come back with it.
    std::vector<InputBus> inputBuses;

    // The buses of one input device, in the order the user made them.
    std::vector<InputBus> inputBusesFor(const std::string& type, const std::string& device) const;
    // Replaces that device's buses, keeping every other device's.
    void setInputBusesFor(const std::string& type, const std::string& device, std::vector<InputBus> buses);

    // True when the stored calibration was made with the given device configuration.
    bool calibrationMatches(const std::string& type,
                            const std::string& device,
                            double rate,
                            int bufferSizeSamples) const noexcept;

    KeyValues toKeyValues() const;
    static AudioSettings fromKeyValues(const KeyValues& values);

    bool operator==(const AudioSettings&) const = default;
};

// Interface implemented by the application's settings store.
class AudioSettingsStore
{
public:
    virtual ~AudioSettingsStore() = default;
    virtual AudioSettings loadAudioSettings() = 0;
    virtual void saveAudioSettings(const AudioSettings& settings) = 0;
};

} // namespace northlight::audio
