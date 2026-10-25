#include "ui/timeline/ClipPainter.h"

namespace northlight::ui::clips
{

void paintBody(juce::Graphics& g,
               const Theme& theme,
               juce::Rectangle<int> block,
               int headerHeight,
               juce::Colour body,
               juce::Colour header)
{
    g.setColour(body);
    g.fillRoundedRectangle(block.toFloat(), theme.radius.sm);
    if (headerHeight <= 0)
        return;
    g.setColour(header);
    juce::Path band;
    band.addRoundedRectangle(static_cast<float>(block.getX()),
                             static_cast<float>(block.getY()),
                             static_cast<float>(block.getWidth()),
                             static_cast<float>(headerHeight),
                             theme.radius.sm,
                             theme.radius.sm,
                             true,
                             true,
                             false,
                             false);
    g.fillPath(band);
}

void paintHatch(juce::Graphics& g,
                juce::Rectangle<int> body,
                juce::Rectangle<int> visible,
                juce::Colour colour,
                float dim)
{
    g.saveState();
    g.reduceClipRegion(body.getIntersection(visible));
    g.setColour(colour.withAlpha(0.10f * dim));
    for (int x = visible.getX() - body.getHeight(); x < visible.getRight(); x += 6)
        g.drawLine(static_cast<float>(x),
                   static_cast<float>(body.getBottom()),
                   static_cast<float>(x + body.getHeight()),
                   static_cast<float>(body.getY()),
                   1.0f);
    g.restoreState();
}

void paintOutline(juce::Graphics& g, const Theme& theme, juce::Rectangle<int> block, juce::Colour colour)
{
    g.setColour(colour);
    g.drawRoundedRectangle(block.toFloat().reduced(0.5f), theme.radius.sm, 1.0f);
}

juce::Rectangle<int> labelArea(
    juce::Rectangle<int> block, juce::Rectangle<int> visible, int headerHeight, int leftInset, int rightInset)
{
    return (headerHeight > 0 ? block.withHeight(headerHeight) : block)
        .getIntersection(visible)
        .withTrimmedLeft(leftInset)
        .withTrimmedRight(rightInset);
}

void paintLabel(juce::Graphics& g,
                const Theme& theme,
                juce::Rectangle<int> area,
                const juce::String& text,
                juce::Colour colour)
{
    g.setColour(colour);
    g.setFont(theme.caption());
    g.drawText(text, area, juce::Justification::centredLeft, true);
}

} // namespace northlight::ui::clips
