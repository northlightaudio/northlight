#include "ui/components/PanelHeader.h"

#include "ui/NorthlightLookAndFeel.h"

namespace northlight::ui
{

PanelHeader::PanelHeader(const Theme& theme) : theme_(theme)
{
    for (auto* button : {&detach_, &close_})
    {
        button->getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
        button->getProperties().set(NorthlightLookAndFeel::propertyIcon, true);
        addChildComponent(*button);
    }
    detach_.setTooltip("Detach into its own window");
    close_.setTooltip("Close");
    detach_.onClick = [this]
    {
        if (onDetach)
            onDetach();
    };
    close_.onClick = [this]
    {
        if (onClose)
            onClose();
    };
}

void PanelHeader::setTitle(const juce::String& title)
{
    title_ = title;
    repaint();
}

void PanelHeader::refreshButtons()
{
    detach_.setVisible(static_cast<bool>(onDetach));
    close_.setVisible(static_cast<bool>(onClose));
    resized();
}

void PanelHeader::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    g.setColour(c.lineSubtle);
    g.fillRect(getLocalBounds().removeFromBottom(1));
    g.setColour(c.textTertiary);
    g.setFont(theme_.captionCaps());
    g.drawText(title_.toUpperCase(),
               getLocalBounds().reduced(theme_.spacing.md, 0),
               juce::Justification::centredLeft);
}

void PanelHeader::resized()
{
    auto area = getLocalBounds().reduced(theme_.spacing.xs);
    const int side = theme_.sizes.controlCompact;
    if (close_.isVisible())
    {
        close_.setBounds(area.removeFromRight(side).withSizeKeepingCentre(side, side));
        area.removeFromRight(theme_.spacing.xs);
    }
    if (detach_.isVisible())
        detach_.setBounds(area.removeFromRight(side).withSizeKeepingCentre(side, side));
}

} // namespace northlight::ui
