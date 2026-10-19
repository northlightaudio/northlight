#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

// User-chosen audio device configuration and the persisted loopback calibration.
//
// This is a plain value type with no JUCE dependency so it can be unit tested and so
// the app's settings store decides how it is written to disk. Serialisation is a flat
// string map; the app maps that onto its properties file.

namespace aural::audio
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
    bool passThroughEnabled = true;

    std::optional<LatencyCalibration> calibration;

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

} // namespace aural::audio
