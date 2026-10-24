#include "ui/strip/RenderedCard.h"

namespace northlight::ui::strip
{

// Follows one knob's hover and drag for the face: the knob is "active" from the
// mouse entering it (or pressing it) until it leaves and is let go.
struct RenderedCard::Watch final : juce::MouseListener
{
    Watch(RenderedFace& face, Knob& knob, int control) : face_(face), knob_(&knob), control_(control)
    {
        knob.addMouseListener(this, false);
    }
    ~Watch() override
    {
        if (knob_ != nullptr)
            knob_->removeMouseListener(this);
    }
    void mouseEnter(const juce::MouseEvent&) override { face_.setActiveControl(control_); }
    void mouseDown(const juce::MouseEvent&) override { face_.setActiveControl(control_); }
    void mouseDrag(const juce::MouseEvent&) override // the value changes, round the knob
    {
        if (knob_ != nullptr)
            face_.repaint(knob_->getBounds().expanded(RenderedCard::controlReach));
    }
    void mouseExit(const juce::MouseEvent& e) override
    {
        if (!e.mouseWasDraggedSinceMouseDown() || !e.mods.isAnyMouseButtonDown())
            release();
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (knob_ != nullptr && !knob_->isMouseOver(true))
            release();
    }
    void release()
    {
        if (face_.activeControl() == control_)
            face_.setActiveControl(-1);
    }

    RenderedFace& face_;
    juce::Component::SafePointer<Knob> knob_;
    int control_;
};

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
        watches_.push_back(std::make_unique<Watch>(face_, knob, control));
    knob.setStyle(Knob::Style::rendered);
    knob.setRingSize(juce::roundToInt(skirt));
    knob.setShowValue(false);
    knob.setRenderedPointer(pointer);
    const int side = juce::roundToInt(imageSize);
    knob.setBounds(juce::roundToInt(centre.x) - side / 2, juce::roundToInt(centre.y) - side / 2, side, side);
    // The face, not the card, holds the knob, so it scales with the face.
    face_.addAndMakeVisible(knob);
}

Knob& RenderedCard::bindRenderedKnob(
    int index, juce::Point<float> centre, float skirt, float imageSize, const juce::Image& pointer)
{
    auto& k = addKnob(index);
    if (auto* label = labelFor(k))
        label->setVisible(false);
    placeRenderedKnob(k, centre, skirt, imageSize, pointer, index);
    k.onValueChanged = [this, &k, prior = k.onValueChanged](double v)
    {
        if (prior)
            prior(v);
        controlMoved(k);
    };
    return k;
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
