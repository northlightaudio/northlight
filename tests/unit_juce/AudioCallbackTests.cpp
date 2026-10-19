#include "audio/AudioCallback.h"
#include "engine/AudioEngine.h"
#include "plugins/BuiltIns.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

namespace
{
const aural::plugins::BuiltInFactory builtIns;
} // namespace

using namespace aural;
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
