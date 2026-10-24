#include "ui/strip/HardwareKey.h"

#include "ui/components/HardwareParts.h"

#include <cmath>

namespace northlight::ui::strip
{

HardwareKey::HardwareKey(const Theme& theme, juce::String label, juce::String title, bool hasLed)
    : theme_(theme), label_(std::move(label)), hasLed_(hasLed)
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTitle(title);
    setTooltip(title);
}

void HardwareKey::setLit(bool lit)
{
    if (lit == lit_)
        return;
    lit_ = lit;
    setDescription(lit ? "On" : "Off");
    repaint();
}

void HardwareKey::setLabel(const juce::String& label)
{
    label_ = label;
    repaint();
}

void HardwareKey::paintLed(juce::Graphics& g, juce::Point<float> centre) const
{
    const auto& hw = theme_.hardware;
    const bool enabled = isEnabled();
    const auto led = juce::Rectangle<float>(5.0f, 5.0f).withCentre(centre);
    g.setColour(!enabled ? hw.ledOff.withAlpha(0.6f) : lit_ ? hw.led : hw.ledOff);
    g.fillEllipse(led);
    if (lit_ && enabled)
    {
        g.setColour(hw.led.withAlpha(0.25f));
        g.fillEllipse(led.expanded(3.0f));
    }
}

void HardwareKey::paint(juce::Graphics& g)
{
    const auto& hw = theme_.hardware;
    const bool enabled = isEnabled();
    const bool down = lit_ || isMouseButtonDown();
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    // The slot round the key, then the key, pressed in a pixel while down.
    g.setColour(hw.bezel.withMultipliedAlpha(enabled ? 1.0f : 0.5f));
    g.fillRoundedRectangle(r, 3.0f);
    auto key = r.reduced(2.0f).translated(0.0f, down ? 1.0f : 0.0f).withTrimmedBottom(down ? 1.0f : 0.0f);
    g.setGradientFill(juce::ColourGradient(
        hw.keyTop, key.getX(), key.getY(), hw.keyBottom, key.getX(), key.getBottom(), false));
    g.setOpacity(enabled ? 1.0f : 0.5f);
    g.fillRoundedRectangle(key, 2.5f);
    g.setOpacity(1.0f);
    g.setColour(hw.highlight.withAlpha(enabled ? (down ? 0.05f : 0.12f) : 0.04f));
    g.drawHorizontalLine(juce::roundToInt(key.getY()) + 1, key.getX() + 2.0f, key.getRight() - 2.0f);
    if (enabled && isMouseOver(true))
    {
        g.setColour(hw.highlight.withAlpha(isMouseButtonDown() ? 0.10f : 0.07f));
        g.fillRoundedRectangle(key, 2.5f);
    }
    const auto glyph = hw.keyGlyph.withAlpha(enabled ? 1.0f : 0.45f);
    if (icon && label_.isEmpty())
    {
        // An icon alone (a preset key's arrow): centred on the key's face.
        if (hasLed_)
            paintLed(g, {key.getX() + 8.0f, key.getY() + 8.0f});
        g.setColour(glyph);
        const float side = std::round(std::min(key.getWidth(), key.getHeight()) * 0.5f);
        icon(g, juce::Rectangle<float>(side, side).withCentre(key.getCentre()));
    }
    else if (icon)
    {
        // A square key: its LED in the top left corner, the icon over the label at
        // half the space above it, snapped to whole pixels so its lines stay crisp.
        paintLed(g, {key.getX() + 8.0f, key.getY() + 8.0f});
        auto inner = key.reduced(6.0f, 6.0f);
        const auto labelArea = inner.removeFromBottom(16.0f);
        g.setColour(glyph);
        const float iconHeight = std::round(inner.getHeight() * 0.5f);
        icon(g,
             inner.withSizeKeepingCentre(std::round(iconHeight * 1.35f), iconHeight)
                 .withY(std::round(inner.getCentreY() - iconHeight * 0.5f + 2.0f)));
        g.setFont(hardware::legendFont(theme_, 13.0f, 700, 0.08f));
        hardware::drawCrispText(g, label_, labelArea);
    }
    else
    {
        if (hasLed_)
        {
            paintLed(g, {key.getX() + 8.0f, key.getCentreY()});
            key.removeFromLeft(12.0f);
        }
        g.setColour(glyph);
        g.setFont(hardware::legendFont(theme_, 14.0f, 700, 0.06f));
        hardware::drawCrispText(g, label_, key);
    }
    hardware::drawFocusRing(g, *this, r, 3.0f);
}

void HardwareKey::mouseUp(const juce::MouseEvent& e)
{
    if (isEnabled() && getLocalBounds().contains(e.getPosition()))
        press();
    repaint();
}

bool HardwareKey::keyPressed(const juce::KeyPress& key)
{
    if (key != juce::KeyPress::spaceKey && key != juce::KeyPress::returnKey)
        return false;
    press();
    return true;
}

std::unique_ptr<juce::AccessibilityHandler> HardwareKey::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this,
        juce::AccessibilityRole::button,
        juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] { press(); }));
}

void HardwareKey::press()
{
    if (isEnabled() && onPress)
        onPress();
}

} // namespace northlight::ui::strip
