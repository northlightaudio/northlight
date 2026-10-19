#include "ui/TrackHeader.h"

namespace aural::ui
{

TrackHeader::TrackHeader()
{
    name_.setFont(juce::FontOptions{theme_.type.body});
    name_.setColour(juce::Label::textColourId, theme_.colours.textPrimary);
    name_.setJustificationType(juce::Justification::centredLeft);
    name_.setMinimumHorizontalScale(1.0f);
    name_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(name_);

    input_.setTextWhenNothingSelected("No input");
    input_.onChange = [this]
    {
        if (updating_)
            return;
        if (midi_)
        {
            const int index = input_.getSelectedId() - 2; // id 1 = all inputs
            if (onMidiInputChanged)
                onMidiInputChanged(trackId_,
                                   index >= 0 && static_cast<size_t>(index) < midiNames_.size()
                                       ? midiNames_[static_cast<size_t>(index)]
                                       : std::string{});
        }
        else if (onInputChanged)
            onInputChanged(trackId_, input_.getSelectedId() - 2); // id 1 = none
    };
    addAndMakeVisible(input_);

    arm_.setClickingTogglesState(true);
    arm_.setColour(juce::TextButton::buttonOnColourId, theme_.colours.danger);
    arm_.setColour(juce::TextButton::textColourOnId, theme_.colours.textPrimary);
    arm_.setTooltip("Arm for recording");
    arm_.onClick = [this]
    {
        if (!updating_ && onArmChanged)
            onArmChanged(trackId_, arm_.getToggleState());
    };
    addAndMakeVisible(arm_);

    monitorButton_.setColour(juce::TextButton::buttonOnColourId, theme_.colours.warning);
    monitorButton_.setColour(juce::TextButton::textColourOnId, theme_.colours.accentText);
    monitorButton_.setTooltip("Input monitoring: off / auto (while armed and not playing) / on");
    monitorButton_.onClick = [this]
    {
        using core::MonitorMode;
        const auto next = monitor_ == MonitorMode::off         ? MonitorMode::automatic
                          : monitor_ == MonitorMode::automatic ? MonitorMode::on
                                                               : MonitorMode::off;
        if (onMonitorChanged)
            onMonitorChanged(trackId_, next);
    };
    addAndMakeVisible(monitorButton_);
}

void TrackHeader::setTrack(const core::Track& track,
                           const std::vector<std::string>& inputNames,
                           const std::vector<std::string>& midiInputNames)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    trackId_ = track.id;
    midi_ = track.isMidi();
    name_.setText(track.name, juce::dontSendNotification);
    name_.setColour(juce::Label::textColourId,
                    track.mute ? theme_.colours.textDisabled : theme_.colours.textPrimary);

    input_.clear(juce::dontSendNotification);
    if (midi_)
    {
        midiNames_ = midiInputNames;
        input_.addItem("All MIDI inputs", 1);
        int selected = 1;
        for (size_t i = 0; i < midiNames_.size(); ++i)
        {
            input_.addItem(midiNames_[i], static_cast<int>(i) + 2);
            if (midiNames_[i] == track.midiInput)
                selected = static_cast<int>(i) + 2;
        }
        if (!track.midiInput.empty() && selected == 1) // chosen but not present now
        {
            input_.addItem(track.midiInput + " (absent)", static_cast<int>(midiNames_.size()) + 2);
            selected = static_cast<int>(midiNames_.size()) + 2;
        }
        input_.setSelectedId(selected, juce::dontSendNotification);
    }
    else
    {
        input_.addItem("No input", 1);
        for (size_t i = 0; i < inputNames.size(); ++i)
            input_.addItem(juce::String(static_cast<int>(i) + 1) + ": " + inputNames[i],
                           static_cast<int>(i) + 2);
        input_.setSelectedId(track.inputChannel >= 0 &&
                                     static_cast<size_t>(track.inputChannel) < inputNames.size()
                                 ? track.inputChannel + 2
                                 : 1,
                             juce::dontSendNotification);
    }

    arm_.setToggleState(track.armed, juce::dontSendNotification);
    armed_ = track.armed;
    monitor_ = track.monitor;
    refreshMonitorButton();
}

void TrackHeader::setLiveState(bool monitoring, bool recording)
{
    if (monitoring == monitoring_ && recording == recording_)
        return;
    monitoring_ = monitoring;
    recording_ = recording;
    arm_.setButtonText(recording_ && armed_ ? "REC" : "R");
    repaint();
}

void TrackHeader::refreshMonitorButton()
{
    using core::MonitorMode;
    monitorButton_.setToggleState(monitor_ != MonitorMode::off, juce::dontSendNotification);
    monitorButton_.setButtonText(monitor_ == MonitorMode::automatic ? "I auto" : "I");
    monitorButton_.setAlpha(monitor_ == MonitorMode::off ? 0.6f : 1.0f);
}

void TrackHeader::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(recording_ && armed_ ? c.surface.overlaidWith(c.danger.withAlpha(0.12f)) : c.surface);
    if (midi_) // a MIDI track: a thin accent stripe at the left edge
    {
        g.setColour(c.accent.withAlpha(0.8f));
        g.fillRect(getLocalBounds().removeFromLeft(3));
    }
    g.setColour(c.border);
    g.fillRect(getLocalBounds().removeFromBottom(1));
    g.fillRect(getLocalBounds().removeFromRight(1));

    // Monitoring indicator: a dot in the top-right corner while the input is heard.
    const auto dot = juce::Rectangle<float>(static_cast<float>(getWidth()) - 16.0f, 8.0f, 8.0f, 8.0f);
    g.setColour(monitoring_ ? c.success : c.borderStrong);
    g.fillEllipse(dot);
}

void TrackHeader::resized()
{
    auto area = getLocalBounds().reduced(theme_.spacing.sm, theme_.spacing.xs);
    name_.setBounds(area.removeFromTop(20).withTrimmedRight(16));
    area.removeFromTop(theme_.spacing.xs);
    auto row = area.removeFromTop(24);
    arm_.setBounds(row.removeFromLeft(36));
    row.removeFromLeft(theme_.spacing.xs);
    monitorButton_.setBounds(row.removeFromLeft(52));
    row.removeFromLeft(theme_.spacing.xs);
    input_.setBounds(row);
}

} // namespace aural::ui
