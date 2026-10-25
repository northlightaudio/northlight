#include "ui/strip/FaceParts.h"

#include "ui/components/HardwareParts.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui::strip
{

// ----- the VU meter ------------------------------------------------------------------------------

VuNeedleMeter::VuNeedleMeter(const Theme& theme, Look look)
    : theme_(theme), look_(look), angle_(-(look.halfSweep + 3.0f))
{
    setInterceptsMouseClicks(false, false);
}

void VuNeedleMeter::setReading(float vu)
{
    const float angle = hardware::vuAngle(vu, look_.halfSweep);
    if (std::abs(angle - angle_) < 0.05f)
        return;
    angle_ = angle;
    setDescription(juce::String(std::max(vu, -20.0f), 1) + " VU");
    repaint();
}

void VuNeedleMeter::paint(juce::Graphics& g)
{
    const auto& hw = theme_.hardware;
    const auto& l = look_;
    g.addTransform(l.transform);
    {
        const juce::Graphics::ScopedSaveState save(g);
        if (!l.clip.isEmpty())
            g.reduceClipRegion(l.clip.toNearestInt());
        const auto base = hardware::polar(l.pivot, l.base, angle_);
        const auto tip = hardware::polar(l.pivot, l.tip, angle_);
        g.setColour(hw.shadow.withAlpha(l.shadowAlpha));
        g.drawLine(juce::Line<float>(base.translated(l.shadowOffset.x, l.shadowOffset.y),
                                     tip.translated(l.shadowOffset.x, l.shadowOffset.y)),
                   l.shadowWidth);
        g.setColour(hw.*l.needle);
        g.drawLine(juce::Line<float>(base, tip), l.width);
    }
    if (const auto& hood = hardware::image(l.folder, l.hoodFile); hood.isValid())
        hardware::drawScaled(g, hood, l.frame);
    else
    {
        // Without its image: the pivot's hood, a dark trapezoid over the needle's foot.
        juce::Path p;
        p.startNewSubPath(l.hoodCentreX - l.hoodFootHalfWidth, l.hoodFoot);
        p.lineTo(l.hoodCentreX - l.hoodTopHalfWidth, l.hoodFoot - l.hoodHeight);
        p.lineTo(l.hoodCentreX + l.hoodTopHalfWidth, l.hoodFoot - l.hoodHeight);
        p.lineTo(l.hoodCentreX + l.hoodFootHalfWidth, l.hoodFoot);
        p.closeSubPath();
        g.setColour(hw.*l.hood);
        g.fillPath(p);
    }
    hardware::drawScaled(g, hardware::image(l.folder, l.glassFile), l.frame);
}

// ----- the jewel lamp ----------------------------------------------------------------------------

JewelLamp::JewelLamp(const Theme& theme, std::function<bool()> lit) : theme_(theme), lit_(std::move(lit)) {}

void JewelLamp::paint(juce::Graphics& g)
{
    const bool lit = lit_();
    const auto& image = hardware::image("corona", lit ? "jewel-lit.png" : "jewel-off.png");
    const auto r = getLocalBounds().toFloat();
    if (image.isValid())
    {
        hardware::drawScaled(g, image, r);
        return;
    }
    // Without its image: the lens, half the jewel's width, where the render has it.
    const auto& hw = theme_.hardware;
    g.setColour(lit ? hw.coronaJewel : hw.coronaJewel.darker(0.7f));
    g.fillEllipse(juce::Rectangle<float>(r.getWidth() * 0.5f, r.getWidth() * 0.5f)
                      .withCentre({r.getCentreX(), r.getWidth() * 0.5f}));
}

// ----- the footswitch ----------------------------------------------------------------------------

Footswitch::Footswitch(const Theme& theme, juce::String name, bool led)
    : theme_(theme), title_(std::move(name)), led_(led)
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTitle(title_ + " footswitch");
    setTooltip(title_ + ": Off");
}

void Footswitch::set(bool on, float glow)
{
    if (on == on_ && std::abs(glow - glow_) < 0.02f)
        return;
    if (on != on_)
    {
        setTooltip(title_ + (on ? ": On" : ": Off"));
        setDescription(on ? "On" : "Off");
    }
    on_ = on;
    glow_ = glow;
    repaint();
}

juce::Rectangle<float> Footswitch::boundsFor(juce::Point<float> centre, float size, bool led)
{
    const float m = size * margin;
    return {centre.x - size * 0.5f - m,
            centre.y - size * 0.5f - m,
            size + 2.0f * m + (led ? ledRoom : 0.0f),
            size + 2.0f * m};
}

juce::Rectangle<float> Footswitch::buttonBounds() const
{
    const float h = static_cast<float>(getHeight());
    const float size = h / (1.0f + 2.0f * margin);
    return {(h - size) * 0.5f, (h - size) * 0.5f, size, size};
}

void Footswitch::paint(juce::Graphics& g)
{
    const auto& hw = theme_.hardware;
    const bool enabled = isEnabled();
    const auto button = buttonBounds();
    const auto& image = hardware::image("corona", isMouseButtonDown() ? "button-down.png" : "button-up.png");
    if (image.isValid())
        hardware::drawScaled(g, image, button.expanded(button.getWidth() * margin), enabled ? 1.0f : 0.5f);
    else
    {
        g.setColour(hw.aluminium.withMultipliedAlpha(enabled ? 1.0f : 0.5f));
        g.fillEllipse(button);
        g.setColour(hw.shadow.withAlpha(0.35f));
        g.drawEllipse(button.reduced(button.getWidth() * 0.18f), 1.5f);
    }
    if (enabled && isMouseOver(true))
    {
        g.setColour(hw.highlight.withAlpha(isMouseButtonDown() ? 0.10f : 0.07f));
        g.fillEllipse(button);
    }
    if (led_)
    {
        // The LED to the button's right.
        const auto led =
            juce::Rectangle<float>(7.0f, 7.0f)
                .withCentre({button.getRight() + button.getWidth() * margin + 4.0f, button.getCentreY()});
        const float level = on_ ? glow_ : 0.0f;
        g.setColour(hw.ledOff);
        g.fillEllipse(led);
        if (level > 0.0f)
        {
            g.setColour(hw.led.withAlpha(level));
            g.fillEllipse(led);
            g.setColour(hw.led.withAlpha(0.3f * level));
            g.fillEllipse(led.expanded(4.0f));
        }
    }
    hardware::drawFocusRing(g, *this, button, button.getWidth() * 0.5f);
}

bool Footswitch::hitTest(int x, int y)
{
    return buttonBounds().expanded(4.0f).contains(static_cast<float>(x), static_cast<float>(y));
}

void Footswitch::press()
{
    if (onPress)
        onPress();
}

void Footswitch::mouseUp(const juce::MouseEvent& e)
{
    if (isEnabled() && buttonBounds().expanded(4.0f).contains(e.position))
        press();
    repaint();
}

bool Footswitch::keyPressed(const juce::KeyPress& key)
{
    if (key != juce::KeyPress::spaceKey && key != juce::KeyPress::returnKey)
        return false;
    press();
    return true;
}

std::unique_ptr<juce::AccessibilityHandler> Footswitch::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(
        *this,
        juce::AccessibilityRole::toggleButton,
        juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] { press(); }));
}

} // namespace northlight::ui::strip
