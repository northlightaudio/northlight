#include "ui/components/Knob.h"

#include "ui/NorthlightLookAndFeel.h"

#include <cmath>

namespace northlight::ui
{

namespace
{
// UI Automation sees the control as a slider with a readable value (11.3).
template <typename Owner> class ValueInterface final : public juce::AccessibilityValueInterface
{
public:
    explicit ValueInterface(Owner& owner) : owner_(owner) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return owner_.accessibleValue(); }
    juce::String getCurrentValueAsString() const override { return owner_.accessibleValueText(); }
    void setValue(double newValue) override { owner_.setAccessibleValue(newValue); }
    void setValueAsString(const juce::String& newValue) override
    {
        owner_.setAccessibleValue(newValue.getDoubleValue());
    }
    AccessibleValueRange getRange() const override
    {
        const auto range = owner_.accessibleRange();
        return {{range.first, range.second}, owner_.accessibleStep()};
    }

private:
    Owner& owner_;
};
} // namespace

namespace
{
constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f; // 7 o'clock
constexpr float sweep = juce::MathConstants<float>::pi * 1.5f;       // to 5 o'clock
} // namespace

Knob::Knob(const Theme& theme) : theme_(theme)
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
    format = [](double v) { return juce::String(v, 2); };
}

void Knob::setRange(double minimum, double maximum, double defaultValue)
{
    minimum_ = minimum;
    maximum_ = std::max(maximum, minimum + 1.0e-9);
    default_ = juce::jlimit(minimum_, maximum_, defaultValue);
    logarithmic_ = logarithmic_ && minimum_ > 0.0;
    setValue(juce::jlimit(minimum_, maximum_, value_), juce::dontSendNotification);
}

void Knob::setValue(double value, juce::NotificationType notify)
{
    value_ = juce::jlimit(minimum_, maximum_, value);
    setTooltip(text());
    repaint();
    if (notify != juce::dontSendNotification && onValueChanged)
        onValueChanged(value_);
}

void Knob::setLogarithmic(bool logarithmic)
{
    logarithmic_ = logarithmic && minimum_ > 0.0;
    repaint();
}

double Knob::fraction() const noexcept
{
    if (logarithmic_)
        return std::log(value_ / minimum_) / std::log(maximum_ / minimum_);
    return (value_ - minimum_) / (maximum_ - minimum_);
}

void Knob::setFraction(double f, bool notify)
{
    f = juce::jlimit(0.0, 1.0, f);
    const double value =
        logarithmic_ ? minimum_ * std::pow(maximum_ / minimum_, f) : minimum_ + f * (maximum_ - minimum_);
    setValue(value, notify ? juce::sendNotification : juce::dontSendNotification);
}

juce::String Knob::text() const
{
    return format ? format(value_) : juce::String(value_, 2);
}

void Knob::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const float size = static_cast<float>(ringSize());
    auto area = getLocalBounds().toFloat();
    const bool room = showValue_ && area.getHeight() >= size + theme_.fonts.size.value + 2.0f;
    auto ring = area.removeFromTop(room ? size : area.getHeight()).withSizeKeepingCentre(size, size);
    const float stroke = size > 40.0f ? 4.0f : 3.0f;
    const auto arcBounds = ring.reduced(stroke * 0.5f + 1.0f);
    const float radius = arcBounds.getWidth() * 0.5f;
    const auto centre = arcBounds.getCentre();

    const auto& m = theme_.material;
    if (m.enabled)
    {
        // The body: a radial gradient lit from the upper left, inside the rim.
        const auto body = arcBounds.reduced(stroke * 0.5f + 1.0f);
        g.setGradientFill(juce::ColourGradient(m.knobBodyLight,
                                               body.getX() + body.getWidth() * 0.3f,
                                               body.getY() + body.getHeight() * 0.3f,
                                               m.knobBodyDark,
                                               body.getRight(),
                                               body.getBottom(),
                                               true));
        g.fillEllipse(body);
        g.setColour(m.knobHighlight);
        juce::Path highlight;
        highlight.addCentredArc(centre.x,
                                centre.y,
                                body.getWidth() * 0.5f - 1.0f,
                                body.getHeight() * 0.5f - 1.0f,
                                0.0f,
                                -juce::MathConstants<float>::pi * 0.75f,
                                -juce::MathConstants<float>::pi * 0.25f,
                                true);
        g.strokePath(highlight, juce::PathStrokeType(1.0f));
    }
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, startAngle + sweep, true);
    g.setColour(m.enabled ? m.knobRim : c.surface2);
    g.strokePath(track,
                 juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = minimum_ < 0.0 && maximum_ > 0.0;
    const float valueAngle = startAngle + static_cast<float>(fraction()) * sweep;
    const float fromAngle =
        bipolar ? startAngle + static_cast<float>((0.0 - minimum_) / (maximum_ - minimum_)) * sweep
                : startAngle;
    juce::Path arc;
    arc.addCentredArc(centre.x,
                      centre.y,
                      radius,
                      radius,
                      0.0f,
                      std::min(fromAngle, valueAngle),
                      std::max(fromAngle, valueAngle),
                      true);
    g.setColour(isEnabled() ? (arcColour_.isTransparent() ? c.accent : arcColour_) : c.textDisabled);
    g.strokePath(arc,
                 juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The indicator line from the centre towards the value.
    const auto tip = centre.getPointOnCircumference(radius - stroke * 0.5f, valueAngle);
    const auto base = centre.getPointOnCircumference(radius * 0.45f, valueAngle);
    g.setColour(isEnabled() ? c.textPrimary : c.textDisabled);
    g.drawLine({base, tip}, 1.5f);

    if (room)
    {
        g.setFont(theme_.value());
        g.setColour(c.textSecondary);
        g.drawText(text(), area.toNearestInt(), juce::Justification::centredTop);
    }
    if (auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
        lnf->drawFocusRing(g, *this, ring, size * 0.5f);
}

void Knob::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragStartValue_ = fraction();
    dragStartY_ = e.y;
    dragging_ = true;
    if (onDragStart)
        onDragStart();
}

void Knob::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_)
        return;
    const double pixelsForFullTravel = e.mods.isShiftDown() ? 1000.0 : 150.0;
    setFraction(dragStartValue_ + static_cast<double>(dragStartY_ - e.y) / pixelsForFullTravel, true);
}

void Knob::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_)
        return;
    dragging_ = false;
    if (onDragEnd)
        onDragEnd();
}

void Knob::mouseDoubleClick(const juce::MouseEvent&)
{
    if (onDragStart)
        onDragStart();
    setValue(default_, juce::sendNotification);
    if (onDragEnd)
        onDragEnd();
}

void Knob::step(double direction, bool fine)
{
    if (onDragStart)
        onDragStart();
    setFraction(fraction() + direction * (fine ? 0.005 : 0.02), true);
    if (onDragEnd)
        onDragEnd();
}

void Knob::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    step(wheel.deltaY > 0 ? 1.0 : -1.0, e.mods.isShiftDown());
}

bool Knob::keyPressed(const juce::KeyPress& key)
{
    const bool fine = key.getModifiers().isShiftDown();
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::rightKey)
        step(1.0, fine);
    else if (key == juce::KeyPress::downKey || key == juce::KeyPress::leftKey)
        step(-1.0, fine);
    else if (key == juce::KeyPress::homeKey)
        setFraction(1.0, true);
    else if (key == juce::KeyPress::endKey)
        setFraction(0.0, true);
    else
        return false;
    return true;
}

std::unique_ptr<juce::AccessibilityHandler> Knob::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this,
        juce::AccessibilityRole::slider,
        juce::AccessibilityActions(),
        juce::AccessibilityHandler::Interfaces{std::make_unique<ValueInterface<Knob>>(*this)});
}

} // namespace northlight::ui
