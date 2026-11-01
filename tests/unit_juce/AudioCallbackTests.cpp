#include "audio/AudioCallback.h"
#include "audio/AudioDeviceLayer.h"
#include "engine/AudioEngine.h"
#include "engine/Recording.h"
#include "engine/WavFile.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace
{
const northlight::plugins::BuiltInFactory builtIns;
} // namespace

using namespace northlight;
using Catch::Approx;

namespace
{
class NoMediaLoader final : public engine::MediaLoader
{
public:
    std::shared_ptr<engine::AudioSource> load(const std::string&, double, std::string& error) override
    {
        error = "no media in this test";
        return nullptr;
    }
};

// Just enough of juce::AudioIODevice for the callback's start/stop handshake.
class FakeDevice final : public juce::AudioIODevice
{
public:
    FakeDevice() : juce::AudioIODevice("Fake", "Fake type") {}

    juce::StringArray getOutputChannelNames() override { return {"Out 1", "Out 2"}; }
    juce::StringArray getInputChannelNames() override { return {"In 1", "In 2"}; }
    juce::Array<double> getAvailableSampleRates() override { return {48000.0}; }
    juce::Array<int> getAvailableBufferSizes() override { return {64}; }
    int getDefaultBufferSize() override { return 64; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 64; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return channels(2); }
    juce::BigInteger getActiveInputChannels() const override { return channels(2); }
    int getOutputLatencyInSamples() override { return 72; }
    int getInputLatencyInSamples() override { return 136; }

private:
    static juce::BigInteger channels(int n)
    {
        juce::BigInteger b;
        b.setRange(0, n, true);
        return b;
    }
};
} // namespace

TEST_CASE("AudioCallback drives the engine through a device's start, blocks and stop", "[audio][callback]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    core::Project p;
    core::Track t;
    t.id = 1;
    t.inputChannel = 1;
    t.monitor = core::MonitorMode::on;
    t.gain = 1.0f;
    t.pan = 0.0f;
    p.tracks.push_back(t);
    engine.setProject(p);

    audio::AudioCallback callback;
    callback.setEngine(&engine);
    callback.setPassThroughEnabled(false);
    FakeDevice device;

    CHECK_FALSE(engine.isRunning());
    callback.audioDeviceAboutToStart(&device);
    CHECK(engine.isRunning());
    CHECK(engine.deviceSampleRate() == 48000.0);
    CHECK(callback.isDeviceRunning());
    CHECK(callback.numActiveInputs() == 2);

    std::vector<float> in0(64, 0.25f), in1(64, 0.5f);
    const float* inputs[] = {in0.data(), in1.data()};
    std::vector<float> out0(64), out1(64);
    float* outputs[] = {out0.data(), out1.data()};
    juce::AudioIODeviceCallbackContext context;

    // Stopped: the monitored input 2 is heard at constant-power centre.
    callback.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 64, context);
    const float centre = std::cos(3.14159265f / 4.0f);
    CHECK(out0[10] == Approx(0.5f * centre).margin(1e-6f));
    CHECK(out1[10] == Approx(0.5f * centre).margin(1e-6f));
    CHECK(engine.position() == 0);

    // Play: the position advances by one block per callback.
    engine.play();
    callback.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 64, context);
    callback.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 64, context);
    CHECK(engine.position() == 128);
    CHECK(callback.callbackCount() == 3);
    CHECK(callback.stats().summary().callbacks == 3);

    // Pass-through adds on top of the engine mix: input 1 on output 1 (0.25) plus the
    // monitored 0.5 * centre.
    callback.setPassThroughEnabled(true);
    callback.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 64, context);
    CHECK(out0[63] == Approx(0.25f + 0.5f * centre).margin(1e-5f));

    callback.audioDeviceStopped();
    CHECK_FALSE(engine.isRunning());
    CHECK_FALSE(callback.isDeviceRunning());
}

namespace
{
std::string tempRecordingPath(const char* name)
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("northlight-tests")
                          .getChildFile(name);
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    return file.getFullPathName().toStdString();
}

void waitForRecording(engine::AudioEngine& engine)
{
    for (int i = 0; i < 2000 && !engine.hasRecordingToCollect(); ++i)
        juce::Thread::sleep(2);
}
} // namespace

TEST_CASE("A recording started through the device callback captures the device input sample for sample",
          "[audio][callback][record]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.setProject(core::Project{});
    audio::AudioCallback callback;
    callback.setEngine(&engine);
    callback.setPassThroughEnabled(false);
    FakeDevice device;
    callback.audioDeviceAboutToStart(&device);

    const auto path = tempRecordingPath("callback.partial.wav");
    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    session->addInput(1, 1, path); // device input 2
    std::string error;
    REQUIRE(engine.startRecording(std::move(session), error));

    std::vector<float> in0(64, 0.0f), in1(64);
    const float* inputs[] = {in0.data(), in1.data()};
    std::vector<float> out0(64), out1(64);
    float* outputs[] = {out0.data(), out1.data()};
    juce::AudioIODeviceCallbackContext context;
    for (int block = 0; block < 4; ++block)
    {
        for (int i = 0; i < 64; ++i)
            in1[static_cast<size_t>(i)] = static_cast<float>(block * 64 + i) / 1000.0f;
        callback.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 64, context);
    }
    CHECK(engine.isRecording());
    CHECK(engine.position() == 256);
    engine.stop();
    callback.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 64, context);
    CHECK_FALSE(engine.isRecording());
    waitForRecording(engine);
    auto finished = engine.collectRecording();
    REQUIRE(finished.has_value());
    REQUIRE(finished->results.size() == 1);
    CHECK(finished->results[0].ok);
    CHECK(finished->results[0].samplesWritten == 256);
    CHECK(finished->results[0].dropped == 0);
    std::vector<std::vector<float>> channels;
    REQUIRE(engine::readWavSamples(path, channels));
    REQUIRE(channels.size() == 1);
    REQUIRE(channels[0].size() == 256);
    for (int i = 0; i < 256; ++i)
        REQUIRE(channels[0][static_cast<size_t>(i)] == Approx(static_cast<float>(i) / 1000.0f).margin(1e-7f));
    callback.audioDeviceStopped();
}

TEST_CASE("A device stopping mid-recording finishes the recording so its files close",
          "[audio][callback][record]")
{
    NoMediaLoader loader;
    engine::AudioEngine engine(loader, builtIns, 0);
    engine.setProject(core::Project{});
    audio::AudioCallback callback;
    callback.setEngine(&engine);
    FakeDevice device;
    callback.audioDeviceAboutToStart(&device);

    const auto path = tempRecordingPath("devicestop.partial.wav");
    auto session = std::make_unique<engine::RecordSession>(48000.0, 0.5);
    session->addInput(1, 0, path);
    std::string error;
    REQUIRE(engine.startRecording(std::move(session), error));
    std::vector<float> in0(64, 0.5f), in1(64, 0.0f);
    const float* inputs[] = {in0.data(), in1.data()};
    std::vector<float> out0(64), out1(64);
    float* outputs[] = {out0.data(), out1.data()};
    juce::AudioIODeviceCallbackContext context;
    callback.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 64, context);
    callback.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 64, context);
    REQUIRE(engine.isRecording());

    callback.audioDeviceStopped(); // the driver went away (or the user changed devices)
    CHECK_FALSE(engine.isRecording());
    waitForRecording(engine);
    auto finished = engine.collectRecording();
    REQUIRE(finished.has_value());
    REQUIRE(finished->results.size() == 1);
    CHECK(finished->results[0].ok);
    CHECK(finished->results[0].samplesWritten == 128);
    const auto info = engine::readWavInfo(path);
    REQUIRE(info.has_value());
    CHECK(info->lengthSamples == 128); // the header was finalised, not left at zero
}

TEST_CASE("Two meters on the callback's peaks do not take each other's", "[audio][callback]")
{
    audio::AudioCallback callback;
    FakeDevice device;
    callback.audioDeviceAboutToStart(&device);
    std::vector<float> in0(64, 0.25f), in1(64, -0.5f);
    const float* inputs[] = {in0.data(), in1.data()};
    std::vector<float> out0(64), out1(64);
    float* outputs[] = {out0.data(), out1.data()};
    juce::AudioIODeviceCallbackContext context;
    callback.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 64, context);

    rt::PeakHistory::Reader setup, firstRun;
    CHECK(setup.take(callback.inputPeaks()) == 0.5f);
    CHECK(firstRun.take(callback.inputPeaks()) == 0.5f);
    CHECK(setup.take(callback.inputPeaks()) == 0.0f);
    callback.audioDeviceStopped();
}

namespace
{
class MemoryStore final : public audio::AudioSettingsStore
{
public:
    audio::AudioSettings loadAudioSettings() override { return saved; }
    void saveAudioSettings(const audio::AudioSettings& settings) override { saved = settings; }
    audio::AudioSettings saved;
};
} // namespace

TEST_CASE("The master gain and pass-through have one home: the callback", "[audio][device]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    MemoryStore store;
    audio::AudioDeviceLayer layer(store);
    layer.setMasterGain(0.5f);
    layer.setPassThroughEnabled(true);
    CHECK(layer.callback().masterGain() == Approx(0.5f));
    CHECK(layer.settings().masterGain == Approx(0.5f));
    CHECK(layer.settings().passThroughEnabled);
    CHECK(store.saved.masterGain == Approx(0.5f));
    CHECK(store.saved.passThroughEnabled);
}

TEST_CASE("A picked device brings its input: the same device, or the driver's default input",
          "[audio][device]")
{
    audio::AudioDeviceLayer::DeviceNames names;
    names.outputs = {"Speakers", "Volt"};
    names.inputs = {"Mic", "Line", "Volt"};
    names.defaultInput = 1;
    const auto asio = audio::AudioDeviceLayer::withDevice({}, {"ASIO", false}, names, "Volt");
    CHECK(asio.deviceType == "ASIO");
    CHECK(asio.outputDevice == "Volt");
    CHECK(asio.inputDevice == "Volt");
    const auto wasapi = audio::AudioDeviceLayer::withDevice({}, {"Windows Audio", true}, names, "Speakers");
    CHECK(wasapi.outputDevice == "Speakers");
    CHECK(wasapi.inputDevice == "Line");
    CHECK(audio::AudioDeviceLayer::indexOf(names.outputs, "Volt") == 1);
    CHECK(audio::AudioDeviceLayer::indexOf(names.outputs, "Gone") == -1);
}

TEST_CASE("With nothing saved, JACK is opened when a server is running, else the system's own pick",
          "[audio][device]")
{
    using Layer = audio::AudioDeviceLayer;
    CHECK(Layer::preferredDefaultType({{"ALSA", true}, {"JACK", true}}) == "JACK");
    CHECK(Layer::preferredDefaultType({{"ALSA", true}, {"JACK", false}}).empty()); // no server
    CHECK(Layer::preferredDefaultType({{"ALSA", true}}).empty());
    CHECK(Layer::preferredDefaultType({{"Windows Audio", true}, {"ASIO", true}}).empty());
}
