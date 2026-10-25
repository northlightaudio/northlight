#include "ui/strip/CoronaCabinet.h"

#include "ui/components/RenderedFace.h"

#include <cmath>

namespace northlight::ui::strip::coronacabinet
{

namespace
{
constexpr float pi = juce::MathConstants<float>::pi;
} // namespace

using hardware::legendFont;

void Baffle::paint(juce::Graphics& g)
{
    const auto& hw = theme_.hardware;
    const auto speakers = ef::speakers(model == pc::custom ? pc::open2x12 : model);
    const bool dim = !cabinetOn || model == pc::custom;
    const char* file = model == pc::closed4x10 || model == pc::bass4x10 ? "speaker-10.png"
                       : model == pc::bass1x15                          ? "speaker-15.png"
                                                                        : "speaker-12.png";
    for (const auto& s : speakers)
    {
        const auto box = juce::Rectangle<float>(s.diameter, s.diameter).withCentre(local({s.x, s.y}));
        const auto& image = part(file);
        if (image.isValid())
            hardware::drawScaled(g, image, box, dim ? 0.45f : 1.0f);
        else
        {
            // Without its image: the rim, the cone and the dust cap.
            g.setColour(hw.coronaSpeakerRim.withMultipliedAlpha(dim ? 0.45f : 1.0f));
            g.fillEllipse(box);
            g.setColour(hw.coronaCone.withMultipliedAlpha(dim ? 0.45f : 1.0f));
            g.fillEllipse(box.reduced(s.diameter * 0.06f));
            g.setColour(hw.shadow.withAlpha(0.35f));
            for (const float ring : {0.3f, 0.45f, 0.6f})
                g.drawEllipse(box.reduced(s.diameter * ring * 0.5f), 1.0f);
            g.setColour(hw.knobBody.withMultipliedAlpha(dim ? 0.45f : 1.0f));
            g.fillEllipse(
                juce::Rectangle<float>(s.diameter * 0.22f, s.diameter * 0.22f).withCentre(box.getCentre()));
        }
    }
    if (message.isNotEmpty())
    {
        const auto area = getLocalBounds().toFloat().reduced(60.0f, 0.0f);
        const auto band = juce::Rectangle<float>(area.getWidth(), 64.0f).withCentre(area.getCentre());
        g.setColour(hw.coronaBaffle.withAlpha(0.88f));
        g.fillRoundedRectangle(band, 6.0f);
        g.setColour(hw.coronaStripInk);
        g.setFont(legendFont(theme_, 20.0f, 700, 0.1f));
        text(g, message, band);
        return;
    }
    if (speakers.empty())
        return;
    // The mics: B first, so A (the main mic) is on top where they cross.
    for (int m = 1; m >= 0; --m)
        paintMic(g, speakers.front(), m);
}

juce::Rectangle<float> Baffle::capsule(int m) const
{
    const auto speakers = ef::speakers(model == pc::custom ? pc::open2x12 : model);
    if (speakers.empty())
        return {};
    const auto& mic = mics[static_cast<size_t>(m)];
    const auto c = local(ef::micPoint(speakers.front(), m, mic.position));
    const float size =
        ef::capsuleSize[static_cast<size_t>(std::clamp(mic.type, 0, 2))] * ef::capsuleScale(mic.distanceCm);
    return juce::Rectangle<float>(size, size).withCentre(c);
}

void Baffle::typeGlyph(juce::Graphics& g, int type, juce::Rectangle<float> box, float stroke)
{
    const juce::PathStrokeType line(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    if (type == pc::ribbon)
    {
        const auto body = box.withSizeKeepingCentre(box.getWidth() * 0.62f, box.getHeight() * 0.86f);
        p.addRoundedRectangle(body, stroke * 1.5f);
        for (int i = 1; i <= 4; ++i)
        {
            const float y = body.getY() + body.getHeight() * static_cast<float>(i) / 5.0f;
            p.startNewSubPath(body.getX() + stroke * 1.6f, y);
            p.lineTo(body.getRight() - stroke * 1.6f, y);
        }
    }
    else if (type == pc::condenser)
    {
        const auto head = box.withSizeKeepingCentre(box.getWidth() * 0.86f, box.getHeight() * 0.86f);
        p.addEllipse(head);
        p.startNewSubPath(head.getX() + head.getWidth() * 0.14f, head.getCentreY());
        p.lineTo(head.getRight() - head.getWidth() * 0.14f, head.getCentreY());
        p.startNewSubPath(head.getCentreX(), head.getY() + head.getHeight() * 0.14f);
        p.lineTo(head.getCentreX(), head.getBottom() - head.getHeight() * 0.14f);
    }
    else
    {
        const auto head = juce::Rectangle<float>(box.getWidth() * 0.44f, box.getHeight() * 0.56f)
                              .withCentre({box.getCentreX(), box.getY() + box.getHeight() * 0.3f});
        p.addRoundedRectangle(head, head.getWidth() * 0.5f);
        p.startNewSubPath(head.getX() + stroke, head.getY() + head.getHeight() * 0.38f);
        p.lineTo(head.getRight() - stroke, head.getY() + head.getHeight() * 0.38f);
        // The clip round its foot, and the stem.
        p.addCentredArc(box.getCentreX(),
                        head.getBottom() - head.getHeight() * 0.25f,
                        head.getWidth() * 0.85f,
                        head.getWidth() * 0.85f,
                        0.0f,
                        pi * 0.5f,
                        pi * 1.5f,
                        true);
        p.startNewSubPath(box.getCentreX(), head.getBottom() + head.getWidth() * 0.6f);
        p.lineTo(box.getCentreX(), box.getBottom());
    }
    g.strokePath(p, line);
}

void Baffle::paintMic(juce::Graphics& g, const ef::Speaker& speaker, int m) const
{
    juce::ignoreUnused(speaker);
    const auto& hw = theme_.hardware;
    const auto& mic = mics[static_cast<size_t>(m)];
    const auto colour = m == 0 ? hw.coronaMarkerA : hw.coronaMarkerB;
    const float alpha = mic.on ? 1.0f : 0.35f;
    const auto box = capsule(m);
    const auto c = box.getCentre();
    const float squeeze = std::cos(mic.angleDeg * pi / 180.0f);
    const auto ring = box.withSizeKeepingCentre(box.getWidth() * squeeze, box.getHeight());

    // A dark halo keeps the ring clear over the cone's light and dark rings.
    g.setColour(hw.shadow.withAlpha(0.45f * alpha));
    g.drawEllipse(ring, 4.5f);
    g.setColour(colour.withAlpha(0.16f * alpha));
    g.fillEllipse(ring);
    g.setColour(colour.withAlpha(alpha));
    if (mic.on)
        g.drawEllipse(ring, 2.0f);
    else
    {
        juce::Path outline;
        outline.addEllipse(ring);
        juce::Path dashed;
        const float dashes[] = {5.0f, 4.0f};
        juce::PathStrokeType(2.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.fillPath(dashed);
    }
    const float glyph = std::clamp(box.getHeight() * 0.48f, 14.0f, 24.0f);
    typeGlyph(g, mic.type, juce::Rectangle<float>(glyph, glyph).withCentre(c), glyph > 18.0f ? 1.8f : 1.4f);

    // Its label: the mic's letter and distance, on the side away from the cap.
    const auto* face = dynamic_cast<const RenderedFace*>(getParentComponent());
    const bool compact = face != nullptr && face->isCompact();
    const float labelW = compact ? 122.0f : 86.0f, labelH = compact ? 30.0f : 22.0f;
    const auto label = juce::Rectangle<float>(labelW, labelH)
                           .withCentre({c.x + box.getWidth() * 0.5f + labelW * 0.5f + 7.0f,
                                        c.y + (m == 0 ? -box.getHeight() * 0.35f : box.getHeight() * 0.35f)});
    g.setColour(hw.coronaBaffle.withAlpha(0.85f * alpha));
    g.fillRoundedRectangle(label, 4.0f);
    g.setColour(colour.withAlpha(alpha));
    g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({label.getX() + 10.0f, label.getCentreY()}));
    g.setColour(hw.coronaStripInk.withAlpha(alpha));
    g.setFont(legendFont(theme_, compact ? 15.0f * RenderedFace::compactNameScale : 15.0f, 700, 0.04f));
    const auto cm = mic.distanceCm < 10.0f ? juce::String(mic.distanceCm, 1)
                                           : juce::String(juce::roundToInt(mic.distanceCm));
    text(g,
         juce::String(m == 0 ? "A" : "B") + juce::String::fromUTF8(" \xc2\xb7 ") + cm + " cm",
         label.withTrimmedLeft(14.0f));
}

} // namespace northlight::ui::strip::coronacabinet
