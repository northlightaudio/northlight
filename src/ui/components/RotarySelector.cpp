#include "ui/components/RotarySelector.h"

#include "ui/Motion.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/components/HardwareParts.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

RotarySelector::RotarySelector(const Theme& theme) : theme_(theme), hue_(theme.colours.accent)
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
}

RotarySelector::~RotarySelector()
{
    motion::Tween::cancel(*this);
}

void RotarySelector::setPositions(std::vector<juce::String> labels,
                                  std::vector<juce::String> fullNames,
                                  juce::String name)
{
    labels_ = std::move(labels);
    fullNames_ = std::move(fullNames);
    name_ = std::move(name);
    selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(labels_.size()) - 1));
    shown_ = static_cast<float>(selected_);
    updateTooltip();
    repaint();
}

void RotarySelector::setHue(juce::Colour hue)
{
    hue_ = hue;
    repaint();
}

void RotarySelector::setSelected(int index, juce::NotificationType notification)
{
    index = std::clamp(index, 0, std::max(0, static_cast<int>(labels_.size()) - 1));
    if (index == selected_)
        return;
    selected_ = index;
    // The model moved it (an undo, automation): the pointer goes there at once.
    motion::Tween::cancel(*this);
    shown_ = static_cast<float>(index);
    updateTooltip();
    repaint();
    if (notification != juce::dontSendNotification && onChange)
        onChange(index);
}

void RotarySelector::choose(int index, bool fromUser)
{
    index = std::clamp(index, 0, std::max(0, static_cast<int>(labels_.size()) - 1));
    if (index == selected_)
        return;
    selected_ = index;
    updateTooltip();
    // The pointer turns over the toggle time.
    const float from = shown_;
    const float to = static_cast<float>(index);
    juce::Component::SafePointer<RotarySelector> safe(this);
    motion::Tween::cancel(*this);
    motion::Tween::run(*this,
                       theme_.motion.ms(theme_.motion.toggleMs),
                       [safe, from, to](float t)
                       {
                           if (safe == nullptr)
                               return;
                           const float eased = 1.0f - (1.0f - t) * (1.0f - t);
                           safe->shown_ = from + (to - from) * eased;
                           safe->repaint();
                       });
    if (fromUser && onChange)
        onChange(index);
}

void RotarySelector::updateTooltip()
{
    const auto at = static_cast<size_t>(selected_);
    const auto value = at < fullNames_.size() ? fullNames_[at]
                       : at < labels_.size()  ? labels_[at]
                                              : juce::String();
    setTooltip(name_.isNotEmpty() ? name_ + ": " + value : value);
}

juce::Point<float> RotarySelector::centre() const
{
    const float labelRoom = static_cast<float>(labelReach) + 6.0f;
    return {getWidth() * 0.5f, labelRoom + bodySize * 0.5f};
}

float RotarySelector::angleOf(float position) const
{
    // Centred on the top, 45 degrees apart, never wider than 270 in all.
    const int n = static_cast<int>(labels_.size());
    if (n <= 1)
        return 0.0f;
    const float step = std::min(juce::degreesToRadians(45.0f), juce::degreesToRadians(270.0f) / (n - 1));
    return (position - 0.5f * static_cast<float>(n - 1)) * step;
}

juce::Point<float> RotarySelector::labelCentre(int index) const
{
    // The label's near edge a fixed gap past its dot, whatever its width: a side
    // label reaches out by half its width, a top one by half its height.
    const float angle = angleOf(static_cast<float>(index));
    const auto font = theme_.caption();
    const float w = juce::GlyphArrangement::getStringWidth(font, labels_[static_cast<size_t>(index)]);
    const float h = font.getHeight();
    const float extent = std::abs(std::sin(angle)) * w * 0.5f + std::abs(std::cos(angle)) * h * 0.5f;
    return centre().getPointOnCircumference(bodySize * 0.5f + 4.0f + 5.0f + extent, angle);
}

int RotarySelector::positionAt(juce::Point<float> p) const
{
    // The nearest label within reach (labels and their dots are the targets).
    int best = -1;
    float bestDistance = 14.0f;
    for (int i = 0; i < static_cast<int>(labels_.size()); ++i)
        if (const float d = labelCentre(i).getDistanceFrom(p); d < bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    return best;
}

void RotarySelector::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto& m = theme_.material;
    const bool enabled = isEnabled();
    const auto mid = centre();
    const float radius = bodySize * 0.5f;
    const auto body = juce::Rectangle<float>(bodySize, bodySize).withCentre(mid);
    const bool hover = enabled && isMouseOver(true) && hoverLabel_ < 0;

    if (hardware_)
    {
        paintHardware(g, hover);
        return;
    }
    // The rim and the body, as a knob's.
    g.setColour(m.enabled ? m.knobRim : c.surface2);
    g.fillEllipse(body);
    const auto inner = body.reduced(3.0f);
    if (m.enabled)
    {
        g.setGradientFill(juce::ColourGradient(hover ? m.knobBodyLight.brighter(0.08f) : m.knobBodyLight,
                                               inner.getX() + inner.getWidth() * 0.3f,
                                               inner.getY() + inner.getHeight() * 0.3f,
                                               m.knobBodyDark,
                                               inner.getRight(),
                                               inner.getBottom(),
                                               true));
        g.fillEllipse(inner);
        juce::Path highlight;
        highlight.addCentredArc(mid.x,
                                mid.y,
                                inner.getWidth() * 0.5f - 1.0f,
                                inner.getHeight() * 0.5f - 1.0f,
                                0.0f,
                                -juce::MathConstants<float>::pi * 0.75f,
                                -juce::MathConstants<float>::pi * 0.25f,
                                true);
        g.setColour(m.knobHighlight);
        g.strokePath(highlight, juce::PathStrokeType(hover ? 1.5f : 1.0f));
    }
    else
    {
        g.setColour(hover ? c.surface3 : c.surface2.brighter(0.05f));
        g.fillEllipse(inner);
    }

    // The positions: a dot outside the rim and the label beyond it.
    g.setFont(theme_.caption());
    for (int i = 0; i < static_cast<int>(labels_.size()); ++i)
    {
        const float angle = angleOf(static_cast<float>(i));
        const bool chosen = i == selected_;
        const auto dot = mid.getPointOnCircumference(radius + 4.0f, angle);
        g.setColour(!enabled ? c.textDisabled : chosen ? hue_ : c.textTertiary);
        g.fillEllipse(juce::Rectangle<float>(chosen ? 3.0f : 2.0f, chosen ? 3.0f : 2.0f).withCentre(dot));
        const auto label =
            juce::Rectangle<float>(2.0f * labelHalfWidth + 8.0f,
                                   static_cast<float>(theme_.lineHeight(theme_.fonts.size.caption)))
                .withCentre(labelCentre(i));
        g.setColour(!enabled           ? c.textDisabled
                    : chosen           ? c.textPrimary
                    : i == hoverLabel_ ? c.textSecondary
                                       : c.textTertiary);
        g.drawText(labels_[static_cast<size_t>(i)], label, juce::Justification::centred, false);
    }

    // The pointer, from near the centre to the rim.
    const float angle = angleOf(shown_);
    const auto tip = mid.getPointOnCircumference(radius - 4.0f, angle);
    const auto base = mid.getPointOnCircumference(radius * 0.3f, angle);
    g.setColour(enabled ? c.textPrimary : c.textDisabled);
    g.drawLine({base, tip}, 2.0f);

    if (auto* laf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
        laf->drawFocusRing(g, *this, body, radius);
}

void RotarySelector::paintHardware(juce::Graphics& g, bool hover)
{
    const auto& hw = theme_.hardware;
    const bool enabled = isEnabled();
    const auto mid = centre();
    const float radius = bodySize * 0.5f;
    // The positions: a printed dot and legend each, the chosen one lit.
    g.setFont(theme_.caption());
    for (int i = 0; i < static_cast<int>(labels_.size()); ++i)
    {
        const float angle = angleOf(static_cast<float>(i));
        const bool chosen = i == selected_;
        const auto dot = mid.getPointOnCircumference(radius + 4.0f, angle);
        g.setColour(!enabled ? hw.legend.withAlpha(0.3f) : chosen ? hw.led : hw.legend.withAlpha(0.7f));
        g.fillEllipse(juce::Rectangle<float>(chosen ? 4.0f : 2.5f, chosen ? 4.0f : 2.5f).withCentre(dot));
        const auto label =
            juce::Rectangle<float>(2.0f * labelHalfWidth + 8.0f,
                                   static_cast<float>(theme_.lineHeight(theme_.fonts.size.caption)))
                .withCentre(labelCentre(i));
        const float alpha = !enabled ? 0.35f : chosen ? 1.0f : i == hoverLabel_ ? 0.9f : 0.62f;
        hardware::drawLegend(g,
                             theme_,
                             labels_[static_cast<size_t>(i)],
                             label,
                             juce::Justification::centred,
                             hw.legend.withAlpha(alpha));
    }
    hardware::drawPointerKnob(g, theme_, mid, bodySize * 1.05f, angleOf(shown_), enabled, hover);
    if (auto* laf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
        laf->drawFocusRing(g, *this, juce::Rectangle<float>(bodySize, bodySize).withCentre(mid), radius);
}

void RotarySelector::mouseMove(const juce::MouseEvent& e)
{
    const int over = positionAt(e.position);
    if (over != hoverLabel_)
    {
        hoverLabel_ = over;
        repaint();
    }
}

void RotarySelector::mouseExit(const juce::MouseEvent&)
{
    hoverLabel_ = -1;
    repaint();
}

void RotarySelector::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled())
        return;
    dragging_ = false;
    clickedLabel_ = false;
    if (const int at = positionAt(e.position); at >= 0)
    {
        clickedLabel_ = true;
        choose(at, true);
        return;
    }
    dragOrigin_ = e.getScreenY();
    dragStart_ = selected_;
}

void RotarySelector::mouseDrag(const juce::MouseEvent& e)
{
    if (!isEnabled() || clickedLabel_)
        return;
    const int steps = (dragOrigin_ - e.getScreenY()) / dragStep;
    if (!dragging_ && steps != 0)
    {
        dragging_ = true;
        if (onGestureBegin)
            onGestureBegin();
    }
    if (dragging_)
        choose(dragStart_ + steps, true);
}

void RotarySelector::mouseUp(const juce::MouseEvent&)
{
    if (dragging_ && onGestureEnd)
        onGestureEnd();
    dragging_ = false;
}

void RotarySelector::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (isEnabled() && positionAt(e.position) < 0)
        choose(default_, true);
}

void RotarySelector::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (!isEnabled() || wheel.deltaY == 0.0f)
        return;
    choose(selected_ + (wheel.deltaY > 0.0f ? 1 : -1) * (wheel.isReversed ? -1 : 1), true);
}

bool RotarySelector::keyPressed(const juce::KeyPress& key)
{
    if (!isEnabled())
        return false;
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey)
        choose(selected_ + 1, true);
    else if (code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey)
        choose(selected_ - 1, true);
    else if (code == juce::KeyPress::homeKey)
        choose(0, true);
    else if (code == juce::KeyPress::endKey)
        choose(static_cast<int>(labels_.size()) - 1, true);
    else
        return false;
    return true;
}

void RotarySelector::focusGained(FocusChangeType)
{
    repaint();
}

void RotarySelector::focusLost(FocusChangeType)
{
    repaint();
}

} // namespace northlight::ui
