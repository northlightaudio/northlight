#include "ui/strip/CardParts.h"

#include "ui/strip/SynthParts.h"

#include <algorithm>

namespace northlight::ui::strip
{

std::vector<juce::Rectangle<int>>
placeBigKnobs(ModuleCard& card, juce::Rectangle<int> row, std::initializer_list<Knob*> knobs, int gap)
{
    const auto& theme = card.theme();
    const int count = static_cast<int>(knobs.size());
    const int tileW = (row.getWidth() - (count - 1) * gap) / std::max(1, count);
    std::vector<juce::Rectangle<int>> tiles;
    for (auto* k : knobs)
    {
        auto tile = row.removeFromLeft(tileW);
        row.removeFromLeft(gap);
        tiles.push_back(tile);
        if (auto* label = card.labelFor(*k))
            label->setFont(theme.label());
        card.placeKnob(
            *k, tile.reduced(theme.spacing.xs, 0).withTrimmedTop(theme.spacing.sm), theme.fonts.size.label);
    }
    return tiles;
}

void KnobGroups::addCaption(const juce::String& text, juce::Rectangle<float> area, bool rule)
{
    captions_.push_back({text, area, rule, 0.0f});
}

void KnobGroups::layout(ModuleCard& card,
                        juce::Rectangle<int> row,
                        std::initializer_list<Group> groups,
                        Spacing spacing)
{
    bool first = true;
    for (const auto& group : groups)
    {
        if (!first)
            row.removeFromLeft(spacing.gap);
        first = false;
        const int count = static_cast<int>(group.knobs.size());
        auto cell = row.removeFromLeft(count * spacing.cellWidth);
        auto caption = cell.removeFromTop(spacing.captionHeight);
        float ruleEnd = 0.0f;
        if (group.chip != nullptr)
        {
            const int w = std::min(caption.getWidth() / 2 + spacing.chipExtra, group.chip->preferredWidth());
            group.chip->setBounds(caption.removeFromRight(w).withSizeKeepingCentre(w, 16));
            ruleEnd = static_cast<float>(group.chip->getX()) - spacing.chipClearance;
        }
        captions_.push_back({group.title, caption.toFloat(), true, ruleEnd});
        cell.removeFromTop(card.theme().spacing.xs);
        card.layoutKnobs(cell, count, group.knobs);
    }
}

void KnobGroups::paint(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> clip) const
{
    const auto& c = theme.colours;
    const auto font = theme.captionCaps();
    g.setFont(font);
    for (const auto& cap : captions_)
    {
        const float right = cap.ruleEnd > 0.0f ? cap.ruleEnd : cap.area.getRight();
        if (!cap.area.withRight(std::max(right, cap.area.getRight())).intersects(clip))
            continue;
        g.setColour(c.textTertiary);
        g.drawText(cap.text, cap.area, juce::Justification::centredLeft);
        if (!cap.rule)
            continue;
        const float x = cap.area.getX() +
                        static_cast<float>(juce::GlyphArrangement::getStringWidthInt(font, cap.text)) + 8.0f;
        if (right > x)
        {
            g.setColour(c.textPrimary.withAlpha(0.08f));
            g.fillRect(juce::Rectangle<float>(x, cap.area.getCentreY(), right - x, 1.0f));
        }
    }
}

} // namespace northlight::ui::strip
