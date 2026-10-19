// Loopback latency hardware test.
//
// Lists audio devices, opens one, prints the driver-reported latency, runs the
// loopback calibration and prints the measured round trip. Requires a cable from
// the chosen output to the chosen input. Not run by CI.
//
//   aural_loopback_tool --list
//   aural_loopback_tool [--type "ASIO"] [--device "Name"] [--rate 48000] [--buffer 64]
//                       [--in 0] [--out 0] [--passthrough-seconds 5]

#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"

#include <juce_events/juce_events.h>

#include <cstdio>
#include <string>

namespace
{

class MemoryStore final : public aural::audio::AudioSettingsStore
{
public:
    aural::audio::AudioSettings settings;
    aural::audio::AudioSettings loadAudioSettings() override { return settings; }
    void saveAudioSettings(const aural::audio::AudioSettings& s) override { settings = s; }
};

struct Args
{
    bool list = false;
    std::string type;
    std::string device;
    double rate = 0.0;
    int buffer = 0;
    int input = 0;
    int output = 0;
    int passThroughSeconds = 0;
};

Args parseArgs(const juce::StringArray& argv)
{
    Args a;
    for (int i = 0; i < argv.size(); ++i)
    {
        const auto& arg = argv[i];
        const auto next = [&]() -> juce::String { return i + 1 < argv.size() ? argv[++i] : juce::String(); };
        if (arg == "--list")
            a.list = true;
        else if (arg == "--type")
            a.type = next().toStdString();
        else if (arg == "--device")
            a.device = next().toStdString();
        else if (arg == "--rate")
            a.rate = next().getDoubleValue();
        else if (arg == "--buffer")
            a.buffer = next().getIntValue();
        else if (arg == "--in")
            a.input = next().getIntValue();
        else if (arg == "--out")
            a.output = next().getIntValue();
        else if (arg == "--passthrough-seconds")
            a.passThroughSeconds = next().getIntValue();
    }
    return a;
}

void printStatus(const aural::audio::AudioDeviceLayer::Status& s)
{
    if (!s.open)
    {
        std::printf("No device open. %s\n", s.message.c_str());
        return;
    }
    std::printf(
        "Device:   %s / %s (%s)\n", s.outputDevice.c_str(), s.inputDevice.c_str(), s.deviceType.c_str());
    std::printf("Format:   %.0f Hz, %d samples, %zu in / %zu out\n",
                s.sampleRate,
                s.bufferSize,
                s.activeInputs.size(),
                s.activeOutputs.size());
    std::printf("Reported: input %d samples (%.2f ms), output %d samples (%.2f ms), round trip %.2f ms\n",
                s.inputLatencySamples,
                s.inputLatencyMilliseconds(),
                s.outputLatencySamples,
                s.outputLatencyMilliseconds(),
                s.inputLatencyMilliseconds() + s.outputLatencyMilliseconds());
    if (!s.message.empty())
        std::printf("Note:     %s\n", s.message.c_str());
}

class Tool final : private juce::Timer
{
public:
    explicit Tool(Args args) : args_(std::move(args)), layer_(store_) {}

    int run()
    {
        if (args_.list)
        {
            layer_.initialise();
            for (const auto& type : layer_.deviceTypes())
            {
                std::printf(
                    "%s%s\n", type.name.c_str(), type.separateInputsAndOutputs ? " (separate in/out)" : "");
                const auto names = layer_.deviceNames(type.name);
                for (const auto& n : names.outputs)
                    std::printf("  out: %s\n", n.c_str());
                if (type.separateInputsAndOutputs)
                    for (const auto& n : names.inputs)
                        std::printf("  in:  %s\n", n.c_str());
            }
            layer_.shutdown();
            return 0;
        }

        store_.settings.deviceType = args_.type;
        store_.settings.outputDevice = args_.device;
        store_.settings.inputDevice = args_.device;
        store_.settings.sampleRate = args_.rate;
        store_.settings.bufferSize = args_.buffer;
        store_.settings.passThroughEnabled = args_.passThroughSeconds > 0;

        layer_.initialise();
        const auto status = layer_.status();
        printStatus(status);
        if (!status.open)
            return 1;

        if (args_.passThroughSeconds > 0)
        {
            std::printf("Passing input through to output for %d s...\n", args_.passThroughSeconds);
            passThroughEndsAt_ = juce::Time::getMillisecondCounter() +
                                 static_cast<juce::uint32>(args_.passThroughSeconds) * 1000u;
            startTimer(200);
        }
        else
        {
            startCalibration();
        }

        juce::MessageManager::getInstance()->runDispatchLoop();
        layer_.shutdown();
        return exitCode_;
    }

private:
    void timerCallback() override
    {
        if (juce::Time::getMillisecondCounter() < passThroughEndsAt_)
            return;
        stopTimer();
        layer_.setPassThroughEnabled(false);
        startCalibration();
    }

    void startCalibration()
    {
        std::printf(
            "Calibrating: output %d -> input %d (active channel indices)...\n", args_.output, args_.input);
        const bool started = layer_.calibrator().start(
            args_.input,
            args_.output,
            0.5f,
            [this](const aural::audio::LatencyCalibrator::Result& r)
            {
                std::printf("%s\n", r.message.c_str());
                if (r.measurement.has_value())
                {
                    std::printf("Measured: %d samples (%.2f ms), confidence %.1f, peak %.3f\n",
                                r.measurement->roundTripSamples,
                                r.measurement->roundTripMilliseconds,
                                r.measurement->confidence,
                                r.measurement->peakLevel);
                    exitCode_ = 0;
                }
                else
                {
                    exitCode_ = 2;
                }
                juce::MessageManager::getInstance()->stopDispatchLoop();
            });
        if (!started)
        {
            std::printf("Could not start calibration (device not running?).\n");
            exitCode_ = 1;
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    }

    Args args_;
    MemoryStore store_;
    aural::audio::AudioDeviceLayer layer_;
    juce::uint32 passThroughEndsAt_ = 0;
    int exitCode_ = 1;
};

} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add(juce::String(argv[i]));

    Tool tool(parseArgs(args));
    return tool.run();
}
