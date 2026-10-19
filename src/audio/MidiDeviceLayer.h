#pragma once

#include "engine/Midi.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace aural::engine
{
class AudioEngine;
}

namespace aural::audio
{

// MIDI devices: the inputs the user enabled (every input by default) are open and
// feed the engine's live MIDI queue, tagged with the device name so a track can
// listen to one of them; one output can be selected and is kept open. Devices
// that appear or vanish (hot-plug) are picked up through JUCE's device-list
// notification and the lists re-read. Message thread only, apart from the input
// callbacks, which run on JUCE's MIDI threads and only touch the engine's queue.
class MidiDeviceLayer final : public juce::ChangeBroadcaster, private juce::MidiInputCallback
{
public:
    MidiDeviceLayer();
    ~MidiDeviceLayer() override;

    // The engine the inputs feed; null detaches (before the engine goes away).
    void setEngine(engine::AudioEngine* engine) noexcept;

    // Opens what the settings enable and starts watching the device list.
    // `enabledInputs` empty optional = every input; names identify devices.
    void initialise(const std::optional<std::vector<std::string>>& enabledInputs, const std::string& output);
    void shutdown();

    struct Device
    {
        std::string name;
        std::string identifier;
        bool enabled = false; // inputs: chosen by the user; output: the selected one
        bool open = false;
    };
    std::vector<Device> inputs() const;
    std::vector<Device> outputs() const;
    // The names tracks can pick from (the enabled inputs that are open).
    std::vector<std::string> openInputNames() const;

    void setInputEnabled(const std::string& name, bool enabled);
    void setOutput(const std::string& name); // empty = none
    // What to persist.
    std::optional<std::vector<std::string>> enabledInputNames() const { return enabledInputs_; }
    const std::string& outputName() const noexcept { return outputName_; }
    juce::MidiOutput* output() noexcept { return output_.get(); }

    // Called after the enabled set or the output changed through the setters.
    std::function<void()> onChanged;

    // Re-reads the device list (also called on hot-plug notifications).
    void refresh();

private:
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& message) override;
    bool isEnabled(const std::string& name) const;
    void openEnabledInputs();

    struct OpenInput
    {
        std::unique_ptr<juce::MidiInput> device;
        std::string name;
    };

    std::atomic<engine::AudioEngine*> engine_{nullptr};
    std::optional<std::vector<std::string>> enabledInputs_;
    std::string outputName_;
    std::vector<OpenInput> open_;
    mutable std::mutex openMutex_; // the callback reads open_ while the list changes
    std::unique_ptr<juce::MidiOutput> output_;
    juce::MidiDeviceListConnection connection_;
    bool initialised_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDeviceLayer)
};

} // namespace aural::audio
