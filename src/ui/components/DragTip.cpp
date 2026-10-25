#include "ui/components/DragTip.h"

namespace northlight::ui
{

void paintDragTip(juce::Graphics& g,
                  const Theme& theme,
                  const juce::String& text,
                  juce::Point<int> at,
                  juce::Rectangle<int> bounds)
{
    const auto& c = theme.colours;
    g.setFont(theme.caption());
    const int w = juce::GlyphArrangement::getStringWidthInt(theme.caption(), text) + 2 * theme.spacing.sm;
    const int h = theme.sizes.controlCompact;
    auto box = juce::Rectangle<int>(at.x + 14, at.y + 18, w, h);
    if (box.getRight() > bounds.getRight())
        box.setX(at.x - w - 6);
    if (box.getBottom() > bounds.getBottom())
        box.setY(at.y - h - 6);
    g.setColour(c.surface2);
    g.fillRoundedRectangle(box.toFloat(), theme.radius.sm);
    g.setColour(c.lineStrong);
    g.drawRoundedRectangle(box.toFloat().reduced(0.5f), theme.radius.sm, 1.0f);
    g.setColour(c.textPrimary);
    g.drawText(text, box, juce::Justification::centred);
}

} // namespace northlight::ui
