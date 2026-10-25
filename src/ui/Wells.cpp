#include "ui/Wells.h"

#include <cmath>
#include <map>
#include <tuple>

namespace northlight::ui
{
namespace
{
int renders = 0;
} // namespace

void paintWellFrame(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> area)
{
    const auto shadow = theme.material.enabled && !theme.material.meterWellInset.empty()
                            ? theme.material.meterWellInset.colour
                            : theme.colours.overlay;
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clip;
        clip.addRoundedRectangle(area, theme.radius.sm);
        g.reduceClipRegion(clip);
        const float top = 10.0f, side = 4.0f, bottom = 3.0f;
        g.setGradientFill(juce::ColourGradient::vertical(shadow.withAlpha(0.0f),
                                                         area.getBottom() - bottom,
                                                         shadow.withMultipliedAlpha(0.3f),
                                                         area.getBottom()));
        g.fillRect(area.withTop(area.getBottom() - bottom));
        g.setGradientFill(
            juce::ColourGradient::vertical(shadow, area.getY(), shadow.withAlpha(0.0f), area.getY() + top));
        g.fillRect(area.withHeight(top));
        g.setGradientFill(juce::ColourGradient::horizontal(
            shadow.withMultipliedAlpha(0.5f), area.getX(), shadow.withAlpha(0.0f), area.getX() + side));
        g.fillRect(area.withWidth(side));
        g.setGradientFill(juce::ColourGradient::horizontal(shadow.withAlpha(0.0f),
                                                           area.getRight() - side,
                                                           shadow.withMultipliedAlpha(0.5f),
                                                           area.getRight()));
        g.fillRect(area.withLeft(area.getRight() - side));
    }
    // The rim, then the lip below it.
    g.setColour(shadow.withMultipliedAlpha(0.9f));
    g.drawRoundedRectangle(area.reduced(0.5f), theme.radius.sm, 1.0f);
    g.setColour(theme.material.enabled ? theme.material.grooveLip : theme.colours.lineSubtle);
    g.drawHorizontalLine(
        static_cast<int>(area.getBottom()), area.getX() + theme.radius.sm, area.getRight() - theme.radius.sm);
}

void drawWellFrame(juce::Graphics& g, const Theme& theme, juce::Rectangle<float> area)
{
    // Wells sit on whole pixels; a fractional one (or an odd transform) is drawn
    // directly so the cached image never blurs.
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const bool whole = area.getX() == std::floor(area.getX()) && area.getY() == std::floor(area.getY()) &&
                       area.getWidth() == std::floor(area.getWidth()) &&
                       area.getHeight() == std::floor(area.getHeight());
    if (!whole || area.isEmpty() || scale <= 0.0f || area.getWidth() * area.getHeight() > 1.0e6f)
    {
        paintWellFrame(g, theme, area);
        return;
    }
    // The art reaches a pixel below the well (the lip): the image is one row taller.
    using Key = std::tuple<int, int, int, juce::uint32, juce::uint32, juce::uint32, float, bool>;
    static std::map<Key, juce::Image> cache;
    const Key key{static_cast<int>(area.getWidth()),
                  static_cast<int>(area.getHeight()),
                  juce::roundToInt(scale * 100.0f),
                  theme.colours.overlay.getARGB(),
                  theme.material.meterWellInset.colour.getARGB(),
                  theme.material.grooveLip.getARGB(),
                  theme.radius.sm,
                  theme.material.enabled};
    auto it = cache.find(key);
    if (it == cache.end())
    {
        if (cache.size() >= 256)
            cache.clear(); // a cache, not a store: sizes come back as they are drawn
        const int w = juce::roundToInt((area.getWidth() + 2.0f) * scale);
        const int h = juce::roundToInt((area.getHeight() + 2.0f) * scale);
        juce::Image image(juce::Image::ARGB, w, h, true);
        {
            juce::Graphics ig(image);
            ig.addTransform(juce::AffineTransform::scale(scale));
            paintWellFrame(ig, theme, area.withPosition(1.0f, 1.0f));
        }
        ++renders;
        it = cache.emplace(key, std::move(image)).first;
    }
    g.drawImage(it->second, area.expanded(1.0f), juce::RectanglePlacement::stretchToFit);
}

int wellFrameRenders() noexcept
{
    return renders;
}

} // namespace northlight::ui
