#include "ui/strip/RenderedCard.h"

namespace northlight::ui::strip
{

RenderedCard::~RenderedCard() = default;

RenderedCard::RenderedCard(
    const Theme& theme, CardHost& host, core::Id channelId, const core::Insert& insert, RenderedFace& face)
    : ModuleCard(theme, host, channelId, insert), face_(face)
{
}

int RenderedCard::preferredWidth() const
{
    return juce::roundToInt(face_.designWidth()) + 2 * theme_.spacing.sm;
}

int RenderedCard::preferredBodyHeight() const
{
    return juce::roundToInt(face_.designHeight()) + 2 * theme_.spacing.xs;
}

int RenderedCard::minimumBodyHeight() const
{
    return juce::roundToInt(face_.designHeight() * 0.5f) + 2 * theme_.spacing.xs;
}

int RenderedCard::preferredWidthAt(int bodyHeight) const
{
    return face_.widthForHeight(bodyHeight - 2 * theme_.spacing.xs) + 2 * theme_.spacing.sm;
}

void RenderedCard::placeRenderedKnob(Knob& knob,
                                     juce::Point<float> centre,
                                     float skirt,
                                     float imageSize,
                                     const juce::Image& pointer,
                                     int control)
{
    if (control >= 0)
        watchActive(knob, control);
    knob.setStyle(Knob::Style::rendered);
    knob.setRingSize(juce::roundToInt(skirt));
    knob.setShowValue(false);
    knob.setRenderedPointer(pointer);
    const int side = juce::roundToInt(imageSize);
    knob.setBounds(juce::roundToInt(centre.x) - side / 2, juce::roundToInt(centre.y) - side / 2, side, side);
    // The face, not the card, holds the knob, so it scales with the face.
    face_.addAndMakeVisible(knob);
}

// The knob is the face's "active" control from the pointer entering it (or
// pressing it) until it has left and been let go: in compact the face shows its
// value where its name is.
void RenderedCard::watchActive(Knob& knob, int control)
{
    const auto release = [this, control]
    {
        if (face_.activeControl() == control)
            face_.setActiveControl(-1);
    };
    knob.onHover = [this, &knob, control, release](bool inside)
    {
        if (inside)
            face_.setActiveControl(control, printArea(knob));
        else if (!knob.isDragging())
            release();
    };
    knob.onDragStart = [this, &knob, control, prior = knob.onDragStart]
    {
        if (prior)
            prior();
        face_.setActiveControl(control, printArea(knob));
    };
    knob.onDragEnd = [&knob, release, prior = knob.onDragEnd]
    {
        if (prior)
            prior();
        if (!knob.isMouseOver(true))
            release();
    };
}

Knob& RenderedCard::bindRenderedKnob(
    int index, juce::Point<float> centre, float skirt, float imageSize, const juce::Image& pointer)
{
    auto& k = addKnob(index);
    if (auto* label = labelFor(k))
        label->setVisible(false);
    placeRenderedKnob(k, centre, skirt, imageSize, pointer, index);
    renderedKnobs_[index] = &k;
    k.onValueChanged = [this, &k, prior = k.onValueChanged](double v)
    {
        if (prior)
            prior(v);
        controlMoved(k);
    };
    return k;
}

Knob* RenderedCard::knobFor(int index) const
{
    const auto it = renderedKnobs_.find(index);
    return it != renderedKnobs_.end() ? it->second : nullptr;
}

void RenderedCard::repaintChanged()
{
    if (shown_.size() != values().size())
    {
        shown_ = values();
        face_.repaint();
        return;
    }
    for (size_t i = 0; i < values().size(); ++i)
    {
        if (shown_[i] == values()[i])
            continue;
        shown_[i] = values()[i];
        const int index = static_cast<int>(i);
        if (printsWidely(index))
        {
            shown_ = values();
            face_.repaint();
            return;
        }
        for (const int shown : alsoChanges(index))
            if (auto* k = knobFor(shown))
                face_.repaint(printArea(*k));
        if (auto* k = knobFor(index))
            face_.repaint(printArea(*k));
    }
}

HardwareLever& RenderedCard::addLever(const juce::String& name,
                                      std::vector<juce::String> positions,
                                      const juce::String& folder,
                                      std::array<juce::String, 3> files,
                                      juce::Point<float> nut,
                                      float scale,
                                      float nutSize)
{
    auto lever =
        std::make_unique<HardwareLever>(theme_, name, std::move(positions), folder, std::move(files));
    // The render is 119 x 178.5 at full size, its nut centred in the top square.
    const float w = 119.0f * scale, h = 178.5f * scale;
    lever->setNutSize(nutSize);
    lever->setBounds(juce::Rectangle<float>(nut.x - w * 0.5f, nut.y - w * 0.5f, w, h).toNearestInt());
    face_.addAndMakeVisible(*lever);
    levers_.push_back(std::move(lever));
    return *levers_.back();
}

} // namespace northlight::ui::strip
