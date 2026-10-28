#include "ui/NorthlightLookAndFeel.h"

#include "ui/Motion.h"
#include "ui/OpticalCentre.h"
#include "ui/components/Readout.h"

#include <atomic>
#include <cmath>

namespace northlight::ui
{

namespace
{
std::atomic<bool> keyboardFocus{false};

juce::String variantOf(const juce::Component& component)
{
    return component.getProperties()
        .getWithDefault(NorthlightLookAndFeel::propertyVariant, "secondary")
        .toString();
}
} // namespace

const juce::Identifier NorthlightLookAndFeel::propertyVariant{"variant"};
const juce::Identifier NorthlightLookAndFeel::propertySemantic{"semantic"};
const juce::Identifier NorthlightLookAndFeel::propertyHardOn{"hardOn"};
const juce::Identifier NorthlightLookAndFeel::propertyMenu{"menu"};
const juce::Identifier NorthlightLookAndFeel::propertyIcon{"icon"};

void NorthlightLookAndFeel::setKeyboardFocusVisible(bool visible) noexcept
{
    if (keyboardFocus.exchange(visible, std::memory_order_relaxed) == visible)
        return;
    // Show or clear the ring now, not at the focused component's next repaint.
    if (juce::MessageManager::existsAndIsCurrentThread())
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
            focused->repaint();
}

bool NorthlightLookAndFeel::keyboardFocusVisible() noexcept
{
    return keyboardFocus.load(std::memory_order_relaxed);
}

NorthlightLookAndFeel::NorthlightLookAndFeel(const Theme& theme) : theme_(theme)
{
    if (theme_.fonts.ui != nullptr)
        setDefaultSansSerifTypeface(theme_.fonts.ui);
    refreshColours();

    // Any click ends keyboard-driven focus rings; focus moved by Tab or an arrow,
    // in any window, turns them back on.
    juce::Desktop::getInstance().addGlobalMouseListener(this);
    juce::Desktop::getInstance().addFocusChangeListener(this);
}

void NorthlightLookAndFeel::refreshColours()
{
    const auto& c = theme_.colours;
    setColour(juce::ResizableWindow::backgroundColourId, c.canvas);
    setColour(juce::DocumentWindow::textColourId, c.textPrimary);

    setColour(juce::Label::textColourId, c.textPrimary);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::TextButton::buttonColourId, c.surface2);
    setColour(juce::TextButton::buttonOnColourId, c.accentSubtle);
    setColour(juce::TextButton::textColourOffId, c.textPrimary);
    setColour(juce::TextButton::textColourOnId, c.accent);

    setColour(juce::ToggleButton::textColourId, c.textPrimary);
    setColour(juce::ToggleButton::tickColourId, c.accent);
    setColour(juce::ToggleButton::tickDisabledColourId, c.textDisabled);

    setColour(juce::ComboBox::backgroundColourId, c.surface2);
    setColour(juce::ComboBox::textColourId, c.textPrimary);
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::arrowColourId, c.textSecondary);
    setColour(juce::ComboBox::buttonColourId, c.surface2);
    setColour(juce::ComboBox::focusedOutlineColourId, c.accent);

    setColour(juce::PopupMenu::backgroundColourId, c.surface1);
    setColour(juce::PopupMenu::textColourId, c.textPrimary);
    setColour(juce::PopupMenu::headerTextColourId, c.textTertiary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, c.surface3);
    setColour(juce::PopupMenu::highlightedTextColourId, c.textPrimary);

    setColour(juce::ListBox::backgroundColourId, c.surface0);
    setColour(juce::ListBox::outlineColourId, c.lineSubtle);
    setColour(juce::ListBox::textColourId, c.textPrimary);
    setColour(juce::TableHeaderComponent::backgroundColourId, c.surface0);
    setColour(juce::TableHeaderComponent::textColourId, c.textTertiary);
    setColour(juce::TableHeaderComponent::outlineColourId, c.lineSubtle);
    setColour(juce::TableHeaderComponent::highlightColourId, c.surface1);

    setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, c.surface3);
    setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour(juce::Slider::backgroundColourId, c.surface2);
    setColour(juce::Slider::trackColourId, c.accent);
    setColour(juce::Slider::thumbColourId, c.textPrimary);
    setColour(juce::Slider::rotarySliderFillColourId, c.accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, c.surface2);
    setColour(juce::Slider::textBoxTextColourId, c.textPrimary);
    setColour(juce::Slider::textBoxBackgroundColourId, c.surface2);
    setColour(juce::Slider::textBoxOutlineColourId, c.lineStrong);
    setColour(juce::Slider::textBoxHighlightColourId, c.selectionFill);

    setColour(juce::TextEditor::backgroundColourId, c.surface2);
    setColour(juce::TextEditor::textColourId, c.textPrimary);
    setColour(juce::TextEditor::outlineColourId, c.lineStrong);
    setColour(juce::TextEditor::focusedOutlineColourId, c.accent);
    setColour(juce::TextEditor::highlightColourId, c.selectionFill);
    setColour(juce::CaretComponent::caretColourId, c.accent);

    setColour(juce::TooltipWindow::backgroundColourId, c.surface3);
    setColour(juce::TooltipWindow::textColourId, c.textPrimary);
    setColour(juce::TooltipWindow::outlineColourId, c.lineStrong);

    setColour(juce::AlertWindow::backgroundColourId, c.surface0);
    setColour(juce::AlertWindow::textColourId, c.textPrimary);
    setColour(juce::AlertWindow::outlineColourId, c.lineSubtle);

    setColour(juce::ProgressBar::backgroundColourId, c.surface2);
    setColour(juce::ProgressBar::foregroundColourId, c.accent);

    setColour(juce::TabbedButtonBar::tabOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedButtonBar::frontOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TabbedButtonBar::tabTextColourId, c.textSecondary);
    setColour(juce::TabbedButtonBar::frontTextColourId, c.textPrimary);
    setColour(juce::TabbedComponent::backgroundColourId, c.surface0);
    setColour(juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
}

void NorthlightLookAndFeel::remapColours(juce::Component& root,
                                         const Theme::Colours& before,
                                         const Theme::Colours& after)
{
    // The tokens Preferences can change (the accent and contrast).
    const std::pair<juce::Colour, juce::Colour> changed[]{{before.accent, after.accent},
                                                          {before.textSecondary, after.textSecondary},
                                                          {before.textTertiary, after.textTertiary},
                                                          {before.textDisabled, after.textDisabled},
                                                          {before.lineSubtle, after.lineSubtle},
                                                          {before.lineStrong, after.lineStrong}};
    const auto remap = [&](juce::Component& c, int id)
    {
        if (!c.isColourSpecified(id))
            return;
        const auto colour = c.findColour(id);
        for (const auto& [was, now] : changed)
            if (colour == was && was != now)
            {
                c.setColour(id, now);
                return;
            }
    };
    remap(root, juce::Label::textColourId);
    remap(root, juce::TextEditor::textColourId);
    remap(root, juce::TextEditor::outlineColourId);
    if (auto* field = dynamic_cast<juce::TextEditor*>(&root); field != nullptr)
        if (const auto placeholder = field->getTextToShowWhenEmpty(); placeholder.isNotEmpty())
            field->setTextToShowWhenEmpty(placeholder,
                                          after.textTertiary); // every placeholder is textTertiary
    for (auto* child : root.getChildren())
        remapColours(*child, before, after);
}

NorthlightLookAndFeel::~NorthlightLookAndFeel()
{
    juce::Desktop::getInstance().removeFocusChangeListener(this);
    juce::Desktop::getInstance().removeGlobalMouseListener(this);
}

void NorthlightLookAndFeel::mouseDown(const juce::MouseEvent&)
{
    setKeyboardFocusVisible(false);
}

void NorthlightLookAndFeel::globalFocusChanged(juce::Component* focused)
{
    using Key = juce::KeyPress;
    const bool byKeyboard = Key::isKeyCurrentlyDown(Key::tabKey) || Key::isKeyCurrentlyDown(Key::leftKey) ||
                            Key::isKeyCurrentlyDown(Key::rightKey) || Key::isKeyCurrentlyDown(Key::upKey) ||
                            Key::isKeyCurrentlyDown(Key::downKey);
    if (byKeyboard)
        setKeyboardFocusVisible(true);
    // Move the ring: not every custom component repaints on a focus change.
    if (keyboardFocusVisible() && lastFocused_.getComponent() != focused)
    {
        if (lastFocused_ != nullptr)
            lastFocused_->repaint();
        if (focused != nullptr)
            focused->repaint();
    }
    lastFocused_ = focused;
}

void NorthlightLookAndFeel::drawFocusRing(juce::Graphics& g,
                                          const juce::Component& component,
                                          juce::Rectangle<float> bounds,
                                          float radius) const
{
    if (!keyboardFocusVisible() || !component.hasKeyboardFocus(false))
        return;
    const auto ring = static_cast<float>(theme_.sizes.focusRing);
    g.setColour(theme_.colours.lineFocus);
    g.drawRoundedRectangle(bounds.expanded(ring * 0.5f + 1.0f), radius + ring, ring);
}

void NorthlightLookAndFeel::drawRegionFocusRing(juce::Graphics& g,
                                                const juce::Component& region,
                                                juce::Rectangle<int> area,
                                                bool orChild) const
{
    if (!keyboardFocusVisible() || !region.hasKeyboardFocus(orChild))
        return;
    const auto ring = static_cast<float>(theme_.sizes.focusRing);
    g.setColour(theme_.colours.lineFocus);
    g.drawRect(area.toFloat().reduced(ring * 0.5f), ring);
}

void NorthlightLookAndFeel::drawControlFace(juce::Graphics& g,
                                            juce::Rectangle<float> bounds,
                                            float radius,
                                            juce::Colour flat,
                                            bool pressed,
                                            bool hover) const
{
    const auto& m = theme_.material;
    if (!m.enabled)
    {
        g.setColour(flat);
        g.fillRoundedRectangle(bounds, radius);
        return;
    }
    // The drop shadow below the face (not when pressed in).
    if (!pressed && !m.controlShadow.empty())
    {
        g.setColour(m.controlShadow.colour);
        g.fillRoundedRectangle(bounds.translated(m.controlShadow.x, m.controlShadow.y), radius);
    }
    auto top = pressed ? m.controlBottom : m.controlTop;
    auto bottom = pressed ? m.controlTop : m.controlBottom;
    if (hover && !pressed)
    {
        top = top.brighter(0.08f);
        bottom = bottom.brighter(0.08f);
    }
    g.setGradientFill(juce::ColourGradient::vertical(top, bounds.getY(), bottom, bounds.getBottom()));
    g.fillRoundedRectangle(bounds, radius);
    if (pressed)
        drawInset(g, bounds, radius, m.pressedInset);
    else
    {
        g.setColour(m.controlHighlight);
        g.fillRect(bounds.getX() + radius * 0.5f, bounds.getY(), bounds.getWidth() - radius, 1.0f);
    }
}

void NorthlightLookAndFeel::drawInset(juce::Graphics& g,
                                      juce::Rectangle<float> bounds,
                                      float radius,
                                      const Theme::Shadow& inset) const
{
    if (inset.empty())
        return;
    // A short gradient from the top edge, the strength of the shadow's blur.
    const float depth = std::max(1.0f, inset.blur + inset.y);
    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle(bounds, radius);
    g.reduceClipRegion(clip);
    g.setGradientFill(juce::ColourGradient::vertical(
        inset.colour, bounds.getY(), inset.colour.withAlpha(0.0f), bounds.getY() + depth));
    g.fillRect(bounds.withHeight(depth));
    g.restoreState();
}

void NorthlightLookAndFeel::drawGlow(juce::Graphics& g,
                                     juce::Rectangle<float> bounds,
                                     float radius,
                                     const Theme::Shadow& glow) const
{
    if (glow.empty() || theme_.motion.reduceMotion || theme_.highContrast) // both drop the glows
        return;
    const int pad = static_cast<int>(std::ceil(glow.blur)) + 2;
    const auto key = GlowKey{static_cast<int>(std::ceil(bounds.getWidth())),
                             static_cast<int>(std::ceil(bounds.getHeight())),
                             glow.colour.getARGB(),
                             radius,
                             glow.blur};
    auto it = glows_.find(key);
    if (it == glows_.end())
    {
        // Rendered once per size and colour: the shape blurred by the glow radius.
        juce::Image image(juce::Image::ARGB, key.width + 2 * pad, key.height + 2 * pad, true);
        {
            juce::Graphics ig(image);
            juce::Path shape;
            shape.addRoundedRectangle(juce::Rectangle<float>(static_cast<float>(pad),
                                                             static_cast<float>(pad),
                                                             static_cast<float>(key.width),
                                                             static_cast<float>(key.height)),
                                      radius);
            juce::DropShadow(glow.colour, static_cast<int>(glow.blur), {0, 0}).drawForPath(ig, shape);
        }
        it = glows_.emplace(key, std::move(image)).first;
    }
    g.drawImageAt(it->second,
                  static_cast<int>(bounds.getX()) - pad + static_cast<int>(glow.x),
                  static_cast<int>(bounds.getY()) - pad + static_cast<int>(glow.y));
}

void NorthlightLookAndFeel::drawChevron(juce::Graphics& g,
                                        juce::Rectangle<float> area,
                                        juce::Colour colour,
                                        bool down) const
{
    const float size = static_cast<float>(theme_.sizes.iconSmall) * 0.5f;
    juce::Path chevron;
    const float cx = area.getCentreX();
    const float cy = area.getCentreY();
    if (down)
    {
        chevron.startNewSubPath(cx - size * 0.5f, cy - size * 0.25f);
        chevron.lineTo(cx, cy + size * 0.25f);
        chevron.lineTo(cx + size * 0.5f, cy - size * 0.25f);
    }
    else
    {
        chevron.startNewSubPath(cx - size * 0.25f, cy - size * 0.5f);
        chevron.lineTo(cx + size * 0.25f, cy);
        chevron.lineTo(cx - size * 0.25f, cy + size * 0.5f);
    }
    g.setColour(colour);
    g.strokePath(chevron,
                 juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void NorthlightLookAndFeel::drawCheck(juce::Graphics& g,
                                      juce::Rectangle<float> box,
                                      juce::Colour colour) const
{
    juce::Path tick;
    tick.startNewSubPath(box.getX() + box.getWidth() * 0.25f, box.getCentreY());
    tick.lineTo(box.getX() + box.getWidth() * 0.43f, box.getBottom() - box.getHeight() * 0.3f);
    tick.lineTo(box.getRight() - box.getWidth() * 0.22f, box.getY() + box.getHeight() * 0.3f);
    g.setColour(colour);
    g.strokePath(tick,
                 juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ----- buttons ------------------------------------------------------------------------

void NorthlightLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                                 juce::Button& button,
                                                 const juce::Colour& /*backgroundColour*/,
                                                 bool isMouseOver,
                                                 bool isButtonDown)
{
    const auto& c = theme_.colours;
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = theme_.radius.sm;
    const bool on = button.getToggleState();
    const auto variant = variantOf(button);
    const auto semantic = button.getProperties().getWithDefault(propertySemantic, "").toString();
    const bool hardOn = static_cast<bool>(button.getProperties().getWithDefault(propertyHardOn, false));

    if (!button.isEnabled())
    {
        if (variant != "ghost")
        {
            g.setColour(c.surface1);
            g.fillRoundedRectangle(bounds, radius);
        }
        return;
    }

    const auto& m = theme_.material;
    juce::Colour fill;
    juce::Colour outline;
    const Theme::Shadow* glow = nullptr;
    if (on)
    {
        // Toggle on: a 20% fill of the colour with a 1 px inset outline and the
        // colour's glow; "hard on" (an armed track) fills solidly.
        juce::Colour colour = c.accent;
        glow = &m.glowAccent;
        if (semantic == "arm" || semantic == "record")
        {
            colour = c.arm;
            glow = &m.glowRecord;
        }
        else if (semantic == "solo")
        {
            colour = c.solo;
            glow = &m.glowSolo;
        }
        else if (semantic == "mute")
            colour = c.mute;
        else if (semantic == "monitor")
        {
            colour = c.monitor;
            glow = &m.glowMonitor;
        }
        else if (semantic == "monitorAuto")
        {
            // Auto monitoring that is not passing the input right now: waiting.
            colour = c.warning;
            glow = nullptr;
        }
        else if (semantic == "phase")
        {
            colour = c.textSecondary;
            glow = nullptr;
        }
        else if (semantic.startsWith("automation"))
        {
            // The A toggle: its automation mode's colour, no glow.
            colour = automationModeColour(semantic);
            glow = nullptr;
        }
        if (glow != nullptr)
            drawGlow(g, bounds, radius, *glow);
        if (hardOn)
        {
            const auto base = isButtonDown  ? colour.darker(0.15f)
                              : isMouseOver ? colour.brighter(0.1f)
                                            : colour;
            if (m.enabled)
            {
                g.setGradientFill(juce::ColourGradient::vertical(
                    base.brighter(0.15f), bounds.getY(), base.darker(0.1f), bounds.getBottom()));
                g.fillRoundedRectangle(bounds, radius);
                g.setColour(m.capHighlight);
                g.fillRect(bounds.getX() + radius * 0.5f, bounds.getY(), bounds.getWidth() - radius, 1.0f);
            }
            else
            {
                g.setColour(base);
                g.fillRoundedRectangle(bounds, radius);
            }
        }
        else
        {
            drawControlFace(g, bounds, radius, c.surface2, isButtonDown, isMouseOver);
            fill = semantic.isEmpty() ? c.accentSubtle : colour.withAlpha(0.2f);
            if (isMouseOver || isButtonDown)
                fill = fill.withMultipliedAlpha(1.4f);
            g.setColour(fill);
            g.fillRoundedRectangle(bounds, radius);
            outline = colour;
        }
    }
    else if (variant == "primary")
    {
        // Moonlight: a cream key with peat text, the one bright thing in a view.
        const auto base = isButtonDown ? c.primaryPressed : isMouseOver ? c.primaryHover : c.primary;
        if (m.enabled)
        {
            g.setGradientFill(
                juce::ColourGradient::vertical(base, bounds.getY(), base.darker(0.06f), bounds.getBottom()));
            g.fillRoundedRectangle(bounds, radius);
            if (!isButtonDown)
            {
                g.setColour(m.controlShadow.colour);
                g.fillRect(
                    bounds.getX() + radius * 0.5f, bounds.getBottom(), bounds.getWidth() - radius, 1.0f);
            }
        }
        else
        {
            g.setColour(base);
            g.fillRoundedRectangle(bounds, radius);
        }
    }
    else if (variant == "ghost")
    {
        if (isMouseOver || isButtonDown)
        {
            g.setColour(isButtonDown ? c.surfacePressed : c.surface2);
            g.fillRoundedRectangle(bounds, radius);
        }
    }
    else if (variant == "danger")
    {
        if (isMouseOver || isButtonDown)
        {
            g.setColour(isButtonDown ? c.danger.darker(0.2f) : c.danger);
            g.fillRoundedRectangle(bounds, radius);
        }
    }
    else
        drawControlFace(g,
                        bounds,
                        radius,
                        isButtonDown  ? c.surfacePressed
                        : isMouseOver ? c.surface3
                                      : c.surface2,
                        isButtonDown,
                        isMouseOver);

    if (!outline.isTransparent())
    {
        g.setColour(outline);
        g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
    }
    drawFocusRing(g, button, bounds, radius);
}

juce::Colour NorthlightLookAndFeel::automationModeColour(const juce::String& semantic) const
{
    const auto& c = theme_.colours;
    if (semantic == "automationRead")
        return c.success;
    if (semantic == "automationTouch" || semantic == "automationLatch")
        return c.warning;
    if (semantic == "automationWrite")
        return c.record;
    return c.textTertiary; // off
}

void NorthlightLookAndFeel::drawButtonText(juce::Graphics& g,
                                           juce::TextButton& button,
                                           bool isMouseOver,
                                           bool isButtonDown)
{
    const auto& c = theme_.colours;
    const auto variant = variantOf(button);
    const auto semantic = button.getProperties().getWithDefault(propertySemantic, "").toString();
    const bool hardOn = static_cast<bool>(button.getProperties().getWithDefault(propertyHardOn, false));
    const bool on = button.getToggleState();
    juce::Colour text = c.textPrimary;
    if (!button.isEnabled())
        text = c.textDisabled;
    else if (on)
    {
        juce::Colour colour = c.accent;
        if (semantic == "arm" || semantic == "record")
            colour = c.arm;
        else if (semantic == "solo")
            colour = c.solo;
        else if (semantic == "mute")
            colour = c.mute;
        else if (semantic == "monitor")
            colour = c.monitor;
        else if (semantic == "monitorAuto")
            colour = c.warning;
        else if (semantic == "phase")
            colour = c.textSecondary;
        else if (semantic.startsWith("automation"))
            colour = automationModeColour(semantic);
        text = hardOn ? c.textOnColour : colour;
    }
    else if (semantic.startsWith("automation"))
        text = automationModeColour(semantic); // the A toggle shows its mode when closed too
    else if (variant == "primary")
        text = c.textOnPrimary;
    else if (variant == "ghost")
        text = isMouseOver || isButtonDown ? c.textPrimary : c.textSecondary;
    else if (variant == "danger")
        text = isMouseOver || isButtonDown ? c.textOnColour : c.danger;

    g.setColour(text);
    g.setFont(getTextButtonFont(button, button.getHeight()));
    auto area = button.getLocalBounds();
    const bool menu = static_cast<bool>(button.getProperties().getWithDefault(propertyMenu, false));
    if (menu)
    {
        const auto chevron = area.removeFromRight(theme_.sizes.icon + theme_.spacing.xs).toFloat();
        drawChevron(g, chevron, text.withMultipliedAlpha(0.8f), true);
    }
    const bool icon = static_cast<bool>(button.getProperties().getWithDefault(propertyIcon, false));
    const int pad = icon ? 0 : std::min(theme_.spacing.md, area.getWidth() / 6);
    // Centred by its letters, not its line box, which leaves a word low (OpticalCentre.h).
    juce::Graphics::ScopedSaveState state(g);
    g.addTransform(
        juce::AffineTransform::translation(0.0f, opticalShift(g.getCurrentFont(), button.getButtonText())));
    g.drawFittedText(button.getButtonText(), area.reduced(pad, 0), juce::Justification::centred, 1, 1.0f);
}

juce::Font NorthlightLookAndFeel::getTextButtonFont(juce::TextButton&, int /*buttonHeight*/)
{
    return theme_.label();
}

void NorthlightLookAndFeel::drawToggleButton(juce::Graphics& g,
                                             juce::ToggleButton& button,
                                             bool isMouseOver,
                                             bool isButtonDown)
{
    // A checkbox: 16 px box, label to the right, the whole row is the hit target.
    const float boxSize = static_cast<float>(theme_.sizes.icon);
    const auto bounds = button.getLocalBounds().toFloat();
    const float y = bounds.getCentreY() - boxSize * 0.5f;
    drawTickBox(g,
                button,
                0.0f,
                y,
                boxSize,
                boxSize,
                button.getToggleState(),
                button.isEnabled(),
                isMouseOver,
                isButtonDown);
    g.setColour(button.isEnabled() ? theme_.colours.textPrimary : theme_.colours.textDisabled);
    g.setFont(theme_.body());
    g.drawFittedText(button.getButtonText(),
                     button.getLocalBounds().withTrimmedLeft(static_cast<int>(boxSize) + theme_.spacing.sm),
                     juce::Justification::centredLeft,
                     1);
}

void NorthlightLookAndFeel::drawTickBox(juce::Graphics& g,
                                        juce::Component& component,
                                        float x,
                                        float y,
                                        float w,
                                        float h,
                                        bool ticked,
                                        bool isEnabled,
                                        bool isMouseOver,
                                        bool /*isButtonDown*/)
{
    const auto& c = theme_.colours;
    const auto box = juce::Rectangle<float>(x, y, w, h).reduced(0.5f);
    juce::Colour fill = ticked ? c.accent : c.surface2;
    if (!isEnabled)
        fill = ticked ? c.accent.withAlpha(0.4f) : c.surface1;
    else if (isMouseOver)
        fill = ticked ? c.accentHover : c.surface3;
    g.setColour(fill);
    g.fillRoundedRectangle(box, theme_.radius.sm);
    if (!ticked)
    {
        g.setColour(c.lineStrong);
        g.drawRoundedRectangle(box, theme_.radius.sm, 1.0f);
    }
    if (ticked)
        drawCheck(g, box, isEnabled ? c.textOnAccent : c.textOnAccent.withAlpha(0.6f));
    drawFocusRing(g, component, box, theme_.radius.sm);
}

// ----- combo boxes and menus --------------------------------------------------------------

void NorthlightLookAndFeel::drawComboBox(juce::Graphics& g,
                                         int width,
                                         int height,
                                         bool isButtonDown,
                                         int /*buttonX*/,
                                         int /*buttonY*/,
                                         int /*buttonW*/,
                                         int /*buttonH*/,
                                         juce::ComboBox& box)
{
    const auto& c = theme_.colours;
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
    if (variantOf(box) == "readout")
    {
        // Part of a readout well (the transport's signature): no face of its own,
        // a faint lift on hover. The text is drawn here, on the well's shared
        // baseline, with the chevron right after it on the figures' centre line
        // (components/Readout.h); the label child is kept empty.
        if (box.isMouseOver(true) || box.isPopupActive())
        {
            g.setColour(c.textPrimary.withAlpha(0.06f));
            g.fillRoundedRectangle(bounds, theme_.radius.sm);
        }
        const auto font = theme_.timeSmall();
        const auto text = box.getText();
        const int baseline = readout::baseline(theme_, height);
        const int x = readout::inset(theme_);
        g.setFont(font);
        g.setColour(box.isEnabled() ? box.findColour(juce::ComboBox::textColourId) : c.textDisabled);
        g.drawSingleLineText(text, x, baseline);
        const float chevronX =
            static_cast<float>(x + readout::textWidth(font, text) + readout::chevronGap(theme_));
        const float middle = static_cast<float>(baseline) - readout::figureMiddle(font);
        const float w = static_cast<float>(readout::chevronWidth(theme_));
        drawChevron(g, {chevronX, middle - w * 0.5f, w, w}, c.textTertiary, true);
        drawFocusRing(g, box, bounds, theme_.radius.sm);
        return;
    }
    if (!box.isEnabled())
    {
        g.setColour(c.surface1);
        g.fillRoundedRectangle(bounds, theme_.radius.sm);
    }
    else
        drawControlFace(g,
                        bounds,
                        theme_.radius.sm,
                        isButtonDown            ? c.surfacePressed
                        : box.isMouseOver(true) ? c.surface3
                                                : c.surface2,
                        isButtonDown,
                        box.isMouseOver(true));
    if (box.isPopupActive())
    {
        g.setColour(c.accent);
        g.drawRoundedRectangle(bounds, theme_.radius.sm, 1.0f);
    }
    const auto arrow = bounds.removeFromRight(static_cast<float>(theme_.sizes.icon + theme_.spacing.sm));
    drawChevron(g, arrow, box.isEnabled() ? c.textSecondary : c.textDisabled, true);
    drawFocusRing(g, box, bounds.withRight(arrow.getRight()), theme_.radius.sm);
}

juce::Font NorthlightLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    if (variantOf(box) == "readout")
        return theme_.timeSmall();
    return theme_.body();
}

void NorthlightLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    if (variantOf(box) == "readout")
    {
        // drawComboBox draws a readout's text itself, on the shared baseline.
        label.setBounds({});
        return;
    }
    const int chevron = theme_.sizes.icon + theme_.spacing.sm;
    label.setBounds(theme_.spacing.sm, 1, box.getWidth() - chevron - theme_.spacing.sm, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    // Never squashed sideways: a text too long for its box ends in an ellipsis.
    label.setMinimumHorizontalScale(1.0f);
    // The box's own padding is the inset; the label adds none of its own.
    label.setBorderSize({1, 0, 1, 0});
}

void NorthlightLookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& g,
                                                               int width,
                                                               int height,
                                                               const juce::PopupMenu::Options& /*options*/)
{
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(theme_.colours.surface1);
    g.fillRoundedRectangle(bounds, theme_.radius.md);
    g.setColour(theme_.colours.lineStrong);
    g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.md, 1.0f);
}

void NorthlightLookAndFeel::preparePopupMenuWindow(juce::Component& window)
{
    // Called with the window placed and not yet shown: start it transparent and
    // 2 px short of its place, and ease both in (design-system.md 2.6).
    const int ms = theme_.motion.ms(theme_.motion.menuMs);
    if (ms <= 0)
        return;
    const auto place = window.getPosition();
    window.setAlpha(0.0f);
    window.setTopLeftPosition(place.translated(0, -2));
    motion::Tween::run(
        window,
        ms,
        [&window, place](float t)
        {
            window.setAlpha(t);
            window.setTopLeftPosition(place.translated(0, juce::roundToInt(-2.0f * (1.0f - t))));
        },
        {},
        true); // timed from when the window is on screen
}

int NorthlightLookAndFeel::getPopupMenuBorderSize()
{
    return theme_.spacing.xs;
}

juce::Font NorthlightLookAndFeel::getPopupMenuFont()
{
    return theme_.body();
}

void NorthlightLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text,
                                                      bool isSeparator,
                                                      int /*standardMenuItemHeight*/,
                                                      int& idealWidth,
                                                      int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = theme_.spacing.xxl;
        idealHeight = theme_.spacing.sm + 1;
        return;
    }
    const auto font = theme_.body();
    idealHeight = theme_.sizes.control;
    // A hint after a tab (drawPopupMenuItem) needs its own room after a gap.
    const int tab = text.indexOfChar('\t');
    const auto name = tab >= 0 ? text.substring(0, tab) : text;
    const int hint =
        tab >= 0 ? juce::GlyphArrangement::getStringWidthInt(theme_.small(), text.substring(tab + 1)) +
                       theme_.spacing.lg
                 : 0;
    idealWidth = juce::GlyphArrangement::getStringWidthInt(font, name) + hint + theme_.spacing.sm * 2 +
                 theme_.sizes.iconLarge + theme_.spacing.xl;
}

void NorthlightLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
                                              const juce::Rectangle<int>& area,
                                              bool isSeparator,
                                              bool isActive,
                                              bool isHighlighted,
                                              bool isTicked,
                                              bool hasSubMenu,
                                              const juce::String& text,
                                              const juce::String& shortcutKeyText,
                                              const juce::Drawable* icon,
                                              const juce::Colour* textColour)
{
    const auto& c = theme_.colours;
    if (isSeparator)
    {
        g.setColour(c.lineSubtle);
        g.fillRect(area.reduced(theme_.spacing.sm, 0).withHeight(1).withY(area.getCentreY()));
        return;
    }
    auto row = area.reduced(theme_.spacing.xxs, 0);
    if (isHighlighted && isActive)
    {
        g.setColour(c.surface3);
        g.fillRoundedRectangle(row.toFloat(), theme_.radius.sm);
    }
    const juce::Colour colour = !isActive               ? c.textDisabled
                                : textColour != nullptr ? *textColour
                                                        : c.textPrimary;
    auto iconColumn = row.removeFromLeft(theme_.sizes.iconLarge + theme_.spacing.sm);
    if (isTicked)
        drawCheck(g,
                  iconColumn.toFloat().withSizeKeepingCentre(static_cast<float>(theme_.sizes.iconSmall),
                                                             static_cast<float>(theme_.sizes.iconSmall)),
                  c.accent);
    else if (icon != nullptr)
    {
        // At the house icon size (its 16 px grid keeps the padding), centred.
        const auto size = static_cast<float>(theme_.sizes.icon);
        icon->drawWithin(g,
                         iconColumn.toFloat().withSizeKeepingCentre(size, size),
                         juce::RectanglePlacement::centred,
                         1.0f);
    }
    if (hasSubMenu)
        drawChevron(g, row.removeFromRight(theme_.sizes.icon + theme_.spacing.xs).toFloat(), colour, false);
    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont(theme_.timeSmall().withPointHeight(theme_.fonts.size.small));
        g.setColour(isActive ? c.textTertiary : c.textDisabled);
        const auto hint = row.removeFromRight(
            juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), shortcutKeyText) +
            theme_.spacing.sm);
        g.drawText(
            shortcutKeyText, hint.withTrimmedRight(theme_.spacing.sm), juce::Justification::centredRight);
    }
    // Text after a tab is a hint about the item (a plugin's maker): right aligned
    // in `small` `textTertiary`, the name keeping the room it needs first.
    auto name = text;
    if (const int tab = text.indexOfChar('\t'); tab >= 0)
    {
        name = text.substring(0, tab);
        const auto hint = text.substring(tab + 1);
        g.setFont(theme_.body());
        const int nameW = juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), name);
        const int room = row.getWidth() - theme_.spacing.sm - nameW - theme_.spacing.lg;
        if (room > theme_.spacing.xl)
        {
            g.setFont(theme_.small());
            g.setColour(isActive ? c.textTertiary : c.textDisabled);
            g.drawText(hint,
                       row.removeFromRight(room).withTrimmedRight(theme_.spacing.sm),
                       juce::Justification::centredRight,
                       true);
        }
    }
    g.setColour(colour);
    g.setFont(theme_.body());
    g.drawFittedText(name, row.withTrimmedRight(theme_.spacing.sm), juce::Justification::centredLeft, 1);
}

void NorthlightLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g,
                                                       const juce::Rectangle<int>& area,
                                                       const juce::String& sectionName)
{
    g.setColour(theme_.colours.textTertiary);
    g.setFont(theme_.captionCaps());
    g.drawFittedText(sectionName.toUpperCase(),
                     area.reduced(theme_.spacing.sm + theme_.sizes.iconLarge, 0),
                     juce::Justification::centredLeft,
                     1);
}

// ----- labels, fields ---------------------------------------------------------------------

juce::Font NorthlightLookAndFeel::getLabelFont(juce::Label& label)
{
    return label.getFont();
}

void NorthlightLookAndFeel::fillTextEditorBackground(juce::Graphics& g,
                                                     int width,
                                                     int height,
                                                     juce::TextEditor& editor)
{
    g.setColour(editor.isEnabled() ? theme_.colours.surface2 : theme_.colours.surface1);
    g.fillRoundedRectangle(juce::Rectangle<int>(0, 0, width, height).toFloat(), theme_.radius.sm);
}

void NorthlightLookAndFeel::drawTextEditorOutline(juce::Graphics& g,
                                                  int width,
                                                  int height,
                                                  juce::TextEditor& editor)
{
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
    const bool invalid = static_cast<bool>(editor.getProperties().getWithDefault("invalid", false));
    g.setColour(invalid                         ? theme_.colours.danger
                : editor.hasKeyboardFocus(true) ? theme_.colours.accent
                                                : theme_.colours.lineStrong);
    g.drawRoundedRectangle(bounds, theme_.radius.sm, 1.0f);
}

// ----- sliders ----------------------------------------------------------------------------

int NorthlightLookAndFeel::getSliderThumbRadius(juce::Slider& slider)
{
    return slider.isMouseOverOrDragging() ? 8 : 7;
}

juce::Label* NorthlightLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(theme_.value());
    label->setJustificationType(juce::Justification::centredRight);
    return label;
}

void NorthlightLookAndFeel::drawLinearSlider(juce::Graphics& g,
                                             int x,
                                             int y,
                                             int width,
                                             int height,
                                             float sliderPos,
                                             float /*minSliderPos*/,
                                             float /*maxSliderPos*/,
                                             juce::Slider::SliderStyle style,
                                             juce::Slider& slider)
{
    const auto& c = theme_.colours;
    const bool horizontal = style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar;
    const float trackThickness = static_cast<float>(theme_.spacing.xs);
    const float thumbRadius = static_cast<float>(getSliderThumbRadius(slider));

    juce::Rectangle<float> track;
    if (horizontal)
        track = juce::Rectangle<float>(static_cast<float>(x) + thumbRadius,
                                       static_cast<float>(y) + static_cast<float>(height) * 0.5f -
                                           trackThickness * 0.5f,
                                       static_cast<float>(width) - thumbRadius * 2.0f,
                                       trackThickness);
    else
        track = juce::Rectangle<float>(static_cast<float>(x) + static_cast<float>(width) * 0.5f -
                                           trackThickness * 0.5f,
                                       static_cast<float>(y) + thumbRadius,
                                       trackThickness,
                                       static_cast<float>(height) - thumbRadius * 2.0f);

    // The track is a groove cut into the panel (the fader slot's colour and
    // rim), so the thumb sits in something even where nothing is filled.
    const auto& m = theme_.material;
    const auto groove = track.expanded(0.5f);
    g.setColour(m.enabled ? m.groove : c.surface2);
    g.fillRoundedRectangle(groove, groove.getHeight() * 0.5f);
    g.setColour(c.lineStrong);
    g.drawRoundedRectangle(groove, std::min(groove.getWidth(), groove.getHeight()) * 0.5f, 1.0f);

    // Bipolar sliders fill from the centre (marked); the rest from the start.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    if (bipolar)
    {
        const float zero = static_cast<float>(slider.getPositionOfValue(0.0));
        g.setColour(c.textTertiary);
        if (horizontal)
            g.fillRect(zero - 0.5f, groove.getY() - 2.0f, 1.0f, groove.getHeight() + 4.0f);
        else
            g.fillRect(groove.getX() - 2.0f, zero - 0.5f, groove.getWidth() + 4.0f, 1.0f);
    }
    juce::Rectangle<float> filled = track;
    juce::Point<float> thumb;
    if (horizontal)
    {
        const float zero = bipolar ? static_cast<float>(slider.getPositionOfValue(0.0)) : track.getX();
        filled.setLeft(std::min(zero, sliderPos));
        filled.setRight(std::max(zero, sliderPos));
        thumb = {sliderPos, track.getCentreY()};
    }
    else
    {
        const float zero = bipolar ? static_cast<float>(slider.getPositionOfValue(0.0)) : track.getBottom();
        filled.setTop(std::min(zero, sliderPos));
        filled.setBottom(std::max(zero, sliderPos));
        thumb = {track.getCentreX(), sliderPos};
    }
    g.setColour(slider.isEnabled() ? c.accent : c.textDisabled);
    g.fillRoundedRectangle(filled, trackThickness * 0.5f);

    const auto thumbBounds = juce::Rectangle<float>(thumbRadius * 2.0f, thumbRadius * 2.0f).withCentre(thumb);
    g.setColour(slider.isEnabled() ? c.textPrimary : c.textDisabled);
    g.fillEllipse(thumbBounds);
    g.setColour(c.canvas);
    g.drawEllipse(thumbBounds, 1.0f);
    drawFocusRing(g, slider, thumbBounds, thumbRadius);
}

// ----- scrollbars -------------------------------------------------------------------------

int NorthlightLookAndFeel::getDefaultScrollbarWidth()
{
    return theme_.sizes.scrollbar;
}

void NorthlightLookAndFeel::drawScrollbar(juce::Graphics& g,
                                          juce::ScrollBar& /*scrollbar*/,
                                          int x,
                                          int y,
                                          int width,
                                          int height,
                                          bool isScrollbarVertical,
                                          int thumbStartPosition,
                                          int thumbSize,
                                          bool isMouseOver,
                                          bool isMouseDown)
{
    if (thumbSize <= 0)
        return;
    const int inset = theme_.spacing.xxs;
    juce::Rectangle<int> thumb;
    if (isScrollbarVertical)
        thumb = {x + inset, thumbStartPosition, width - 2 * inset, thumbSize};
    else
        thumb = {thumbStartPosition, y + inset, thumbSize, height - 2 * inset};
    g.setColour((isMouseOver || isMouseDown) ? theme_.colours.textTertiary : theme_.colours.surface3);
    g.fillRoundedRectangle(thumb.toFloat(),
                           static_cast<float>(std::min(thumb.getWidth(), thumb.getHeight())) * 0.5f);
}

// ----- tabs ---------------------------------------------------------------------------------

int NorthlightLookAndFeel::getTabButtonBestWidth(juce::TabBarButton& button, int /*tabDepth*/)
{
    return juce::GlyphArrangement::getStringWidthInt(theme_.label(), button.getButtonText()) +
           2 * theme_.spacing.md;
}

int NorthlightLookAndFeel::getTabButtonOverlap(int /*tabDepth*/)
{
    return 0;
}

void NorthlightLookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) {}

void NorthlightLookAndFeel::drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) {}

void NorthlightLookAndFeel::drawTabButton(juce::TabBarButton& button,
                                          juce::Graphics& g,
                                          bool isMouseOver,
                                          bool /*isMouseDown*/)
{
    const auto& c = theme_.colours;
    const auto area = button.getActiveArea();
    const bool front = button.isFrontTab();
    g.setColour(front ? c.textPrimary : isMouseOver ? c.textPrimary : c.textSecondary);
    g.setFont(theme_.label());
    g.drawFittedText(
        button.getButtonText(), area.reduced(theme_.spacing.sm, 0), juce::Justification::centred, 1);
    if (front)
    {
        g.setColour(c.accent);
        g.fillRect(area.withTrimmedTop(area.getHeight() - theme_.spacing.xxs).reduced(theme_.spacing.sm, 0));
    }
}

// ----- tooltips, progress, tables ---------------------------------------------------------

NorthlightLookAndFeel::TooltipParts NorthlightLookAndFeel::splitShortcut(const juce::String& tip)
{
    const int at = tip.lastIndexOf("  ");
    if (at <= 0)
        return {tip, {}};
    const auto shortcut = tip.substring(at + 2).trim();
    if (shortcut.isEmpty() || shortcut.containsChar(' '))
        return {tip, {}};
    static const juce::StringArray modifiers{"Ctrl", "Shift", "Alt", "Cmd"};
    static const juce::StringArray named{"Space",
                                         "Enter",
                                         "Esc",
                                         "Tab",
                                         "Delete",
                                         "Backspace",
                                         "Home",
                                         "End",
                                         "Up",
                                         "Down",
                                         "Left",
                                         "Right",
                                         "PgUp",
                                         "PgDn"};
    auto keys = juce::StringArray::fromTokens(shortcut, "+", "");
    for (int i = 0; i < keys.size(); ++i)
    {
        const auto& k = keys[i];
        const bool last = i == keys.size() - 1;
        const bool isKey = (k.length() == 1 && !juce::CharacterFunctions::isWhitespace(k[0])) ||
                           named.contains(k) ||
                           (k.length() >= 2 && k.length() <= 3 && k[0] == 'F' &&
                            k.substring(1).containsOnly("0123456789")) ||
                           (k.length() == 4 && k.startsWith("Num")) || k == "Insert";
        if (last ? !isKey : !modifiers.contains(k))
            return {tip, {}};
    }
    return {tip.substring(0, at).trimEnd(), keys};
}

namespace
{
// The key caps' measures (shared by the bounds and the drawing).
constexpr float capPad = 5.0f, capGap = 3.0f, capToText = 10.0f;
float capWidth(const juce::Font& font, const juce::String& key, float height)
{
    return std::max(height,
                    static_cast<float>(juce::GlyphArrangement::getStringWidthInt(font, key)) + 2.0f * capPad);
}
float capsWidth(const juce::Font& font, const juce::StringArray& keys, float height)
{
    float w = 0.0f;
    for (const auto& k : keys)
        w += capWidth(font, k, height) + capGap;
    return keys.isEmpty() ? 0.0f : w - capGap;
}
} // namespace

juce::Rectangle<int> NorthlightLookAndFeel::getTooltipBounds(const juce::String& tipText,
                                                             juce::Point<int> screenPos,
                                                             juce::Rectangle<int> parentArea)
{
    const auto font = theme_.small();
    const auto parts = splitShortcut(tipText);
    const float capH = theme_.caption().getHeight() + 4.0f;
    const float keysW =
        parts.keys.isEmpty() ? 0.0f : capToText + capsWidth(theme_.caption(), parts.keys, capH);
    juce::TextLayout layout;
    juce::AttributedString text(parts.text);
    text.setFont(font);
    layout.createLayout(text,
                        static_cast<float>(theme_.sizes.tooltipMaxWidth - 2 * theme_.spacing.sm) - keysW);
    // Rounded up, with a pixel to spare: drawTooltip lays the text out again in
    // the box, and a box a fraction narrower than the text wraps it once more
    // (and clips the last line).
    const int w = static_cast<int>(std::ceil(layout.getWidth() + keysW)) + 1 + 2 * theme_.spacing.sm;
    const int h =
        static_cast<int>(std::ceil(std::max(layout.getHeight(), parts.keys.isEmpty() ? 0.0f : capH))) +
        2 * theme_.spacing.xs;
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + theme_.spacing.sm)
                                                                      : screenPos.x + theme_.spacing.sm,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + theme_.spacing.sm)
                                                                      : screenPos.y + theme_.spacing.xl,
                                w,
                                h)
        .constrainedWithin(parentArea);
}

void NorthlightLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(theme_.colours.surface3);
    g.fillRoundedRectangle(bounds, theme_.radius.sm);
    g.setColour(theme_.colours.lineStrong);
    g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.sm, 1.0f);
    const auto parts = splitShortcut(text);
    const auto capFont = theme_.caption();
    const float capH = capFont.getHeight() + 4.0f;
    const float keysW = parts.keys.isEmpty() ? 0.0f : capsWidth(capFont, parts.keys, capH);
    auto content =
        bounds.reduced(static_cast<float>(theme_.spacing.sm), static_cast<float>(theme_.spacing.xs));
    juce::TextLayout layout;
    juce::AttributedString attributed(parts.text);
    attributed.setFont(theme_.small());
    attributed.setColour(theme_.colours.textPrimary);
    layout.createLayout(attributed, content.getWidth() - (keysW > 0.0f ? keysW + capToText : 0.0f));
    // A single line sits centred; the key caps sit on the first line, at the right.
    const float textTop = content.getY() + std::max(0.0f, (content.getHeight() - layout.getHeight()) * 0.5f) *
                                               (layout.getNumLines() <= 1 ? 1.0f : 0.0f);
    layout.draw(g, content.withY(textTop).withHeight(layout.getHeight()));
    if (parts.keys.isEmpty())
        return;
    const float firstLine =
        layout.getNumLines() > 0 ? layout.getLine(0).getLineBoundsY().getLength() : content.getHeight();
    const float capY = textTop + (firstLine - capH) * 0.5f;
    float x = content.getRight() - keysW;
    g.setFont(capFont);
    for (const auto& key : parts.keys)
    {
        const auto cap = juce::Rectangle<float>(x, capY, capWidth(capFont, key, capH), capH);
        g.setColour(theme_.colours.surface1);
        g.fillRoundedRectangle(cap, theme_.radius.sm);
        g.setColour(theme_.colours.lineStrong);
        g.drawRoundedRectangle(cap.reduced(0.5f), theme_.radius.sm, 1.0f);
        g.setColour(theme_.colours.textSecondary);
        g.drawText(key, cap, juce::Justification::centred, false);
        x = cap.getRight() + capGap;
    }
}

void NorthlightLookAndFeel::drawProgressBar(juce::Graphics& g,
                                            juce::ProgressBar& /*bar*/,
                                            int width,
                                            int height,
                                            double progress,
                                            const juce::String& textToShow)
{
    const auto& c = theme_.colours;
    const float barH = static_cast<float>(theme_.spacing.xs);
    auto area = juce::Rectangle<int>(0, 0, width, height).toFloat();
    if (textToShow.isNotEmpty())
    {
        g.setColour(c.textSecondary);
        g.setFont(theme_.small());
        g.drawText(
            textToShow, area.removeFromTop(area.getHeight() - barH - 2.0f), juce::Justification::centredLeft);
    }
    const auto track = area.withHeight(barH).withCentre({area.getCentreX(), area.getBottom() - barH * 0.5f});
    g.setColour(c.surface2);
    g.fillRoundedRectangle(track, barH * 0.5f);
    g.setColour(c.accent);
    if (progress >= 0.0 && progress <= 1.0)
        g.fillRoundedRectangle(track.withWidth(track.getWidth() * static_cast<float>(progress)), barH * 0.5f);
    else
    {
        // Indeterminate: a 30% segment sweeping once every 1.2 s.
        const double phase = std::fmod(juce::Time::getMillisecondCounterHiRes() / 1200.0, 1.0);
        const float segment = track.getWidth() * 0.3f;
        const float x = track.getX() + static_cast<float>(phase) * (track.getWidth() + segment) - segment;
        g.fillRoundedRectangle(
            track.withX(std::max(track.getX(), x)).withRight(std::min(track.getRight(), x + segment)),
            barH * 0.5f);
    }
}

void NorthlightLookAndFeel::drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.setColour(theme_.colours.surface0);
    g.fillRect(header.getLocalBounds());
    g.setColour(theme_.colours.lineSubtle);
    g.fillRect(header.getLocalBounds().removeFromBottom(1));
}

void NorthlightLookAndFeel::drawTableHeaderColumn(juce::Graphics& g,
                                                  juce::TableHeaderComponent& /*header*/,
                                                  const juce::String& columnName,
                                                  int /*columnId*/,
                                                  int width,
                                                  int height,
                                                  bool isMouseOver,
                                                  bool /*isMouseDown*/,
                                                  int columnFlags)
{
    auto area = juce::Rectangle<int>(0, 0, width, height);
    if (isMouseOver)
    {
        g.setColour(theme_.colours.surface1);
        g.fillRect(area);
    }
    if ((columnFlags &
         (juce::TableHeaderComponent::sortedForwards | juce::TableHeaderComponent::sortedBackwards)) != 0)
        drawChevron(g,
                    area.removeFromRight(theme_.sizes.icon).toFloat(),
                    theme_.colours.textSecondary,
                    (columnFlags & juce::TableHeaderComponent::sortedForwards) != 0);
    g.setColour(theme_.colours.textTertiary);
    g.setFont(theme_.captionCaps());
    g.drawFittedText(
        columnName.toUpperCase(), area.reduced(theme_.spacing.sm, 0), juce::Justification::centredLeft, 1);
}

} // namespace northlight::ui
