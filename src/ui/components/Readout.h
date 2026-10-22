#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The geometry the transport's readout well shares between its fields (bars,
// time, tempo, signature) and the header bar's layout (screens/transport.md).
// Each field is as wide as its text plus `inset` on both sides; the bar spaces
// the fields so the text, not the boxes, is evenly apart. Every field draws its
// text on the same baseline, the one that centres the large bars figure.
namespace northlight::ui::readout
{

// Space between a field's edge and its text.
inline int inset(const Theme& theme)
{
    return theme.spacing.xs;
}

// The shared baseline, from the top of a field `height` tall: the large figure's
// line box centred in the field. All fields are the same height and top, so they
// all land on the same row of pixels.
inline int baseline(const Theme& theme, int height)
{
    const auto font = theme.timeLarge();
    return juce::roundToInt(static_cast<float>(height) * 0.5f +
                            (font.getAscent() - font.getDescent()) * 0.5f);
}

// The signature's chevron: the drawn glyph's width, and the space before it.
inline int chevronWidth(const Theme& theme)
{
    return theme.sizes.iconSmall / 2;
}
inline int chevronGap(const Theme& theme)
{
    return theme.spacing.xs;
}

// How far above the baseline the middle of a figure sits in `font`, so a glyph
// beside the text (the chevron) can sit on the figures' centre line.
inline float figureMiddle(const juce::Font& font)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, "0", 0.0f, 0.0f);
    juce::Path outline;
    glyphs.createPath(outline);
    return -outline.getBounds().getCentreY();
}

inline int textWidth(const juce::Font& font, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidthInt(font, text);
}

// The width of a choice field (the signature): its text, then the chevron.
inline int choiceWidth(const Theme& theme, const juce::String& text)
{
    return inset(theme) + textWidth(theme.timeSmall(), text) + chevronGap(theme) + chevronWidth(theme) +
           inset(theme);
}

} // namespace northlight::ui::readout
