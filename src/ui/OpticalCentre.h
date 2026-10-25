#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <utility>

// Text set by its letters, not its line box. Centring a line box (ascent plus
// descent) leaves a control's words low, since the descent below the baseline is
// mostly empty for them; centring the capitals is still low for words that are
// mostly lowercase ("Chamber", "Short"), whose weight is the x-height. A word reads
// centred when the box's middle is half way between the baseline and a height half
// way between the x-height and the capitals; a label of capitals alone ("M", "S")
// by its capitals.

namespace northlight::ui
{

// The capitals' height ("H") and the x-height ("x"), measured once per face and size.
inline std::pair<float, float> letterHeights(const juce::Font& font)
{
    static std::map<std::pair<juce::String, float>, std::pair<float, float>> measured; // UI thread only
    const auto key = std::pair{font.getTypefaceName() + "/" + font.getTypefaceStyle(), font.getHeight()};
    if (const auto it = measured.find(key); it != measured.end())
        return it->second;
    const auto heightOf = [&font](const char* text)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, text, 0.0f, 0.0f);
        juce::Path outline;
        glyphs.createPath(outline);
        return outline.getBounds().getHeight();
    };
    const auto heights = std::pair{heightOf("H"), heightOf("x")};
    measured.emplace(key, heights);
    return heights;
}

// The height a label's letters read at: the mean of the capitals and the x-height
// when it has lowercase letters, else the capitals'.
inline float opticalHeight(const juce::Font& font, const juce::String& text)
{
    const auto [caps, x] = letterHeights(font);
    for (const auto ch : text)
        if (juce::CharacterFunctions::isLowerCase(ch))
            return 0.5f * (caps + x);
    return caps;
}

// How far to move text that JUCE centres by its line box (a baseline half the
// ascent less the descent below the middle) so it centres by its letters instead:
// negative, up. For a drawFittedText that must keep fitting long text.
inline float opticalShift(const juce::Font& font, const juce::String& text)
{
    return 0.5f * opticalHeight(font, text) - 0.5f * (font.getAscent() - font.getDescent());
}

// One line of text centred across `box` and, by its letters, down it.
inline void drawOpticallyCentred(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> box)
{
    const float baseline = box.getCentreY() + 0.5f * opticalHeight(g.getCurrentFont(), text);
    g.drawSingleLineText(text,
                         juce::roundToInt(box.getCentreX()),
                         juce::roundToInt(baseline),
                         juce::Justification::horizontallyCentred);
}

} // namespace northlight::ui
