#pragma once

#include "audio/AudioDeviceLayer.h"
#include "ui/Theme.h"
#include "ui/components/Meter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui
{

// The Audio Setup screen: driver type, device, sample rate, buffer size, channel
// enables, stereo input buses, MIDI inputs and output, reported and measured
// latency, calibration and the ASIO control panel.
//
// The view is a thin reflection of AudioDeviceLayer: every control change is turned
// into an AudioSettings and applied; every change message from the layer refreshes
// the controls from its status. Nothing here touches the audio thread.
class AudioSetupView final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    // The window (screens/setup.md 8.1): 720x620, scrolling the content's natural height.
    static constexpr int preferredWidth = 720;
    static constexpr int preferredHeight = 620;
    static constexpr int minimumWidth = 640;
    static constexpr int minimumHeight = 480;
    static constexpr int contentHeight = 1280;

    // Pass-through switched on while an input is live: the owner shows a toast.
    std::function<void(const juce::String& message)> onWarning;

    explicit AudioSetupView(audio::AudioDeviceLayer& audio);
    ~AudioSetupView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class ChannelListModel;
    class InputBusRow;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    void refreshFromDevice();
    void refreshLatency(const audio::AudioDeviceLayer::Status& status);
    void refreshCalibrationChoices(const audio::AudioDeviceLayer::Status& status);

    void deviceTypeChanged();
    void deviceChanged();
    void formatChanged();
    void channelsChanged();
    void midiChanged();
    void applySettings(const audio::AudioSettings& settings);

    // Input buses: the rows follow the device's buses; every edit saves them all,
    // and enables a channel a bus reads when it is off.
    void refreshInputBuses(const audio::AudioDeviceLayer::Status& status);
    void commitInputBuses(std::vector<audio::InputBus> buses);
    void addInputBus();

    void calibrateClicked();
    void calibrationFinished(const audio::LatencyCalibrator::Result& result);
    void setStatus(const juce::String& text, juce::Colour colour);

    std::string selectedDeviceType() const;
    bool selectedTypeHasSeparateInputs() const;

    audio::AudioDeviceLayer& audio_;
    const Theme& theme_;
    bool updating_ = false;

    // Device
    juce::Label deviceTypeLabel_, outputDeviceLabel_, inputDeviceLabel_, sampleRateLabel_, bufferSizeLabel_;
    juce::ComboBox deviceTypeBox_, outputDeviceBox_, inputDeviceBox_, sampleRateBox_, bufferSizeBox_;
    juce::TextButton controlPanelButton_;
    std::vector<audio::AudioDeviceLayer::DeviceType> deviceTypes_;
    audio::AudioDeviceLayer::DeviceNames deviceNames_;

    // Channels
    juce::Label inputsLabel_, outputsLabel_;
    std::unique_ptr<ChannelListModel> inputsModel_, outputsModel_;
    juce::ListBox inputsList_, outputsList_;

    // Input buses
    juce::Label inputBusesHint_;
    juce::TextButton addInputBusButton_;
    juce::Viewport inputBusesView_;
    juce::Component inputBusesHolder_;
    std::vector<std::unique_ptr<InputBusRow>> inputBusRows_;
    std::vector<audio::InputBus> inputBuses_; // as the rows show them

    // MIDI
    juce::Label midiInputsLabel_, midiOutputLabel_;
    std::unique_ptr<ChannelListModel> midiInputsModel_;
    juce::ListBox midiInputsList_;
    juce::ComboBox midiOutputBox_;
    std::vector<audio::MidiDeviceLayer::Device> midiInputs_, midiOutputs_;

    // Latency
    juce::Label reportedLatencyLabel_, reportedLatencyValue_;
    juce::Label measuredLatencyLabel_, measuredLatencyValue_;
    juce::Label loopbackLabel_, loopbackArrow_;
    juce::ComboBox calibrationOutputBox_, calibrationInputBox_;
    juce::TextButton calibrateButton_, clearCalibrationButton_;
    juce::Label calibrationStatus_;

    // Monitoring
    juce::ToggleButton passThroughToggle_;
    juce::Label passThroughHint_;
    bool warnedPassThrough_ = false;
    float lastInputPeak_ = 0.0f;
    double lastMeterMs_ = 0.0;
    juce::Label levelLabel_;
    juce::Slider levelSlider_;
    juce::Label inputMeterLabel_, outputMeterLabel_;
    std::unique_ptr<Meter> inputMeter_, outputMeter_; // ui::Meter, horizontal

    // Status
    juce::Label statusLabel_;
    juce::Colour statusColour_;

    juce::Rectangle<int> deviceSection_, channelsSection_, inputBusesSection_, midiSection_, latencySection_,
        monitoringSection_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSetupView)
};

} // namespace northlight::ui
