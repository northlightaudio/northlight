#include "ui/components/ChoiceControl.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

ChoiceControl::ChoiceControl(const Theme& theme,
                             const std::vector<std::string>& choices,
                             const juce::String& tooltip)
    : theme_(theme)
{
    for (const auto& c : choices)
        labels_.push_back(juce::String::fromUTF8(c.c_str()));
    if (labels_.size() <= static_cast<size_t>(maxSegments))
    {
        segments_ = std::make_unique<SegmentedControl>(theme_);
        segments_->setSegments(labels_);
        segments_->setTooltip(tooltip);
        segments_->setTitle(tooltip);
        segments_->onChange = [this](int index) { picked(index); };
        addAndMakeVisible(*segments_);
    }
    else
    {
        combo_ = std::make_unique<juce::ComboBox>();
        for (size_t i = 0; i < labels_.size(); ++i)
            combo_->addItem(labels_[i], static_cast<int>(i) + 1);
        combo_->setSelectedId(1, juce::dontSendNotification);
        combo_->setTooltip(tooltip);
        combo_->setTitle(tooltip);
        combo_->onChange = [this] { picked(combo_->getSelectedId() - 1); };
        addAndMakeVisible(*combo_);
    }
}

void ChoiceControl::setValue(float value)
{
    const int index = std::clamp(
        static_cast<int>(std::lround(value)), 0, std::max(0, static_cast<int>(labels_.size()) - 1));
    if (index == selected_ && (segments_ == nullptr || segments_->selected() == index))
        return;
    const juce::ScopedValueSetter<bool> guard(updating_, true);
    selected_ = index;
    if (segments_ != nullptr)
        segments_->setSelected(index, juce::dontSendNotification);
    else
        combo_->setSelectedId(index + 1, juce::dontSendNotification);
}

void ChoiceControl::picked(int index)
{
    if (updating_ || index < 0 || index == selected_)
        return;
    selected_ = index;
    if (onChange)
        onChange(static_cast<float>(index));
}

int ChoiceControl::preferredWidth() const
{
    if (segments_ != nullptr)
        return segments_->preferredWidth();
    int widest = 0;
    for (const auto& label : labels_)
        widest = std::max(widest, juce::GlyphArrangement::getStringWidthInt(theme_.body(), label));
    // The text, the chevron and the padding either side of each.
    return widest + theme_.sizes.icon + 3 * theme_.spacing.sm;
}

void ChoiceControl::setOnCard(bool onCard)
{
    if (segments_ != nullptr)
        segments_->setTrackColour(onCard ? theme_.colours.surface0 : theme_.colours.surface1);
}

void ChoiceControl::resized()
{
    if (segments_ != nullptr)
        segments_->setBounds(getLocalBounds());
    else
        combo_->setBounds(getLocalBounds());
}

} // namespace northlight::ui
