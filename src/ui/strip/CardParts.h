#pragma once

#include "ui/Theme.h"
#include "ui/strip/ModuleCard.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <initializer_list>
#include <vector>

// Small parts the strip's and the built-ins' cards share: the display's share of
// a body, the handle drawn at a point to drag, and two layouts of knobs (big
// knobs on tiles, and captioned groups in a row).

namespace northlight::ui::strip
{

class CornerChoice;

// The display takes the body above `controlsHeight` of controls. The controls
// come first: in a short dock the display shrinks (to 40 px at worst) rather
// than pushing the knobs out of the card.
inline juce::Rectangle<int> takeDisplay(juce::Rectangle<int>& body, int controlsHeight)
{
    auto display = body.removeFromTop(std::max(40, body.getHeight() - controlsHeight - 6));
    body.removeFromTop(6);
    return display;
}

// A point to drag on a display (design-system.md "Handle"): a 9 px textPrimary
// disc (85% until held) with a 1.5 px ring in the hue, and while held a halo of
// the hue at 25%, 4 px out.
inline void
drawHandle(juce::Graphics& g, const Theme& theme, juce::Colour hue, juce::Point<float> at, bool active)
{
    const auto r = juce::Rectangle<float>(9.0f, 9.0f).withCentre(at);
    if (active)
    {
        g.setColour(hue.withAlpha(0.25f));
        g.fillEllipse(r.expanded(4.0f));
    }
    g.setColour(theme.colours.textPrimary.withAlpha(active ? 1.0f : 0.85f));
    g.fillEllipse(r);
    g.setColour(hue);
    g.drawEllipse(r, 1.5f);
}

// Big knobs, each on a tile of its own across `row` (`gap` between them): the
// knob's stack ends `spacing.sm` above the tile's foot, as far as its ring is
// from the tile's top, with its name in `label`. Returns the tiles, for the card
// to draw.
std::vector<juce::Rectangle<int>>
placeBigKnobs(ModuleCard& card, juce::Rectangle<int> row, std::initializer_list<Knob*> knobs, int gap);

// A row of knob groups (Bloom's, Vesper's): each group as wide as its knobs, a
// captionCaps caption over it with a hairline running on from the words (to a
// chip at the caption's right end, when the group has one), its knobs under it.
class KnobGroups
{
public:
    struct Group
    {
        const char* title;
        std::vector<Knob*> knobs;
        CornerChoice* chip = nullptr;
    };
    struct Spacing
    {
        int cellWidth = 50;      // a knob's column
        int gap = 14;            // between groups
        int captionHeight = 16;  // the caption's row
        int chipExtra = 0;       // a chip may take this much past half the caption
        float chipClearance = 8; // the hairline stops this far short of a chip
    };

    void clear() { captions_.clear(); }
    // A caption of a card's own (a tile's), with or without the hairline.
    void addCaption(const juce::String& text, juce::Rectangle<float> area, bool rule);
    // Lays `groups` out from the left of `row`, knobs and chips included.
    void
    layout(ModuleCard& card, juce::Rectangle<int> row, std::initializer_list<Group> groups, Spacing spacing);
    // The captions and hairlines that reach `clip`.
    void paint(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> clip) const;

private:
    struct Caption
    {
        juce::String text;
        juce::Rectangle<float> area;
        bool rule = true;
        float ruleEnd = 0.0f; // 0: the caption's right
    };
    std::vector<Caption> captions_;
};

} // namespace northlight::ui::strip
