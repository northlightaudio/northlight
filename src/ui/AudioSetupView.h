#pragma once

#include "audio/AudioDeviceLayer.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>
#include <vector>

namespace aural::ui
{

// The Audio Setup screen: driver type, device, sample rate, buffer size, channel
// enables, MIDI inputs and output, reported and measured latency, calibration and
// the ASIO control panel.
//
// The view is a thin reflection of AudioDeviceLayer: every control change is turned
// into an AudioSettings and applied; every change message from the layer refreshes
// the controls from its status. Nothing here touches the audio thread.
class AudioSetupView final : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    static constexpr int preferredWidth = 860;
    static constexpr int preferredHeight = 960;
    static constexpr int minimumWidth = 700;
    static constexpr int minimumHeight = 820;

    explicit AudioSetupView(audio::AudioDeviceLayer& audio);
    ~AudioSetupView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class ChannelListModel;
    class Meter;

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
    juce::Label levelLabel_;
    juce::Slider levelSlider_;
    juce::Label inputMeterLabel_, outputMeterLabel_;
    std::unique_ptr<Meter> inputMeter_, outputMeter_;

    // Status
    juce::Label statusLabel_;

    juce::Rectangle<int> deviceSection_, channelsSection_, midiSection_, latencySection_, monitoringSection_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSetupView)
};

} // namespace aural::ui
