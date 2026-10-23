#include "ui/components/HardwareParts.h"

#include <algorithm>
#include <map>

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

} // namespace northlight::ui::hardware
