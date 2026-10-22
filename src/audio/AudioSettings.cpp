#include "audio/AudioSettings.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <sstream>

namespace northlight::audio
{

namespace
{
constexpr const char* keyDeviceType = "audio.deviceType";
constexpr const char* keyOutputDevice = "audio.outputDevice";
constexpr const char* keyInputDevice = "audio.inputDevice";
constexpr const char* keySampleRate = "audio.sampleRate";
constexpr const char* keyBufferSize = "audio.bufferSize";
constexpr const char* keyInputChannels = "audio.inputChannels";
constexpr const char* keyOutputChannels = "audio.outputChannels";
constexpr const char* keyMasterGain = "audio.masterGain";
constexpr const char* keyPassThrough = "audio.passThrough";
constexpr const char* keyCalSamples = "audio.calibration.roundTripSamples";
constexpr const char* keyCalRate = "audio.calibration.sampleRate";
constexpr const char* keyCalBuffer = "audio.calibration.bufferSize";
constexpr const char* keyCalType = "audio.calibration.deviceType";
constexpr const char* keyCalDevice = "audio.calibration.deviceName";
constexpr const char* keyMidiInputs = "midi.inputs";
constexpr const char* keyMidiOutput = "midi.output";
// Input buses: a count, then one group of keys per bus ("audio.inputBus.0.name"...).
constexpr const char* keyInputBusCount = "audio.inputBus.count";
const std::string inputBusPrefix = "audio.inputBus.";
constexpr char nameSeparator = '\t'; // device names never contain a tab

std::string joinNames(const std::vector<std::string>& names)
{
    std::string out;
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i > 0)
            out += nameSeparator;
        out += names[i];
    }
    return out;
}

std::vector<std::string> splitNames(const std::string& text)
{
    std::vector<std::string> out;
    if (text.empty())
        return out;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, nameSeparator))
        if (!item.empty())
            out.push_back(item);
    return out;
}

std::string joinChannels(const std::vector<int>& channels)
{
    std::string out;
    for (size_t i = 0; i < channels.size(); ++i)
    {
        if (i > 0)
            out += ',';
        out += std::to_string(channels[i]);
    }
    return out;
}

std::vector<int> splitChannels(const std::string& text)
{
    std::vector<int> out;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ','))
    {
        int value = 0;
        const char* begin = item.data();
        const char* end = begin + item.size();
        while (begin != end && *begin == ' ')
            ++begin;
        if (std::from_chars(begin, end, value).ec == std::errc{} && value >= 0)
            out.push_back(value);
    }
    return out;
}

const std::string* find(const AudioSettings::KeyValues& values, const char* key)
{
    const auto it = values.find(key);
    return it == values.end() ? nullptr : &it->second;
}

std::string getString(const AudioSettings::KeyValues& values, const char* key)
{
    const auto* s = find(values, key);
    return s != nullptr ? *s : std::string{};
}

int getInt(const AudioSettings::KeyValues& values, const char* key, int fallback)
{
    const auto* s = find(values, key);
    if (s == nullptr)
        return fallback;
    int value = 0;
    const auto result = std::from_chars(s->data(), s->data() + s->size(), value);
    return result.ec == std::errc{} ? value : fallback;
}

double getDouble(const AudioSettings::KeyValues& values, const char* key, double fallback)
{
    const auto* s = find(values, key);
    if (s == nullptr || s->empty())
        return fallback;
    char* end = nullptr;
    const double value = std::strtod(s->c_str(), &end);
    return (end != nullptr && end != s->c_str()) ? value : fallback;
}

bool getBool(const AudioSettings::KeyValues& values, const char* key, bool fallback)
{
    const auto* s = find(values, key);
    if (s == nullptr)
        return fallback;
    if (*s == "1" || *s == "true")
        return true;
    if (*s == "0" || *s == "false")
        return false;
    return fallback;
}

std::string formatDouble(double value)
{
    // Shortest round-trippable representation, independent of the C locale.
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}
} // namespace

bool AudioSettings::calibrationMatches(const std::string& type,
                                       const std::string& device,
                                       double rate,
                                       int bufferSizeSamples) const noexcept
{
    if (!calibration.has_value())
        return false;
    const auto& c = *calibration;
    return c.deviceType == type && c.deviceName == device && c.sampleRate == rate &&
           c.bufferSize == bufferSizeSamples;
}

AudioSettings::KeyValues AudioSettings::toKeyValues() const
{
    KeyValues out;
    out[keyDeviceType] = deviceType;
    out[keyOutputDevice] = outputDevice;
    out[keyInputDevice] = inputDevice;
    out[keySampleRate] = formatDouble(sampleRate);
    out[keyBufferSize] = std::to_string(bufferSize);
    if (inputChannels.has_value())
        out[keyInputChannels] = joinChannels(*inputChannels);
    if (outputChannels.has_value())
        out[keyOutputChannels] = joinChannels(*outputChannels);
    out[keyMasterGain] = formatDouble(static_cast<double>(masterGain));
    out[keyPassThrough] = passThroughEnabled ? "1" : "0";
    if (midiInputs.has_value())
        out[keyMidiInputs] = joinNames(*midiInputs);
    out[keyMidiOutput] = midiOutput;
    if (calibration.has_value())
    {
        out[keyCalSamples] = std::to_string(calibration->roundTripSamples);
        out[keyCalRate] = formatDouble(calibration->sampleRate);
        out[keyCalBuffer] = std::to_string(calibration->bufferSize);
        out[keyCalType] = calibration->deviceType;
        out[keyCalDevice] = calibration->deviceName;
    }
    if (!inputBuses.empty())
    {
        out[keyInputBusCount] = std::to_string(inputBuses.size());
        for (size_t i = 0; i < inputBuses.size(); ++i)
        {
            const auto& bus = inputBuses[i];
            const auto prefix = inputBusPrefix + std::to_string(i) + ".";
            out[prefix + "type"] = bus.deviceType;
            out[prefix + "device"] = bus.deviceName;
            out[prefix + "name"] = bus.name;
            out[prefix + "left"] = std::to_string(bus.left);
            out[prefix + "right"] = std::to_string(bus.right);
        }
    }
    return out;
}

std::map<std::string, std::pair<int, int>> resolveInputBuses(const std::vector<InputBus>& buses,
                                                             const std::vector<int>& activeInputs)
{
    const auto position = [&](int deviceChannel)
    {
        const auto it = std::find(activeInputs.begin(), activeInputs.end(), deviceChannel);
        return it != activeInputs.end() ? static_cast<int>(it - activeInputs.begin()) : -1;
    };
    std::map<std::string, std::pair<int, int>> out;
    for (const auto& bus : buses)
    {
        const int left = position(bus.left), right = position(bus.right);
        if (left >= 0 && right >= 0 && !bus.name.empty())
            out.emplace(bus.name, std::pair{left, right});
    }
    return out;
}

std::vector<InputBus> AudioSettings::inputBusesFor(const std::string& type, const std::string& device) const
{
    std::vector<InputBus> out;
    for (const auto& bus : inputBuses)
        if (bus.deviceType == type && bus.deviceName == device)
            out.push_back(bus);
    return out;
}

void AudioSettings::setInputBusesFor(const std::string& type,
                                     const std::string& device,
                                     std::vector<InputBus> buses)
{
    std::erase_if(inputBuses,
                  [&](const InputBus& bus) { return bus.deviceType == type && bus.deviceName == device; });
    for (auto& bus : buses)
    {
        bus.deviceType = type;
        bus.deviceName = device;
        inputBuses.push_back(std::move(bus));
    }
}

AudioSettings AudioSettings::fromKeyValues(const KeyValues& values)
{
    AudioSettings s;
    s.deviceType = getString(values, keyDeviceType);
    s.outputDevice = getString(values, keyOutputDevice);
    s.inputDevice = getString(values, keyInputDevice);
    s.sampleRate = getDouble(values, keySampleRate, 0.0);
    s.bufferSize = getInt(values, keyBufferSize, 0);
    if (const auto* text = find(values, keyInputChannels))
        s.inputChannels = splitChannels(*text);
    if (const auto* text = find(values, keyOutputChannels))
        s.outputChannels = splitChannels(*text);
    s.masterGain = static_cast<float>(getDouble(values, keyMasterGain, 1.0));
    s.passThroughEnabled = getBool(values, keyPassThrough, false);
    if (const auto* text = find(values, keyMidiInputs))
        s.midiInputs = splitNames(*text);
    s.midiOutput = getString(values, keyMidiOutput);

    if (find(values, keyCalSamples) != nullptr)
    {
        LatencyCalibration c;
        c.roundTripSamples = getInt(values, keyCalSamples, 0);
        c.sampleRate = getDouble(values, keyCalRate, 0.0);
        c.bufferSize = getInt(values, keyCalBuffer, 0);
        c.deviceType = getString(values, keyCalType);
        c.deviceName = getString(values, keyCalDevice);
        if (c.roundTripSamples > 0 && c.sampleRate > 0.0)
            s.calibration = c;
    }

    const int busCount = std::clamp(getInt(values, keyInputBusCount, 0), 0, 1024);
    for (int i = 0; i < busCount; ++i)
    {
        const auto prefix = inputBusPrefix + std::to_string(i) + ".";
        InputBus bus;
        bus.deviceType = getString(values, (prefix + "type").c_str());
        bus.deviceName = getString(values, (prefix + "device").c_str());
        bus.name = getString(values, (prefix + "name").c_str());
        bus.left = getInt(values, (prefix + "left").c_str(), -1);
        bus.right = getInt(values, (prefix + "right").c_str(), -1);
        if (!bus.name.empty())
            s.inputBuses.push_back(std::move(bus));
    }

    if (s.sampleRate < 0.0)
        s.sampleRate = 0.0;
    if (s.bufferSize < 0)
        s.bufferSize = 0;
    if (!(s.masterGain >= 0.0f) || s.masterGain > 8.0f)
        s.masterGain = 1.0f;
    return s;
}

} // namespace northlight::audio
