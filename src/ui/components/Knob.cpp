#include "ui/components/Knob.h"

#include "ui/NorthlightLookAndFeel.h"
#include "ui/components/HardwareParts.h"

#include <algorithm>
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
    if (interval_ > 0.0)
        value = minimum_ + std::round((value - minimum_) / interval_) * interval_;
    value_ = juce::jlimit(minimum_, maximum_, value);
    // The value is the tooltip only where the knob does not draw it.
    setTooltip(showValue_ ? juce::String() : text());
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

// The gap between the ring and the value under it, up to 6 px (`spacing.xs` and
// 2), as much as the room left after the value's line allows.
float Knob::valueGap(float below) const
{
    const float line = static_cast<float>(theme_.lineHeight(theme_.fonts.size.value));
    return juce::jlimit(0.0f, static_cast<float>(theme_.spacing.xs + 2), below - line);
}

void Knob::paintPointer(juce::Graphics& g)
{
    const auto& hw = theme_.hardware;
    const float size = static_cast<float>(ringSize());
    auto area = getLocalBounds().toFloat();
    const bool room = showValue_ && area.getHeight() >=
                                        size + static_cast<float>(theme_.lineHeight(theme_.fonts.size.value));
    const auto ring = area.removeFromTop(room ? size : area.getHeight()).withSizeKeepingCentre(size, size);
    if (room)
        area.removeFromTop(valueGap(area.getHeight()));
    const auto centre = ring.getCentre();
    const bool enabled = isEnabled();
    hardware::drawScale(g, theme_, centre, size * 0.5f, startAngle, sweep, 11, 5, enabled);
    hardware::drawPointerKnob(g,
                              theme_,
                              centre,
                              size * 0.72f,
                              startAngle + static_cast<float>(fraction()) * sweep,
                              enabled,
                              isMouseOver(true) || dragging_);
    if (room)
    {
        g.setFont(theme_.value());
        hardware::drawLegend(
            g,
            theme_,
            text(),
            area.withHeight(static_cast<float>(theme_.lineHeight(theme_.fonts.size.value)) + 2.0f),
            juce::Justification::centredTop,
            enabled ? hw.legend : hw.legend.withAlpha(0.45f));
    }
    if (auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
        lnf->drawFocusRing(g, *this, ring, size * 0.5f);
}

void Knob::paintBud(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const float size = static_cast<float>(ringSize());
    auto area = getLocalBounds().toFloat();
    const bool room = showValue_ && area.getHeight() >=
                                        size + static_cast<float>(theme_.lineHeight(theme_.fonts.size.value));
    const auto ring = area.removeFromTop(room ? size : area.getHeight()).withSizeKeepingCentre(size, size);
    if (room)
        area.removeFromTop(valueGap(area.getHeight()));
    const float stroke = std::max(3.0f, std::round(size * 0.085f));
    const auto bounds = ring.reduced(stroke * 1.3f);
    const float radius = bounds.getWidth() * 0.5f;
    const auto centre = bounds.getCentre();
    const bool enabled = isEnabled();
    const bool hot = enabled && (isMouseOver(true) || dragging_);
    const auto hue = !enabled ? c.textDisabled : arcColour_.isTransparent() ? c.accent : arcColour_;
    const juce::PathStrokeType line(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, startAngle + sweep, true);
    g.setColour(hot ? c.surface3 : c.surface2);
    g.strokePath(track, line);

    const bool bipolar = minimum_ < 0.0 && maximum_ > 0.0;
    const float valueAngle = startAngle + static_cast<float>(fraction()) * sweep;
    const float fromAngle =
        bipolar ? startAngle + static_cast<float>((0.0 - minimum_) / (maximum_ - minimum_)) * sweep
                : startAngle;
    if (std::abs(valueAngle - fromAngle) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc(centre.x,
                          centre.y,
                          radius,
                          radius,
                          0.0f,
                          std::min(fromAngle, valueAngle),
                          std::max(fromAngle, valueAngle),
                          true);
        g.setColour(hue);
        g.strokePath(arc, line);
    }
    // The bud: a lit tip at the value, with a soft halo in the hue.
    const auto tip = centre.getPointOnCircumference(radius, valueAngle);
    g.setColour(hue.withAlpha(hot ? 0.4f : 0.25f));
    g.fillEllipse(juce::Rectangle<float>(stroke * 2.6f, stroke * 2.6f).withCentre(tip));
    g.setColour(enabled ? c.textPrimary : c.textDisabled);
    g.fillEllipse(juce::Rectangle<float>(stroke * 1.3f, stroke * 1.3f).withCentre(tip));

    if (room)
    {
        g.setFont(theme_.value());
        g.setColour(enabled ? c.textPrimary : c.textDisabled);
        g.drawText(text(), area.toNearestInt(), juce::Justification::centredTop);
    }
    if (auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
        lnf->drawFocusRing(g, *this, ring, size * 0.5f);
}

bool Knob::hitTest(int x, int y)
{
    if (style_ != Style::rendered)
        return true;
    // Only the skirt takes the mouse: the rest of the bounds is the pointer
    // layer's margin, over the panel.
    const auto centre = getLocalBounds().toFloat().getCentre();
    return centre.getDistanceFrom({static_cast<float>(x), static_cast<float>(y)}) <=
           static_cast<float>(ringSize()) * 0.5f + 2.0f;
}

void Knob::paintRendered(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto centre = area.getCentre();
    const float size = static_cast<float>(ringSize());
    const bool enabled = isEnabled();
    const float angle = pointerAngle();
    if (renderedPointer_.isValid())
    {
        // Shrunk first to the pixels it covers, so turning it only resamples 1:1.
        hardware::drawScaledRotated(g, renderedPointer_, area, angle, enabled ? 1.0f : 0.45f);
    }
    else
    {
        // Without its image: a pointer line over the body the face draws.
        const auto& hw = theme_.hardware;
        g.setColour(enabled ? hw.knobPointer : hw.knobPointer.withAlpha(0.4f));
        g.drawLine(juce::Line<float>(centre.getPointOnCircumference(size * 0.2f, angle),
                                     centre.getPointOnCircumference(size * 0.48f, angle)),
                   std::max(2.0f, size * 0.04f));
    }
    // Hover and drag: the light picks the knob out a little.
    if (enabled && (isMouseOver(true) || dragging_))
    {
        g.setColour(theme_.hardware.highlight.withAlpha(dragging_ ? 0.1f : 0.07f));
        g.fillEllipse(juce::Rectangle<float>(size, size).withCentre(centre));
    }
    if (auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
        lnf->drawFocusRing(g, *this, juce::Rectangle<float>(size, size).withCentre(centre), size * 0.5f);
}

void Knob::paint(juce::Graphics& g)
{
    if (style_ == Style::rendered)
    {
        paintRendered(g);
        return;
    }
    if (style_ == Style::pointer)
    {
        paintPointer(g);
        return;
    }
    if (style_ == Style::bud)
    {
        paintBud(g);
        return;
    }
    const auto& c = theme_.colours;
    const float size = static_cast<float>(ringSize());
    auto area = getLocalBounds().toFloat();
    const bool room = showValue_ && area.getHeight() >=
                                        size + static_cast<float>(theme_.lineHeight(theme_.fonts.size.value));
    auto ring = area.removeFromTop(room ? size : area.getHeight()).withSizeKeepingCentre(size, size);
    if (room)
        area.removeFromTop(valueGap(area.getHeight()));
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

// Hover lightens the knob: it repaints itself (only itself) and tells its owner.
void Knob::mouseEnter(const juce::MouseEvent&)
{
    repaint();
    if (onHover)
        onHover(true);
}

void Knob::mouseExit(const juce::MouseEvent&)
{
    repaint();
    if (onHover)
        onHover(false);
}

void Knob::mouseDown(const juce::MouseEvent& e)
{
    repaint();
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
    repaint();
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
    if (interval_ > 0.0)
        setValue(value_ + direction * interval_, juce::sendNotification);
    else
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
