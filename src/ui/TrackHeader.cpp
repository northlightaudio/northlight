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
        if (!updating_ && onInputChanged)
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

void TrackHeader::setTrack(const core::Track& track, const std::vector<std::string>& inputNames)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    trackId_ = track.id;
    name_.setText(track.name, juce::dontSendNotification);
    name_.setColour(juce::Label::textColourId,
                    track.mute ? theme_.colours.textDisabled : theme_.colours.textPrimary);

    input_.clear(juce::dontSendNotification);
    input_.addItem("No input", 1);
    for (size_t i = 0; i < inputNames.size(); ++i)
        input_.addItem(juce::String(static_cast<int>(i) + 1) + ": " + inputNames[i], static_cast<int>(i) + 2);
    input_.setSelectedId(track.inputChannel >= 0 &&
                                 static_cast<size_t>(track.inputChannel) < inputNames.size()
                             ? track.inputChannel + 2
                             : 1,
                         juce::dontSendNotification);

    arm_.setToggleState(track.armed, juce::dontSendNotification);
    monitor_ = track.monitor;
    refreshMonitorButton();
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
    g.fillAll(c.surface);
    g.setColour(c.border);
    g.fillRect(getLocalBounds().removeFromBottom(1));
    g.fillRect(getLocalBounds().removeFromRight(1));
}

void TrackHeader::resized()
{
    auto area = getLocalBounds().reduced(theme_.spacing.sm, theme_.spacing.xs);
    name_.setBounds(area.removeFromTop(20));
    area.removeFromTop(theme_.spacing.xs);
    auto row = area.removeFromTop(24);
    arm_.setBounds(row.removeFromLeft(28));
    row.removeFromLeft(theme_.spacing.xs);
    monitorButton_.setBounds(row.removeFromLeft(52));
    row.removeFromLeft(theme_.spacing.xs);
    input_.setBounds(row);
}

} // namespace aural::ui
