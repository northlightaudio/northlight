#include "ui/components/HardwareParts.h"

#include "ui/NorthlightLookAndFeel.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace northlight::ui::hardware
{

void drawPointerKnob(juce::Graphics& g,
                     const Theme& theme,
                     juce::Point<float> centre,
                     float diameter,
                     float angle,
                     bool enabled,
                     bool hover)
{
    const auto& hw = theme.hardware;
    // The body in a 100-unit box, pointing up: a tapering pointer into a round base.
    juce::Path body;
    body.startNewSubPath(50.0f, 4.0f);
    body.cubicTo(58.0f, 4.0f, 62.0f, 30.0f, 66.0f, 44.0f);
    constexpr float quarter = juce::MathConstants<float>::pi * 0.25f;
    body.addCentredArc(50.0f, 60.0f, 22.6f, 22.6f, 0.0f, quarter, 7.0f * quarter, false);
    body.cubicTo(38.0f, 30.0f, 42.0f, 4.0f, 50.0f, 4.0f);
    body.closeSubPath();
    const auto place = juce::AffineTransform::translation(-50.0f, -50.0f)
                           .scaled(diameter / 100.0f)
                           .rotated(angle)
                           .translated(centre.x, centre.y);
    const float unit = diameter / 100.0f;

    // Its shadow on the panel, below and a little soft.
    for (int i = 3; i >= 1; --i)
    {
        g.setColour(hw.shadow.withAlpha(0.16f));
        g.fillPath(body, place.translated(0.0f, unit * 2.5f * static_cast<float>(i)));
    }
    const auto bodyColour = hover && enabled ? hw.knobBody.brighter(0.25f) : hw.knobBody;
    g.setGradientFill(juce::ColourGradient(bodyColour.brighter(0.35f),
                                           centre.x - diameter * 0.3f,
                                           centre.y - diameter * 0.35f,
                                           bodyColour,
                                           centre.x + diameter * 0.2f,
                                           centre.y + diameter * 0.3f,
                                           false));
    g.fillPath(body, place);
    g.setColour(hw.highlight.withAlpha(0.08f));
    g.strokePath(body, juce::PathStrokeType(1.0f), place);

    // The cream line along the pointer, and the cap over the base.
    const auto from = juce::Point<float>(50.0f, 9.0f).transformedBy(place);
    const auto to = juce::Point<float>(50.0f, 40.0f).transformedBy(place);
    g.setColour(enabled ? hw.knobPointer : hw.knobPointer.withAlpha(0.4f));
    g.drawLine({from, to}, std::max(1.5f, 3.2f * unit));
    const auto cap = juce::Point<float>(50.0f, 60.0f).transformedBy(place);
    const float capRadius = 14.0f * unit;
    g.setGradientFill(juce::ColourGradient(hw.knobCap.brighter(0.3f),
                                           cap.x - capRadius * 0.5f,
                                           cap.y - capRadius * 0.6f,
                                           hw.knobCap,
                                           cap.x + capRadius * 0.4f,
                                           cap.y + capRadius * 0.6f,
                                           false));
    g.fillEllipse(juce::Rectangle<float>(2.0f * capRadius, 2.0f * capRadius).withCentre(cap));
}

void drawScale(juce::Graphics& g,
               const Theme& theme,
               juce::Point<float> centre,
               float radius,
               float from,
               float sweep,
               int count,
               int major,
               bool enabled)
{
    const auto& hw = theme.hardware;
    const float stroke = radius > 30.0f ? 1.6f : 1.3f;
    for (int i = 0; i < count; ++i)
    {
        const float a = from + sweep * static_cast<float>(i) / static_cast<float>(std::max(1, count - 1));
        const bool big = major > 0 && i % major == 0;
        const auto inner = centre.getPointOnCircumference(radius * (big ? 0.80f : 0.87f), a);
        const auto outer = centre.getPointOnCircumference(radius, a);
        g.setColour(hw.legendShadow.withMultipliedAlpha(0.6f));
        g.drawLine({inner.translated(0.0f, 1.0f), outer.translated(0.0f, 1.0f)}, stroke);
        g.setColour(hw.legend.withAlpha(enabled ? (big ? 0.95f : 0.7f) : 0.35f));
        g.drawLine({inner, outer}, stroke);
    }
}

void drawLegend(juce::Graphics& g,
                const Theme& theme,
                const juce::String& text,
                juce::Rectangle<float> area,
                juce::Justification justification,
                juce::Colour colour)
{
    g.setColour(theme.hardware.legendShadow);
    g.drawText(text, area.translated(0.0f, 1.0f), justification, false);
    g.setColour(theme.hardware.legendShadow.withMultipliedAlpha(0.4f));
    g.drawText(text, area.translated(1.0f, 1.0f), justification, false);
    g.setColour(colour);
    g.drawText(text, area, justification, false);
}

const juce::Image& image(const juce::String& folder, const juce::String& file)
{
    JUCE_ASSERT_MESSAGE_THREAD
    static std::map<juce::String, juce::Image> cache;
    const auto key = folder + "/" + file;
    if (const auto it = cache.find(key); it != cache.end())
        return it->second;
    const auto path = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                          .getParentDirectory()
                          .getChildFile("assets")
                          .getChildFile(folder)
                          .getChildFile(file);
    return cache[key] = path.existsAsFile() ? juce::ImageFileFormat::loadFrom(path) : juce::Image();
}

const juce::Image& scaled(const juce::Image& source, int width, int height)
{
    JUCE_ASSERT_MESSAGE_THREAD
    static std::map<std::tuple<const void*, int, int>, juce::Image> cache;
    width = std::max(1, width);
    height = std::max(1, height);
    if (!source.isValid() || (width >= source.getWidth() && height >= source.getHeight()))
        return source;
    const auto key = std::make_tuple(static_cast<const void*>(source.getPixelData().get()), width, height);
    if (const auto it = cache.find(key); it != cache.end())
        return it->second;
    if (cache.size() > 512)
        cache.clear(); // a face scaled through many sizes: start again rather than grow
    auto image = source;
    while (image.getWidth() >= 2 * width && image.getHeight() >= 2 * height)
        image = image.rescaled(
            image.getWidth() / 2, image.getHeight() / 2, juce::Graphics::highResamplingQuality);
    if (image.getWidth() != width || image.getHeight() != height)
        image = image.rescaled(width, height, juce::Graphics::highResamplingQuality);
    return cache[key] = image;
}

void drawScaled(juce::Graphics& g, const juce::Image& source, juce::Rectangle<float> area, float opacity)
{
    if (!source.isValid() || area.isEmpty())
        return;
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto& image =
        scaled(source, juce::roundToInt(area.getWidth() * scale), juce::roundToInt(area.getHeight() * scale));
    g.setOpacity(opacity);
    g.drawImage(image, area, juce::RectanglePlacement::stretchToFit);
    g.setOpacity(1.0f);
}

void drawScaledRotated(
    juce::Graphics& g, const juce::Image& source, juce::Rectangle<float> area, float angle, float opacity)
{
    if (!source.isValid() || area.isEmpty())
        return;
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const auto& image =
        scaled(source, juce::roundToInt(area.getWidth() * scale), juce::roundToInt(area.getHeight() * scale));
    const auto centre = area.getCentre();
    const auto place = juce::AffineTransform::scale(area.getWidth() / static_cast<float>(image.getWidth()),
                                                    area.getHeight() / static_cast<float>(image.getHeight()))
                           .translated(area.getX(), area.getY())
                           .rotated(angle, centre.x, centre.y);
    const juce::Graphics::ScopedSaveState save(g);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.setOpacity(opacity);
    g.drawImageTransformed(image, place);
}

void drawPlate(juce::Graphics& g,
               const juce::Image& source,
               juce::Rectangle<float> area,
               float margin,
               juce::Colour flat,
               float cornerRadius)
{
    if (source.isValid())
    {
        drawScaled(g, source, area.expanded(margin));
        return;
    }
    g.setColour(flat);
    g.fillRoundedRectangle(area, cornerRadius);
}

void drawTicks(juce::Graphics& g,
               juce::Point<float> centre,
               float inner,
               float outer,
               int count,
               float thickness,
               int majorEvery,
               float majorExtra,
               float sweepDegrees)
{
    const juce::PathStrokeType stroke(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    for (int i = 0; i < count; ++i)
    {
        const float a = -0.5f * sweepDegrees +
                        sweepDegrees * static_cast<float>(i) / static_cast<float>(std::max(1, count - 1));
        const float from = majorEvery > 0 && i % majorEvery == 0 ? inner - majorExtra : inner;
        juce::Path p;
        p.startNewSubPath(polar(centre, from, a));
        p.lineTo(polar(centre, outer, a));
        g.strokePath(p, stroke);
    }
}

namespace
{
// The outlines' bounds of a run of glyphs: where its ink is.
juce::Rectangle<float> inkBounds(const juce::GlyphArrangement& glyphs)
{
    juce::Path outline;
    glyphs.createPath(outline);
    return outline.getBounds();
}
} // namespace

void drawCrispText(juce::Graphics& g,
                   const juce::String& text,
                   juce::Rectangle<float> area,
                   juce::Justification justification,
                   bool centreInk)
{
    if (text.isEmpty())
        return;
    const float k = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (k <= 0.0f)
        return;
    const juce::Graphics::ScopedSaveState save(g);
    // From here on, one unit is one device pixel.
    g.addTransform(juce::AffineTransform::scale(1.0f / k));
    const auto font = g.getCurrentFont().withHeight(g.getCurrentFont().getHeight() * k);
    const juce::Rectangle<float> box(
        area.getX() * k, area.getY() * k, area.getWidth() * k, area.getHeight() * k);

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f); // baseline at 0
    const auto bounds = glyphs.getBoundingBox(0, -1, true);
    const float width = bounds.getRight();
    float x = box.getX();
    if (justification.testFlags(juce::Justification::horizontallyCentred))
    {
        // Centred on the letters as they are seen: by their outlines, not their
        // advances (a glyph's box is its advance, which ends in the tracking
        // added after the last letter).
        const auto ink = inkBounds(glyphs);
        x = box.getCentreX() - (ink.getX() + ink.getRight()) * 0.5f;
    }
    else if (justification.testFlags(juce::Justification::right))
        x = box.getRight() - width;
    // Vertically as drawText does: the font's height placed in the box, the
    // baseline its ascent below that.
    float top = box.getY();
    if (justification.testFlags(juce::Justification::verticallyCentred))
        top = box.getCentreY() - font.getHeight() * 0.5f;
    else if (justification.testFlags(juce::Justification::bottom))
        top = box.getBottom() - font.getHeight();
    float baseline = top + font.getAscent();
    if (centreInk && justification.testFlags(juce::Justification::verticallyCentred))
    {
        const auto ink = inkBounds(glyphs); // relative to a baseline at 0
        baseline = box.getCentreY() - (ink.getY() + ink.getBottom()) * 0.5f;
    }
    glyphs.moveRangeOfGlyphs(0, -1, std::round(x), std::round(baseline));
    glyphs.draw(g);
}

juce::Point<float> polar(juce::Point<float> centre, float radius, float degrees)
{
    const float a = juce::degreesToRadians(degrees);
    return {centre.x + radius * std::sin(a), centre.y - radius * std::cos(a)};
}

juce::Font legendFont(const Theme& theme, float size, int weight, float trackingEm)
{
    return theme.font(Theme::Face::legend, size, weight).withExtraKerningFactor(trackingEm);
}

void drawFocusRing(juce::Graphics& g, juce::Component& component, juce::Rectangle<float> area, float radius)
{
    if (auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&component.getLookAndFeel()))
        lnf->drawFocusRing(g, component, area, radius);
}

float fitScale(float bodyWidth, float bodyHeight, float faceWidth, float faceHeight) noexcept
{
    if (bodyWidth <= 0.0f || bodyHeight <= 0.0f || faceWidth <= 0.0f || faceHeight <= 0.0f)
        return 0.0f;
    return std::min({1.0f, bodyWidth / faceWidth, bodyHeight / faceHeight});
}

int widthForHeight(int bodyHeight, float faceWidth, float faceHeight) noexcept
{
    const float scale = std::clamp(static_cast<float>(bodyHeight) / faceHeight, 0.0f, 1.0f);
    return static_cast<int>(std::lround(faceWidth * scale));
}

} // namespace northlight::ui::hardware
