#include "ui/components/ProgressRow.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

namespace
{
int labelHeight(const Theme& theme)
{
    return static_cast<int>(std::ceil(theme.fonts.size.body * theme.fonts.lineHeight));
}
} // namespace

void ProgressRow::setProgress(double fraction, const juce::String& label)
{
    fraction = juce::jlimit(0.0, 1.0, fraction);
    if (fraction == fraction_ && label == label_)
        return;
    fraction_ = fraction;
    label_ = label;
    repaint();
}

int ProgressRow::preferredHeight(const Theme& theme)
{
    return labelHeight(theme) + theme.spacing.sm + theme.spacing.xs;
}

void ProgressRow::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    auto area = getLocalBounds();
    g.setFont(theme_.body());
    g.setColour(c.textSecondary);
    g.drawText(label_, area.removeFromTop(labelHeight(theme_)), juce::Justification::centredLeft, true);
    const float barH = static_cast<float>(theme_.spacing.xs);
    const auto track = area.removeFromBottom(theme_.spacing.xs).toFloat();
    g.setColour(c.surface2);
    g.fillRoundedRectangle(track, barH * 0.5f);
    if (fraction_ > 0.0)
    {
        g.setColour(c.accent);
        g.fillRoundedRectangle(
            track.withWidth(std::max(barH, track.getWidth() * static_cast<float>(fraction_))), barH * 0.5f);
    }
}

} // namespace northlight::ui
