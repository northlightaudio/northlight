#include "ui/AuralLookAndFeel.h"

#include <atomic>
#include <cmath>

namespace aural::ui
{

namespace
{
std::atomic<bool> keyboardFocus{false};

juce::String variantOf(const juce::Component& component)
{
    return component.getProperties()
        .getWithDefault(AuralLookAndFeel::propertyVariant, "secondary")
        .toString();
}
} // namespace

const juce::Identifier AuralLookAndFeel::propertyVariant{"variant"};
const juce::Identifier AuralLookAndFeel::propertySemantic{"semantic"};
const juce::Identifier AuralLookAndFeel::propertyHardOn{"hardOn"};
const juce::Identifier AuralLookAndFeel::propertyMenu{"menu"};
const juce::Identifier AuralLookAndFeel::propertyIcon{"icon"};

void AuralLookAndFeel::setKeyboardFocusVisible(bool visible) noexcept
{
    keyboardFocus.store(visible, std::memory_order_relaxed);
}

bool AuralLookAndFeel::keyboardFocusVisible() noexcept
{
    return keyboardFocus.load(std::memory_order_relaxed);
}

AuralLookAndFeel::AuralLookAndFeel(const Theme& theme) : theme_(theme)
{
    const auto& c = theme_.colours;
    if (theme_.fonts.ui != nullptr)
        setDefaultSansSerifTypeface(theme_.fonts.ui);

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

    // Any click ends keyboard-driven focus rings; a Tab or arrow press (reported
    // by the window) turns them back on.
    juce::Desktop::getInstance().addGlobalMouseListener(this);
}

AuralLookAndFeel::~AuralLookAndFeel()
{
    juce::Desktop::getInstance().removeGlobalMouseListener(this);
}

void AuralLookAndFeel::mouseDown(const juce::MouseEvent&)
{
    setKeyboardFocusVisible(false);
}

void AuralLookAndFeel::drawFocusRing(juce::Graphics& g,
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

void AuralLookAndFeel::drawControlFace(juce::Graphics& g,
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

void AuralLookAndFeel::drawInset(juce::Graphics& g,
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

void AuralLookAndFeel::drawGlow(juce::Graphics& g,
                                juce::Rectangle<float> bounds,
                                float radius,
                                const Theme::Shadow& glow) const
{
    if (glow.empty() || theme_.motion.reduceMotion)
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

void AuralLookAndFeel::drawChevron(juce::Graphics& g,
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

void AuralLookAndFeel::drawCheck(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour) const
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

void AuralLookAndFeel::drawButtonBackground(juce::Graphics& g,
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
        const auto base = isButtonDown ? c.accentPressed : isMouseOver ? c.accentHover : c.accent;
        if (m.enabled)
        {
            g.setGradientFill(juce::ColourGradient::vertical(
                base.brighter(0.1f), bounds.getY(), base.darker(0.1f), bounds.getBottom()));
            g.fillRoundedRectangle(bounds, radius);
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

void AuralLookAndFeel::drawButtonText(juce::Graphics& g,
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
        text = hardOn ? c.textOnColour : colour;
    }
    else if (variant == "primary")
        text = c.textOnAccent;
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
    g.drawFittedText(button.getButtonText(), area.reduced(pad, 0), juce::Justification::centred, 1, 1.0f);
}

juce::Font AuralLookAndFeel::getTextButtonFont(juce::TextButton&, int /*buttonHeight*/)
{
    return theme_.label();
}

void AuralLookAndFeel::drawToggleButton(juce::Graphics& g,
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

void AuralLookAndFeel::drawTickBox(juce::Graphics& g,
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

void AuralLookAndFeel::drawComboBox(juce::Graphics& g,
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

juce::Font AuralLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return theme_.body();
}

void AuralLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(theme_.spacing.sm,
                    1,
                    box.getWidth() - theme_.sizes.icon - 2 * theme_.spacing.sm,
                    box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void AuralLookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& g,
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

int AuralLookAndFeel::getPopupMenuBorderSize()
{
    return theme_.spacing.xs;
}

juce::Font AuralLookAndFeel::getPopupMenuFont()
{
    return theme_.body();
}

void AuralLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text,
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
    idealWidth = juce::GlyphArrangement::getStringWidthInt(font, text) + theme_.spacing.sm * 2 +
                 theme_.sizes.iconLarge + theme_.spacing.xl;
}

void AuralLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
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
        icon->drawWithin(g, iconColumn.toFloat().reduced(2.0f), juce::RectanglePlacement::centred, 1.0f);
    if (hasSubMenu)
        drawChevron(g, row.removeFromRight(theme_.sizes.icon + theme_.spacing.xs).toFloat(), colour, false);
    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont(theme_.timeSmall().withHeight(theme_.fonts.size.small));
        g.setColour(isActive ? c.textTertiary : c.textDisabled);
        const auto hint = row.removeFromRight(
            juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), shortcutKeyText) +
            theme_.spacing.sm);
        g.drawText(
            shortcutKeyText, hint.withTrimmedRight(theme_.spacing.sm), juce::Justification::centredRight);
    }
    g.setColour(colour);
    g.setFont(theme_.body());
    g.drawFittedText(text, row.withTrimmedRight(theme_.spacing.sm), juce::Justification::centredLeft, 1);
}

void AuralLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g,
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

juce::Font AuralLookAndFeel::getLabelFont(juce::Label& label)
{
    return label.getFont();
}

void AuralLookAndFeel::fillTextEditorBackground(juce::Graphics& g,
                                                int width,
                                                int height,
                                                juce::TextEditor& editor)
{
    g.setColour(editor.isEnabled() ? theme_.colours.surface2 : theme_.colours.surface1);
    g.fillRoundedRectangle(juce::Rectangle<int>(0, 0, width, height).toFloat(), theme_.radius.sm);
}

void AuralLookAndFeel::drawTextEditorOutline(juce::Graphics& g,
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

int AuralLookAndFeel::getSliderThumbRadius(juce::Slider& slider)
{
    return slider.isMouseOverOrDragging() ? 8 : 7;
}

juce::Label* AuralLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(theme_.value());
    label->setJustificationType(juce::Justification::centredRight);
    return label;
}

void AuralLookAndFeel::drawLinearSlider(juce::Graphics& g,
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

    g.setColour(c.surface2);
    g.fillRoundedRectangle(track, trackThickness * 0.5f);

    // Bipolar sliders fill from the centre; the rest from the start.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
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

int AuralLookAndFeel::getDefaultScrollbarWidth()
{
    return theme_.sizes.scrollbar;
}

void AuralLookAndFeel::drawScrollbar(juce::Graphics& g,
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

int AuralLookAndFeel::getTabButtonBestWidth(juce::TabBarButton& button, int /*tabDepth*/)
{
    return juce::GlyphArrangement::getStringWidthInt(theme_.label(), button.getButtonText()) +
           2 * theme_.spacing.md;
}

int AuralLookAndFeel::getTabButtonOverlap(int /*tabDepth*/)
{
    return 0;
}

void AuralLookAndFeel::drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) {}

void AuralLookAndFeel::drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) {}

void AuralLookAndFeel::drawTabButton(juce::TabBarButton& button,
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

juce::Rectangle<int> AuralLookAndFeel::getTooltipBounds(const juce::String& tipText,
                                                        juce::Point<int> screenPos,
                                                        juce::Rectangle<int> parentArea)
{
    const auto font = theme_.small();
    juce::TextLayout layout;
    juce::AttributedString text(tipText);
    text.setFont(font);
    layout.createLayout(text, static_cast<float>(theme_.sizes.tooltipMaxWidth - 2 * theme_.spacing.sm));
    const int w = static_cast<int>(layout.getWidth()) + 2 * theme_.spacing.sm;
    const int h = static_cast<int>(layout.getHeight()) + 2 * theme_.spacing.xs + theme_.spacing.xs;
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + theme_.spacing.sm)
                                                                      : screenPos.x + theme_.spacing.sm,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + theme_.spacing.sm)
                                                                      : screenPos.y + theme_.spacing.xl,
                                w,
                                h)
        .constrainedWithin(parentArea);
}

void AuralLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(theme_.colours.surface3);
    g.fillRoundedRectangle(bounds, theme_.radius.sm);
    g.setColour(theme_.colours.lineStrong);
    g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.sm, 1.0f);
    juce::TextLayout layout;
    juce::AttributedString attributed(text);
    attributed.setFont(theme_.small());
    attributed.setColour(theme_.colours.textPrimary);
    layout.createLayout(attributed, static_cast<float>(width - 2 * theme_.spacing.sm));
    layout.draw(g,
                bounds.reduced(static_cast<float>(theme_.spacing.sm), static_cast<float>(theme_.spacing.xs)));
}

void AuralLookAndFeel::drawProgressBar(juce::Graphics& g,
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

void AuralLookAndFeel::drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header)
{
    g.setColour(theme_.colours.surface0);
    g.fillRect(header.getLocalBounds());
    g.setColour(theme_.colours.lineSubtle);
    g.fillRect(header.getLocalBounds().removeFromBottom(1));
}

void AuralLookAndFeel::drawTableHeaderColumn(juce::Graphics& g,
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

} // namespace aural::ui
