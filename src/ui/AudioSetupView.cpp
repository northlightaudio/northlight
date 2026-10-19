#include "ui/AudioSetupView.h"

#include <cmath>

namespace aural::ui
{

namespace
{
constexpr int uiTimerHz = 15;
constexpr float calibrationLevel = 0.5f; // -6 dBFS test signal
constexpr double minLevelDb = -60.0;
constexpr double maxLevelDb = 6.0;

// JUCE's String(const char*) is Latin-1; these UTF-8 source literals (/utf-8) go through fromUTF8.
const juce::String separator = juce::String::fromUTF8("  ·  "); // middle dot
const juce::String rightArrow = juce::String::fromUTF8("→");
const juce::String emDash = juce::String::fromUTF8("—");

juce::String ms(double milliseconds)
{
    return juce::String(milliseconds, 2) + " ms";
}

float dbToGain(double db)
{
    return db <= minLevelDb + 0.05 ? 0.0f : static_cast<float>(std::pow(10.0, db / 20.0));
}

double gainToDb(float gain)
{
    return gain <= 0.0f ? minLevelDb : juce::jlimit(minLevelDb, maxLevelDb, 20.0 * std::log10(gain));
}

void styleCaption(juce::Label& label, const Theme& theme)
{
    label.setFont(juce::FontOptions{theme.type.body});
    label.setColour(juce::Label::textColourId, theme.colours.textSecondary);
    label.setJustificationType(juce::Justification::centredLeft);
}

void styleValue(juce::Label& label, const Theme& theme)
{
    label.setFont(juce::FontOptions{theme.type.body});
    label.setColour(juce::Label::textColourId, theme.colours.textPrimary);
    label.setJustificationType(juce::Justification::centredLeft);
}
} // namespace

// ----- channel list -----------------------------------------------------------------------

class AudioSetupView::ChannelListModel final : public juce::ListBoxModel
{
public:
    // `numbered`: rows read "1: name" (device channels) rather than the name alone.
    explicit ChannelListModel(const Theme& theme, bool numbered = true) : theme_(theme), numbered_(numbered)
    {
    }

    std::function<void()> onChanged;
    // Which row changed last (for lists keyed by name rather than index).
    int lastChangedRow() const noexcept { return lastChanged_; }
    bool isEnabled(size_t row) const noexcept { return row < enabled_.size() && enabled_[row]; }

    void setChannels(std::vector<std::string> names, const std::vector<int>& enabledIndices)
    {
        names_ = std::move(names);
        enabled_.assign(names_.size(), false);
        for (int i : enabledIndices)
            if (i >= 0 && static_cast<size_t>(i) < enabled_.size())
                enabled_[static_cast<size_t>(i)] = true;
    }

    std::vector<int> enabledIndices() const
    {
        std::vector<int> out;
        for (size_t i = 0; i < enabled_.size(); ++i)
            if (enabled_[i])
                out.push_back(static_cast<int>(i));
        return out;
    }

    int getNumRows() override { return static_cast<int>(names_.size()); }

    void paintListBoxItem(int /*row*/, juce::Graphics&, int /*w*/, int /*h*/, bool /*selected*/) override {}

    juce::Component* refreshComponentForRow(int row, bool /*selected*/, juce::Component* existing) override
    {
        auto* toggle = dynamic_cast<juce::ToggleButton*>(existing);
        if (row < 0 || row >= getNumRows())
        {
            delete existing;
            return nullptr;
        }
        if (toggle == nullptr)
        {
            delete existing;
            toggle = new juce::ToggleButton();
        }
        const auto index = static_cast<size_t>(row);
        toggle->setButtonText(numbered_ ? juce::String(row + 1) + ": " + names_[index]
                                        : juce::String::fromUTF8(names_[index].c_str()));
        toggle->setToggleState(enabled_[index], juce::dontSendNotification);
        toggle->onClick = [this, index, toggle]
        {
            if (index < enabled_.size())
                enabled_[index] = toggle->getToggleState();
            lastChanged_ = static_cast<int>(index);
            if (onChanged)
                onChanged();
        };
        return toggle;
    }

private:
    const Theme& theme_;
    bool numbered_ = true;
    int lastChanged_ = -1;
    std::vector<std::string> names_;
    std::vector<bool> enabled_;
};

// ----- meter ------------------------------------------------------------------------------

class AudioSetupView::Meter final : public juce::Component
{
public:
    explicit Meter(const Theme& theme) : theme_(theme) {}

    void push(float peak)
    {
        level_ = juce::jmax(peak, level_ * 0.8f);
        if (peak >= 1.0f)
            clipFrames_ = uiTimerHz;
        else if (clipFrames_ > 0)
            --clipFrames_;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        auto bounds = getLocalBounds().toFloat();
        g.setColour(c.surfaceRaised);
        g.fillRoundedRectangle(bounds, 3.0f);

        const double db = level_ > 0.0f ? 20.0 * std::log10(level_) : -100.0;
        const auto fraction = static_cast<float>(juce::jlimit(0.0, 1.0, (db + 60.0) / 60.0));
        auto fill = bounds.withWidth(bounds.getWidth() * fraction);
        g.setColour(clipFrames_ > 0 ? c.meterClip : (db > -6.0 ? c.meterHot : c.meter));
        g.fillRoundedRectangle(fill, 3.0f);
    }

private:
    const Theme& theme_;
    float level_ = 0.0f;
    int clipFrames_ = 0;
};

// ----- view -------------------------------------------------------------------------------

AudioSetupView::AudioSetupView(audio::AudioDeviceLayer& audio) : audio_(audio), theme_(Theme::dark())
{
    setOpaque(true);

    // Device
    for (auto* label :
         {&deviceTypeLabel_, &outputDeviceLabel_, &inputDeviceLabel_, &sampleRateLabel_, &bufferSizeLabel_})
    {
        styleCaption(*label, theme_);
        addAndMakeVisible(*label);
    }
    deviceTypeLabel_.setText("Driver type", juce::dontSendNotification);
    outputDeviceLabel_.setText("Output device", juce::dontSendNotification);
    inputDeviceLabel_.setText("Input device", juce::dontSendNotification);
    sampleRateLabel_.setText("Sample rate", juce::dontSendNotification);
    bufferSizeLabel_.setText("Buffer size", juce::dontSendNotification);

    for (auto* box : {&deviceTypeBox_, &outputDeviceBox_, &inputDeviceBox_, &sampleRateBox_, &bufferSizeBox_})
        addAndMakeVisible(*box);
    deviceTypeBox_.onChange = [this] { deviceTypeChanged(); };
    outputDeviceBox_.onChange = [this] { deviceChanged(); };
    inputDeviceBox_.onChange = [this] { deviceChanged(); };
    sampleRateBox_.onChange = [this] { formatChanged(); };
    bufferSizeBox_.onChange = [this] { formatChanged(); };

    controlPanelButton_.setButtonText("Control Panel...");
    controlPanelButton_.onClick = [this] { audio_.showControlPanel(); };
    addAndMakeVisible(controlPanelButton_);

    // Channels
    inputsModel_ = std::make_unique<ChannelListModel>(theme_);
    outputsModel_ = std::make_unique<ChannelListModel>(theme_);
    inputsModel_->onChanged = [this] { channelsChanged(); };
    outputsModel_->onChanged = [this] { channelsChanged(); };
    for (auto* label : {&inputsLabel_, &outputsLabel_})
    {
        styleCaption(*label, theme_);
        addAndMakeVisible(*label);
    }
    inputsLabel_.setText("Inputs", juce::dontSendNotification);
    outputsLabel_.setText("Outputs", juce::dontSendNotification);
    inputsList_.setModel(inputsModel_.get());
    outputsList_.setModel(outputsModel_.get());
    for (auto* list : {&inputsList_, &outputsList_})
    {
        list->setRowHeight(theme_.sizes.rowHeight);
        list->setOutlineThickness(1);
        addAndMakeVisible(*list);
    }

    // MIDI
    midiInputsModel_ = std::make_unique<ChannelListModel>(theme_, false);
    midiInputsModel_->onChanged = [this] { midiChanged(); };
    for (auto* label : {&midiInputsLabel_, &midiOutputLabel_})
    {
        styleCaption(*label, theme_);
        addAndMakeVisible(*label);
    }
    midiInputsLabel_.setText("Inputs", juce::dontSendNotification);
    midiOutputLabel_.setText("Output", juce::dontSendNotification);
    midiInputsList_.setModel(midiInputsModel_.get());
    midiInputsList_.setRowHeight(theme_.sizes.rowHeight);
    midiInputsList_.setOutlineThickness(1);
    addAndMakeVisible(midiInputsList_);
    midiOutputBox_.onChange = [this]
    {
        if (!updating_)
            midiChanged();
    };
    addAndMakeVisible(midiOutputBox_);

    // Latency
    for (auto* label : {&reportedLatencyLabel_, &measuredLatencyLabel_, &loopbackLabel_})
    {
        styleCaption(*label, theme_);
        addAndMakeVisible(*label);
    }
    reportedLatencyLabel_.setText("Reported", juce::dontSendNotification);
    measuredLatencyLabel_.setText("Measured", juce::dontSendNotification);
    loopbackLabel_.setText("Loopback", juce::dontSendNotification);
    for (auto* label : {&reportedLatencyValue_, &measuredLatencyValue_, &loopbackArrow_, &calibrationStatus_})
    {
        styleValue(*label, theme_);
        addAndMakeVisible(*label);
    }
    loopbackArrow_.setText(rightArrow, juce::dontSendNotification);
    loopbackArrow_.setJustificationType(juce::Justification::centred);
    calibrationStatus_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
    calibrationStatus_.setMinimumHorizontalScale(1.0f);

    addAndMakeVisible(calibrationOutputBox_);
    addAndMakeVisible(calibrationInputBox_);
    calibrateButton_.setButtonText("Calibrate");
    calibrateButton_.onClick = [this] { calibrateClicked(); };
    addAndMakeVisible(calibrateButton_);
    clearCalibrationButton_.setButtonText("Clear");
    clearCalibrationButton_.onClick = [this] { audio_.clearCalibration(); };
    addAndMakeVisible(clearCalibrationButton_);

    // Monitoring
    passThroughToggle_.setButtonText("Pass inputs through to outputs");
    passThroughToggle_.onClick = [this]
    {
        if (!updating_)
            audio_.setPassThroughEnabled(passThroughToggle_.getToggleState());
    };
    addAndMakeVisible(passThroughToggle_);

    styleCaption(levelLabel_, theme_);
    levelLabel_.setText("Level", juce::dontSendNotification);
    addAndMakeVisible(levelLabel_);
    levelSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    levelSlider_.setRange(minLevelDb, maxLevelDb, 0.1);
    levelSlider_.setSkewFactorFromMidPoint(-12.0);
    levelSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, theme_.sizes.controlHeight - 4);
    levelSlider_.textFromValueFunction = [](double v)
    { return v <= minLevelDb + 0.05 ? juce::String("-inf dB") : juce::String(v, 1) + " dB"; };
    levelSlider_.valueFromTextFunction = [](const juce::String& t)
    { return t.containsIgnoreCase("inf") ? minLevelDb : t.getDoubleValue(); };
    levelSlider_.onValueChange = [this]
    {
        if (!updating_)
            audio_.setMasterGain(dbToGain(levelSlider_.getValue()));
    };
    levelSlider_.updateText();
    addAndMakeVisible(levelSlider_);

    for (auto* label : {&inputMeterLabel_, &outputMeterLabel_})
    {
        styleCaption(*label, theme_);
        addAndMakeVisible(*label);
    }
    inputMeterLabel_.setText("Input", juce::dontSendNotification);
    outputMeterLabel_.setText("Output", juce::dontSendNotification);
    inputMeter_ = std::make_unique<Meter>(theme_);
    outputMeter_ = std::make_unique<Meter>(theme_);
    addAndMakeVisible(*inputMeter_);
    addAndMakeVisible(*outputMeter_);

    // Status
    styleValue(statusLabel_, theme_);
    statusLabel_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
    statusLabel_.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(statusLabel_);

    audio_.addChangeListener(this);
    refreshFromDevice();
    startTimerHz(uiTimerHz);
}

AudioSetupView::~AudioSetupView()
{
    stopTimer();
    audio_.calibrator().cancel();
    audio_.removeChangeListener(this);
    inputsList_.setModel(nullptr);
    outputsList_.setModel(nullptr);
    midiInputsList_.setModel(nullptr);
}

// ----- painting and layout ------------------------------------------------------------------

void AudioSetupView::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.background);

    const auto drawSection = [&](juce::Rectangle<int> area, const juce::String& title)
    {
        g.setColour(c.surface);
        g.fillRoundedRectangle(area.toFloat(), theme_.radius.lg);
        g.setColour(c.border);
        g.drawRoundedRectangle(area.toFloat().reduced(0.5f), theme_.radius.lg, 1.0f);
        g.setColour(c.textPrimary);
        g.setFont(juce::FontOptions{theme_.type.title});
        g.drawText(title,
                   area.reduced(theme_.spacing.lg, theme_.spacing.md).removeFromTop(22),
                   juce::Justification::centredLeft);
    };

    drawSection(deviceSection_, "Device");
    drawSection(channelsSection_, "Channels");
    drawSection(midiSection_, "MIDI");
    drawSection(latencySection_, "Latency");
    drawSection(monitoringSection_, "Monitoring");
}

void AudioSetupView::resized()
{
    const auto& sp = theme_.spacing;
    const int rowH = theme_.sizes.controlHeight;
    const int labelW = theme_.sizes.labelWidth;
    const int gap = sp.sm;
    const int titleH = 22 + sp.md;
    const bool separateInputs = selectedTypeHasSeparateInputs();

    auto bounds = getLocalBounds().reduced(sp.xl);
    statusLabel_.setBounds(bounds.removeFromBottom(rowH));
    bounds.removeFromBottom(sp.md);

    const int deviceRows = separateInputs ? 4 : 3;
    const int deviceH = titleH + deviceRows * (rowH + gap) + sp.lg;
    const int latencyH = titleH + 4 * (rowH + gap) + sp.lg;
    const int monitoringH = titleH + 2 * (rowH + gap) + sp.lg;
    const int midiH = titleH + 3 * (rowH + gap) + sp.lg;

    deviceSection_ = bounds.removeFromTop(deviceH);
    bounds.removeFromTop(sp.md);
    monitoringSection_ = bounds.removeFromBottom(monitoringH);
    bounds.removeFromBottom(sp.md);
    latencySection_ = bounds.removeFromBottom(latencyH);
    bounds.removeFromBottom(sp.md);
    midiSection_ = bounds.removeFromBottom(midiH);
    bounds.removeFromBottom(sp.md);
    channelsSection_ = bounds;

    // Device
    {
        auto area = deviceSection_.reduced(sp.lg).withTrimmedTop(titleH);
        auto row = area.removeFromTop(rowH);
        deviceTypeLabel_.setBounds(row.removeFromLeft(labelW));
        deviceTypeBox_.setBounds(row.removeFromLeft(280));
        area.removeFromTop(gap);

        row = area.removeFromTop(rowH);
        outputDeviceLabel_.setBounds(row.removeFromLeft(labelW));
        outputDeviceLabel_.setText(separateInputs ? "Output device" : "Device", juce::dontSendNotification);
        controlPanelButton_.setBounds(row.removeFromRight(150));
        row.removeFromRight(gap);
        outputDeviceBox_.setBounds(row);
        area.removeFromTop(gap);

        inputDeviceLabel_.setVisible(separateInputs);
        inputDeviceBox_.setVisible(separateInputs);
        if (separateInputs)
        {
            row = area.removeFromTop(rowH);
            inputDeviceLabel_.setBounds(row.removeFromLeft(labelW));
            row.removeFromRight(150 + gap);
            inputDeviceBox_.setBounds(row);
            area.removeFromTop(gap);
        }

        row = area.removeFromTop(rowH);
        sampleRateLabel_.setBounds(row.removeFromLeft(labelW));
        sampleRateBox_.setBounds(row.removeFromLeft(140));
        row.removeFromLeft(sp.xl);
        bufferSizeLabel_.setBounds(row.removeFromLeft(labelW - sp.xl));
        bufferSizeBox_.setBounds(row.removeFromLeft(140));
    }

    // Channels
    {
        auto area = channelsSection_.reduced(sp.lg).withTrimmedTop(titleH);
        auto left = area.removeFromLeft((area.getWidth() - sp.lg) / 2);
        area.removeFromLeft(sp.lg);
        inputsLabel_.setBounds(left.removeFromTop(rowH));
        inputsList_.setBounds(left);
        outputsLabel_.setBounds(area.removeFromTop(rowH));
        outputsList_.setBounds(area);
    }

    // MIDI: the inputs as a toggle list on the left, the output choice on the right.
    {
        auto area = midiSection_.reduced(sp.lg).withTrimmedTop(titleH);
        auto left = area.removeFromLeft((area.getWidth() - sp.lg) / 2);
        area.removeFromLeft(sp.lg);
        midiInputsLabel_.setBounds(left.removeFromTop(rowH));
        midiInputsList_.setBounds(left);
        auto row = area.removeFromTop(rowH);
        midiOutputLabel_.setBounds(row.removeFromLeft(labelW));
        area.removeFromTop(gap);
        midiOutputBox_.setBounds(area.removeFromTop(rowH));
    }

    // Latency
    {
        auto area = latencySection_.reduced(sp.lg).withTrimmedTop(titleH);
        auto row = area.removeFromTop(rowH);
        reportedLatencyLabel_.setBounds(row.removeFromLeft(labelW));
        reportedLatencyValue_.setBounds(row);
        area.removeFromTop(gap);

        row = area.removeFromTop(rowH);
        measuredLatencyLabel_.setBounds(row.removeFromLeft(labelW));
        clearCalibrationButton_.setBounds(row.removeFromRight(90));
        row.removeFromRight(gap);
        measuredLatencyValue_.setBounds(row);
        area.removeFromTop(gap);

        row = area.removeFromTop(rowH);
        loopbackLabel_.setBounds(row.removeFromLeft(labelW));
        calibrateButton_.setBounds(row.removeFromRight(130));
        row.removeFromRight(gap);
        const int comboW = juce::jmax(120, (row.getWidth() - 32) / 2);
        calibrationOutputBox_.setBounds(row.removeFromLeft(comboW));
        loopbackArrow_.setBounds(row.removeFromLeft(32));
        calibrationInputBox_.setBounds(row.removeFromLeft(comboW));
        area.removeFromTop(gap);

        row = area.removeFromTop(rowH);
        row.removeFromLeft(labelW);
        calibrationStatus_.setBounds(row);
    }

    // Monitoring
    {
        auto area = monitoringSection_.reduced(sp.lg).withTrimmedTop(titleH);
        auto row = area.removeFromTop(rowH);
        passThroughToggle_.setBounds(row.removeFromLeft(280));
        row.removeFromLeft(sp.lg);
        levelLabel_.setBounds(row.removeFromLeft(50));
        levelSlider_.setBounds(row);
        area.removeFromTop(gap);

        row = area.removeFromTop(rowH);
        const int meterH = theme_.sizes.meterHeight + 4;
        auto left = row.removeFromLeft((row.getWidth() - sp.lg) / 2);
        row.removeFromLeft(sp.lg);
        inputMeterLabel_.setBounds(left.removeFromLeft(60));
        inputMeter_->setBounds(left.withSizeKeepingCentre(left.getWidth(), meterH));
        outputMeterLabel_.setBounds(row.removeFromLeft(60));
        outputMeter_->setBounds(row.withSizeKeepingCentre(row.getWidth(), meterH));
    }
}

// ----- refresh ----------------------------------------------------------------------------

void AudioSetupView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    refreshFromDevice();
}

void AudioSetupView::timerCallback()
{
    inputMeter_->push(audio_.callback().takeInputPeak());
    outputMeter_->push(audio_.callback().takeOutputPeak());

    auto& calibrator = audio_.calibrator();
    if (calibrator.isRunning())
    {
        const auto p = calibrator.progress();
        calibrationStatus_.setText("Calibrating... trial " + juce::String(p.trial) + " of " +
                                       juce::String(p.trialsTotal),
                                   juce::dontSendNotification);
    }
}

std::string AudioSetupView::selectedDeviceType() const
{
    const int index = deviceTypeBox_.getSelectedId() - 1;
    if (index >= 0 && static_cast<size_t>(index) < deviceTypes_.size())
        return deviceTypes_[static_cast<size_t>(index)].name;
    return {};
}

bool AudioSetupView::selectedTypeHasSeparateInputs() const
{
    const int index = deviceTypeBox_.getSelectedId() - 1;
    if (index >= 0 && static_cast<size_t>(index) < deviceTypes_.size())
        return deviceTypes_[static_cast<size_t>(index)].separateInputsAndOutputs;
    return false;
}

void AudioSetupView::refreshFromDevice()
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    const auto status = audio_.status();
    const auto& settings = audio_.settings();

    // Driver type
    deviceTypes_ = audio_.deviceTypes();
    const std::string typeName = !status.deviceType.empty() ? status.deviceType : settings.deviceType;
    const bool typeChanged = selectedDeviceType() != typeName;
    deviceTypeBox_.clear(juce::dontSendNotification);
    for (size_t i = 0; i < deviceTypes_.size(); ++i)
    {
        deviceTypeBox_.addItem(deviceTypes_[i].name, static_cast<int>(i) + 1);
        if (deviceTypes_[i].name == typeName)
            deviceTypeBox_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
    }

    // Devices of that type
    deviceNames_ = audio_.deviceNames(selectedDeviceType());
    const auto fillDevices =
        [](juce::ComboBox& box, const std::vector<std::string>& names, const std::string& current)
    {
        box.clear(juce::dontSendNotification);
        box.addItem("(none)", 1);
        for (size_t i = 0; i < names.size(); ++i)
        {
            box.addItem(names[i], static_cast<int>(i) + 2);
            if (names[i] == current)
                box.setSelectedId(static_cast<int>(i) + 2, juce::dontSendNotification);
        }
        if (box.getSelectedId() == 0)
            box.setSelectedId(1, juce::dontSendNotification);
    };
    fillDevices(outputDeviceBox_, deviceNames_.outputs, status.outputDevice);
    fillDevices(inputDeviceBox_, deviceNames_.inputs, status.inputDevice);

    // Sample rate and buffer size
    sampleRateBox_.clear(juce::dontSendNotification);
    int id = 1;
    for (double rate : audio_.availableSampleRates())
    {
        sampleRateBox_.addItem(
            juce::String(rate / 1000.0, rate == std::floor(rate / 100.0) * 100.0 ? 1 : 3) + " kHz", id);
        if (rate == status.sampleRate)
            sampleRateBox_.setSelectedId(id, juce::dontSendNotification);
        ++id;
    }
    bufferSizeBox_.clear(juce::dontSendNotification);
    id = 1;
    for (int size : audio_.availableBufferSizes())
    {
        juce::String text(size);
        if (status.sampleRate > 0.0)
            text << " samples  (" << juce::String(1000.0 * size / status.sampleRate, 1) << " ms)";
        bufferSizeBox_.addItem(text, id);
        if (size == status.bufferSize)
            bufferSizeBox_.setSelectedId(id, juce::dontSendNotification);
        ++id;
    }
    sampleRateBox_.setEnabled(status.open);
    bufferSizeBox_.setEnabled(status.open);
    controlPanelButton_.setEnabled(status.hasControlPanel);

    // Channels
    inputsModel_->setChannels(audio_.inputChannelNames(), status.activeInputs);
    outputsModel_->setChannels(audio_.outputChannelNames(), status.activeOutputs);
    inputsList_.updateContent();
    outputsList_.updateContent();
    inputsList_.repaint();
    outputsList_.repaint();

    // MIDI
    midiInputs_ = audio_.midi().inputs();
    midiOutputs_ = audio_.midi().outputs();
    {
        std::vector<std::string> names;
        std::vector<int> enabled;
        for (size_t i = 0; i < midiInputs_.size(); ++i)
        {
            names.push_back(midiInputs_[i].name +
                            (midiInputs_[i].enabled && !midiInputs_[i].open ? " (could not open)" : ""));
            if (midiInputs_[i].enabled)
                enabled.push_back(static_cast<int>(i));
        }
        midiInputsModel_->setChannels(std::move(names), enabled);
        midiInputsList_.updateContent();
        midiInputsList_.repaint();
        midiOutputBox_.clear(juce::dontSendNotification);
        midiOutputBox_.addItem("(none)", 1);
        midiOutputBox_.setSelectedId(1, juce::dontSendNotification);
        for (size_t i = 0; i < midiOutputs_.size(); ++i)
        {
            midiOutputBox_.addItem(juce::String::fromUTF8(midiOutputs_[i].name.c_str()),
                                   static_cast<int>(i) + 2);
            if (midiOutputs_[i].enabled)
                midiOutputBox_.setSelectedId(static_cast<int>(i) + 2, juce::dontSendNotification);
        }
    }

    refreshLatency(status);
    refreshCalibrationChoices(status);

    // Monitoring
    passThroughToggle_.setToggleState(audio_.callback().isPassThroughEnabled(), juce::dontSendNotification);
    levelSlider_.setValue(gainToDb(audio_.callback().masterGain()), juce::dontSendNotification);

    // Status line
    if (!status.message.empty())
        setStatus(status.message, theme_.colours.warning);
    else if (!status.open)
        setStatus("No audio device is open.", theme_.colours.warning);
    else
        setStatus(juce::String(status.running ? "Running" : "Open") + separator + status.deviceType +
                      separator + juce::String(status.activeInputs.size()) + " in / " +
                      juce::String(status.activeOutputs.size()) + " out" + separator +
                      "Dropouts: " + (status.xruns >= 0 ? juce::String(status.xruns) : juce::String("n/a")),
                  theme_.colours.textSecondary);

    if (typeChanged)
        resized();
}

void AudioSetupView::refreshLatency(const audio::AudioDeviceLayer::Status& status)
{
    if (!status.open)
    {
        reportedLatencyValue_.setText(emDash, juce::dontSendNotification);
    }
    else
    {
        juce::String text;
        text << "Input " << ms(status.inputLatencyMilliseconds()) << " (" << status.inputLatencySamples
             << " samples)" << separator << "Output " << ms(status.outputLatencyMilliseconds()) << " ("
             << status.outputLatencySamples << " samples)" << separator << "Round trip "
             << ms(status.inputLatencyMilliseconds() + status.outputLatencyMilliseconds());
        reportedLatencyValue_.setText(text, juce::dontSendNotification);
    }

    const auto calibration = audio_.currentCalibration();
    if (calibration.has_value())
    {
        juce::String text;
        text << "Round trip " << ms(calibration->roundTripMilliseconds()) << " ("
             << calibration->roundTripSamples << " samples)";
        const int reported = status.inputLatencySamples + status.outputLatencySamples;
        const int unreported = calibration->roundTripSamples - reported;
        text << separator << (unreported >= 0 ? "+" : "") << unreported
             << " samples beyond the driver's report";
        measuredLatencyValue_.setText(text, juce::dontSendNotification);
        measuredLatencyValue_.setColour(juce::Label::textColourId, theme_.colours.textPrimary);
    }
    else
    {
        measuredLatencyValue_.setText(audio_.settings().calibration.has_value()
                                          ? "Not measured for this device configuration"
                                          : "Not measured",
                                      juce::dontSendNotification);
        measuredLatencyValue_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
    }
    clearCalibrationButton_.setEnabled(audio_.settings().calibration.has_value());
}

void AudioSetupView::refreshCalibrationChoices(const audio::AudioDeviceLayer::Status& status)
{
    const auto fill =
        [](juce::ComboBox& box, const std::vector<std::string>& names, const std::vector<int>& active)
    {
        const int previous = box.getSelectedId();
        box.clear(juce::dontSendNotification);
        for (size_t i = 0; i < active.size(); ++i)
        {
            const auto channel = static_cast<size_t>(active[i]);
            const juce::String name =
                channel < names.size() ? juce::String(names[channel]) : juce::String(active[i] + 1);
            box.addItem(juce::String(active[i] + 1) + ": " + name, static_cast<int>(i) + 1);
        }
        if (box.getNumItems() > 0)
            box.setSelectedId(previous >= 1 && previous <= box.getNumItems() ? previous : 1,
                              juce::dontSendNotification);
    };
    fill(calibrationOutputBox_, audio_.outputChannelNames(), status.activeOutputs);
    fill(calibrationInputBox_, audio_.inputChannelNames(), status.activeInputs);

    const bool canCalibrate = status.running && !status.activeInputs.empty() && !status.activeOutputs.empty();
    const bool running = audio_.calibrator().isRunning();
    calibrateButton_.setEnabled(canCalibrate || running);
    calibrateButton_.setButtonText(running ? "Cancel" : "Calibrate");
    calibrationOutputBox_.setEnabled(canCalibrate && !running);
    calibrationInputBox_.setEnabled(canCalibrate && !running);
}

// ----- changes ----------------------------------------------------------------------------

void AudioSetupView::deviceTypeChanged()
{
    if (updating_)
        return;
    audio::AudioSettings s = audio_.settings();
    s.deviceType = selectedDeviceType();
    deviceNames_ = audio_.deviceNames(s.deviceType);
    const auto pick = [](const std::vector<std::string>& names, int defaultIndex)
    {
        if (names.empty())
            return std::string{};
        return names[static_cast<size_t>(juce::jlimit(0, static_cast<int>(names.size()) - 1, defaultIndex))];
    };
    s.outputDevice = pick(deviceNames_.outputs, deviceNames_.defaultOutput);
    s.inputDevice = selectedTypeHasSeparateInputs() ? pick(deviceNames_.inputs, deviceNames_.defaultInput)
                                                    : s.outputDevice;
    s.sampleRate = 0.0;
    s.bufferSize = 0;
    s.inputChannels.reset();
    s.outputChannels.reset();
    applySettings(s);
}

void AudioSetupView::deviceChanged()
{
    if (updating_)
        return;
    const auto nameAt = [](const juce::ComboBox& box, const std::vector<std::string>& names)
    {
        const int index = box.getSelectedId() - 2;
        return index >= 0 && static_cast<size_t>(index) < names.size() ? names[static_cast<size_t>(index)]
                                                                       : std::string{};
    };
    audio::AudioSettings s = audio_.settings();
    s.deviceType = selectedDeviceType();
    s.outputDevice = nameAt(outputDeviceBox_, deviceNames_.outputs);
    s.inputDevice =
        selectedTypeHasSeparateInputs() ? nameAt(inputDeviceBox_, deviceNames_.inputs) : s.outputDevice;
    s.inputChannels.reset();
    s.outputChannels.reset();
    applySettings(s);
}

void AudioSetupView::formatChanged()
{
    if (updating_)
        return;
    audio::AudioSettings s = audio_.settings();
    const auto status = audio_.status();
    s.deviceType = status.deviceType;
    s.outputDevice = status.outputDevice;
    s.inputDevice = status.inputDevice;
    s.inputChannels = status.activeInputs;
    s.outputChannels = status.activeOutputs;

    const auto rates = audio_.availableSampleRates();
    const int rateIndex = sampleRateBox_.getSelectedId() - 1;
    s.sampleRate = rateIndex >= 0 && static_cast<size_t>(rateIndex) < rates.size()
                       ? rates[static_cast<size_t>(rateIndex)]
                       : 0.0;
    const auto sizes = audio_.availableBufferSizes();
    const int sizeIndex = bufferSizeBox_.getSelectedId() - 1;
    s.bufferSize = sizeIndex >= 0 && static_cast<size_t>(sizeIndex) < sizes.size()
                       ? sizes[static_cast<size_t>(sizeIndex)]
                       : 0;
    applySettings(s);
}

void AudioSetupView::channelsChanged()
{
    if (updating_)
        return;
    audio::AudioSettings s = audio_.settings();
    const auto status = audio_.status();
    s.deviceType = status.deviceType;
    s.outputDevice = status.outputDevice;
    s.inputDevice = status.inputDevice;
    s.sampleRate = status.sampleRate;
    s.bufferSize = status.bufferSize;
    s.inputChannels = inputsModel_->enabledIndices();
    s.outputChannels = outputsModel_->enabledIndices();
    applySettings(s);
}

void AudioSetupView::midiChanged()
{
    if (updating_)
        return;
    const int row = midiInputsModel_->lastChangedRow();
    if (row >= 0 && static_cast<size_t>(row) < midiInputs_.size() &&
        midiInputsModel_->isEnabled(static_cast<size_t>(row)) !=
            midiInputs_[static_cast<size_t>(row)].enabled)
    {
        audio_.midi().setInputEnabled(midiInputs_[static_cast<size_t>(row)].name,
                                      midiInputsModel_->isEnabled(static_cast<size_t>(row)));
        return;
    }
    const int index = midiOutputBox_.getSelectedId() - 2;
    audio_.midi().setOutput(index >= 0 && static_cast<size_t>(index) < midiOutputs_.size()
                                ? midiOutputs_[static_cast<size_t>(index)].name
                                : std::string{});
}

void AudioSetupView::applySettings(const audio::AudioSettings& settings)
{
    setStatus("Opening device...", theme_.colours.textSecondary);
    if (const auto error = audio_.apply(settings))
        setStatus(*error, theme_.colours.danger);
    // Success is reported through the layer's change message.
}

void AudioSetupView::setStatus(const juce::String& text, juce::Colour colour)
{
    statusLabel_.setText(text, juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::textColourId, colour);
}

// ----- calibration ------------------------------------------------------------------------

void AudioSetupView::calibrateClicked()
{
    auto& calibrator = audio_.calibrator();
    if (calibrator.isRunning())
    {
        calibrator.cancel();
        calibrationStatus_.setText("Calibration cancelled.", juce::dontSendNotification);
        refreshCalibrationChoices(audio_.status());
        return;
    }

    const int output = calibrationOutputBox_.getSelectedId() - 1;
    const int input = calibrationInputBox_.getSelectedId() - 1;
    if (output < 0 || input < 0)
        return;

    juce::Component::SafePointer<AudioSetupView> safe(this);
    const bool started = calibrator.start(input,
                                          output,
                                          calibrationLevel,
                                          [safe](const audio::LatencyCalibrator::Result& result)
                                          {
                                              if (safe != nullptr)
                                                  safe->calibrationFinished(result);
                                          });
    if (!started)
    {
        calibrationStatus_.setText("Could not start calibration: the device is not running.",
                                   juce::dontSendNotification);
        return;
    }
    calibrationStatus_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
    calibrationStatus_.setText("Calibrating...", juce::dontSendNotification);
    refreshCalibrationChoices(audio_.status());
}

void AudioSetupView::calibrationFinished(const audio::LatencyCalibrator::Result& result)
{
    calibrationStatus_.setText(result.message, juce::dontSendNotification);
    calibrationStatus_.setColour(juce::Label::textColourId,
                                 result.measurement.has_value() ? theme_.colours.success
                                                                : theme_.colours.warning);
    if (result.measurement.has_value())
        audio_.storeCalibration(*result.measurement); // triggers a refresh
    else
        refreshCalibrationChoices(audio_.status());
}

} // namespace aural::ui
