#include "ui/InsertEditor.h"

#include "plugins/BuiltIns.h"

namespace aural::ui
{

InsertEditor::InsertEditor(core::Id channelId, core::Id insertId, const engine::InsertProcessor& processor)
    : channelId_(channelId), insertId_(insertId), infos_(processor.parameters())
{
    setOpaque(true);
    title_.setText(plugins::BuiltInFactory::displayName(processor.typeName()), juce::dontSendNotification);
    title_.setFont(juce::FontOptions{theme_.type.title});
    title_.setColour(juce::Label::textColourId, theme_.colours.textPrimary);
    addAndMakeVisible(title_);

    bypass_.onClick = [this]
    {
        if (!updating_ && onCommand)
            onCommand(
                std::make_unique<core::SetInsertBypass>(channelId_, insertId_, bypass_.getToggleState()));
    };
    addAndMakeVisible(bypass_);

    mixLabel_.setText("Mix", juce::dontSendNotification);
    mixLabel_.setFont(juce::FontOptions{theme_.type.body});
    mixLabel_.setColour(juce::Label::textColourId, theme_.colours.textSecondary);
    addAndMakeVisible(mixLabel_);
    mix_.setSliderStyle(juce::Slider::LinearHorizontal);
    mix_.setRange(0.0, 100.0, 1.0);
    mix_.setTextValueSuffix(" %");
    mix_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    mix_.setDoubleClickReturnValue(true, 100.0);
    mix_.setValue(100.0, juce::dontSendNotification);
    mix_.onValueChange = [this]
    {
        if (!updating_ && onCommand)
            onCommand(std::make_unique<core::SetInsertMix>(
                channelId_, insertId_, static_cast<float>(mix_.getValue() / 100.0)));
    };
    addAndMakeVisible(mix_);

    for (size_t i = 0; i < infos_.size(); ++i)
    {
        const auto& info = infos_[i];
        auto label = std::make_unique<juce::Label>();
        label->setText(info.name, juce::dontSendNotification);
        label->setFont(juce::FontOptions{theme_.type.body});
        label->setColour(juce::Label::textColourId, theme_.colours.textSecondary);
        addAndMakeVisible(*label);
        labels_.push_back(std::move(label));

        auto slider =
            std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        const double step = info.unit == "Hz" ? 1.0 : 0.01;
        slider->setRange(info.minimum, info.maximum, step);
        if (info.logarithmic && info.minimum > 0.0f)
            slider->setSkewFactorFromMidPoint(std::sqrt(static_cast<double>(info.minimum) * info.maximum));
        slider->setDoubleClickReturnValue(true, info.defaultValue);
        slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
        slider->setTextValueSuffix(info.unit.empty() ? juce::String() : " " + juce::String(info.unit));
        slider->setValue(info.defaultValue, juce::dontSendNotification);
        const int index = static_cast<int>(i);
        slider->onDragStart = [this, index]
        {
            if (onGestureBegin)
                onGestureBegin(infos_[static_cast<size_t>(index)].name);
        };
        slider->onDragEnd = [this]
        {
            if (onGestureEnd)
                onGestureEnd();
        };
        slider->onValueChange = [this, index]
        {
            if (!updating_ && onCommand)
                onCommand(std::make_unique<core::SetInsertParameter>(
                    channelId_,
                    insertId_,
                    index,
                    static_cast<float>(sliders_[static_cast<size_t>(index)]->getValue())));
        };
        addAndMakeVisible(*slider);
        sliders_.push_back(std::move(slider));
    }
    setSize(preferredWidth, preferredHeight(static_cast<int>(infos_.size())));
}

void InsertEditor::setInsert(const core::Insert& insert)
{
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    bypass_.setToggleState(insert.bypass, juce::dontSendNotification);
    mix_.setValue(insert.mix * 100.0, juce::dontSendNotification);
    for (size_t i = 0; i < sliders_.size() && i < insert.params.size(); ++i)
        sliders_[i]->setValue(insert.params[i], juce::dontSendNotification);
}

void InsertEditor::paint(juce::Graphics& g)
{
    g.fillAll(theme_.colours.background);
}

void InsertEditor::resized()
{
    auto area = getLocalBounds().reduced(theme_.spacing.lg, theme_.spacing.md);
    auto header = area.removeFromTop(theme_.sizes.controlHeight);
    bypass_.setBounds(header.removeFromRight(90));
    title_.setBounds(header);
    area.removeFromTop(theme_.spacing.sm);
    auto mixRow = area.removeFromTop(30);
    mixLabel_.setBounds(mixRow.removeFromLeft(100));
    mix_.setBounds(mixRow);
    for (size_t i = 0; i < sliders_.size(); ++i)
    {
        auto row = area.removeFromTop(30);
        labels_[i]->setBounds(row.removeFromLeft(100));
        sliders_[i]->setBounds(row);
    }
}

} // namespace aural::ui
