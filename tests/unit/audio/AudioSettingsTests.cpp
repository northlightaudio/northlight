#include "audio/AudioSettings.h"

#include <catch2/catch_test_macros.hpp>

using aural::audio::AudioSettings;
using aural::audio::LatencyCalibration;

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
    CHECK(s.passThroughEnabled);
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
