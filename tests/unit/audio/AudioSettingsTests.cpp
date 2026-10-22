#include "audio/AudioSettings.h"

#include <catch2/catch_test_macros.hpp>

using northlight::audio::AudioSettings;
using northlight::audio::LatencyCalibration;

TEST_CASE("Default audio settings round-trip through key/values unchanged", "[audio][settings]")
{
    const AudioSettings original;
    const auto restored = AudioSettings::fromKeyValues(original.toKeyValues());
    CHECK(restored == original);
    CHECK_FALSE(restored.inputChannels.has_value());
    CHECK_FALSE(restored.outputChannels.has_value());
    CHECK_FALSE(restored.calibration.has_value());
}

TEST_CASE("Fully populated audio settings round-trip through key/values", "[audio][settings]")
{
    AudioSettings s;
    s.deviceType = "ASIO";
    s.outputDevice = "Focusrite USB ASIO";
    s.inputDevice = "Focusrite USB ASIO";
    s.sampleRate = 44100.0;
    s.bufferSize = 64;
    s.inputChannels = std::vector<int>{0, 1, 5};
    s.outputChannels = std::vector<int>{2, 3};
    s.masterGain = 0.5f;
    s.passThroughEnabled = false;
    s.calibration = LatencyCalibration{437, 44100.0, 64, "ASIO", "Focusrite USB ASIO"};

    const auto values = s.toKeyValues();
    CHECK(values.at("audio.inputChannels") == "0,1,5");
    CHECK(values.at("audio.outputChannels") == "2,3");
    CHECK(values.at("audio.sampleRate") == "44100");
    CHECK(values.at("audio.passThrough") == "0");

    const auto restored = AudioSettings::fromKeyValues(values);
    CHECK(restored == s);
    REQUIRE(restored.calibration.has_value());
    CHECK(restored.calibration->roundTripMilliseconds() > 9.9);
    CHECK(restored.calibration->roundTripMilliseconds() < 9.92);
}

TEST_CASE("MIDI device choices round-trip, with an explicit empty input list kept apart from all",
          "[audio][settings]")
{
    AudioSettings s;
    s.midiInputs = std::vector<std::string>{"Keystation 49", "MPK mini"};
    s.midiOutput = "loopMIDI Port";
    const auto values = s.toKeyValues();
    CHECK(values.at("midi.inputs") == "Keystation 49\tMPK mini");
    CHECK(values.at("midi.output") == "loopMIDI Port");
    const auto restored = AudioSettings::fromKeyValues(values);
    CHECK(restored == s);

    AudioSettings none;
    none.midiInputs = std::vector<std::string>{};
    const auto noneValues = none.toKeyValues();
    REQUIRE(noneValues.contains("midi.inputs"));
    CHECK(AudioSettings::fromKeyValues(noneValues).midiInputs == std::vector<std::string>{});
    CHECK_FALSE(AudioSettings::fromKeyValues({}).midiInputs.has_value()); // all inputs
}

TEST_CASE("An explicitly empty channel list is distinct from device default", "[audio][settings]")
{
    AudioSettings s;
    s.inputChannels = std::vector<int>{};
    const auto values = s.toKeyValues();
    REQUIRE(values.contains("audio.inputChannels"));
    CHECK(values.at("audio.inputChannels").empty());

    const auto restored = AudioSettings::fromKeyValues(values);
    REQUIRE(restored.inputChannels.has_value());
    CHECK(restored.inputChannels->empty());
    CHECK_FALSE(restored.outputChannels.has_value());
}

TEST_CASE("Missing and malformed keys fall back to defaults", "[audio][settings]")
{
    AudioSettings::KeyValues values;
    values["audio.sampleRate"] = "not a number";
    values["audio.bufferSize"] = "-5";
    values["audio.masterGain"] = "999";
    values["audio.passThrough"] = "maybe";
    values["audio.inputChannels"] = "1, x, 3, -2";
    values["audio.calibration.roundTripSamples"] = "0"; // present but invalid

    const auto s = AudioSettings::fromKeyValues(values);
    CHECK(s.sampleRate == 0.0);
    CHECK(s.bufferSize == 0);
    CHECK(s.masterGain == 1.0f);
    CHECK_FALSE(s.passThroughEnabled); // off by default: feedback otherwise
    REQUIRE(s.inputChannels.has_value());
    CHECK(*s.inputChannels == std::vector<int>{1, 3});
    CHECK_FALSE(s.calibration.has_value());
}

TEST_CASE("Calibration is only valid for the configuration it was measured with", "[audio][settings]")
{
    AudioSettings s;
    CHECK_FALSE(s.calibrationMatches("ASIO", "Dev", 48000.0, 64));

    s.calibration = LatencyCalibration{300, 48000.0, 64, "ASIO", "Dev"};
    CHECK(s.calibrationMatches("ASIO", "Dev", 48000.0, 64));
    CHECK_FALSE(s.calibrationMatches("ASIO", "Dev", 48000.0, 128));
    CHECK_FALSE(s.calibrationMatches("ASIO", "Dev", 44100.0, 64));
    CHECK_FALSE(s.calibrationMatches("ASIO", "Other", 48000.0, 64));
    CHECK_FALSE(s.calibrationMatches("Windows Audio", "Dev", 48000.0, 64));
}

TEST_CASE("Stereo input buses of several devices round-trip and are kept per device", "[audio][settings]")
{
    using northlight::audio::InputBus;
    AudioSettings s;
    s.setInputBusesFor("ASIO", "Volt", {InputBus{"", "", "Vocals", 0, 1}, InputBus{"", "", "Keys", 3, 2}});
    s.setInputBusesFor("Windows Audio", "Mic Array", {InputBus{"", "", "Room", 0, 1}});

    const auto restored = AudioSettings::fromKeyValues(s.toKeyValues());
    CHECK(restored == s);
    const auto volt = restored.inputBusesFor("ASIO", "Volt");
    REQUIRE(volt.size() == 2);
    CHECK(volt[0].name == "Vocals");
    CHECK(volt[1].name == "Keys");
    CHECK(volt[1].left == 3);
    CHECK(volt[1].right == 2);
    CHECK(volt[0].deviceType == "ASIO");

    // Replacing one device's buses leaves the other device's alone.
    s.setInputBusesFor("ASIO", "Volt", {});
    CHECK(s.inputBusesFor("ASIO", "Volt").empty());
    REQUIRE(s.inputBusesFor("Windows Audio", "Mic Array").size() == 1);
}

TEST_CASE("Input buses resolve to positions among the enabled inputs, and need both sides enabled",
          "[audio][settings]")
{
    using northlight::audio::InputBus;
    const std::vector<InputBus> buses{
        InputBus{"ASIO", "Volt", "Pair 5/6", 4, 5},
        InputBus{"ASIO", "Volt", "Pair 3/4", 2, 3}, // 3 and 4 are off
        InputBus{"ASIO", "Volt", "Swapped", 1, 0},
        InputBus{"ASIO", "Volt", "Swapped", 4, 5}}; // a repeated name: the first wins
    const auto resolved = northlight::audio::resolveInputBuses(buses, {0, 1, 4, 5});
    CHECK(resolved.size() == 2);
    CHECK(resolved.at("Pair 5/6") == std::pair{2, 3});
    CHECK(resolved.at("Swapped") == std::pair{1, 0});
    CHECK_FALSE(resolved.contains("Pair 3/4"));
}
