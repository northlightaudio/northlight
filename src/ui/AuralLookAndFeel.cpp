#include "ui/AuralLookAndFeel.h"

namespace aural::ui
{

AuralLookAndFeel::AuralLookAndFeel(const Theme& theme) : theme_(theme)
{
    const auto& c = theme_.colours;

    setColour(juce::ResizableWindow::backgroundColourId, c.background);
    setColour(juce::DocumentWindow::textColourId, c.textPrimary);

    setColour(juce::Label::textColourId, c.textPrimary);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    setColour(juce::TextButton::buttonColourId, c.surfaceRaised);
    setColour(juce::TextButton::buttonOnColourId, c.accent);
    setColour(juce::TextButton::textColourOffId, c.textPrimary);
    setColour(juce::TextButton::textColourOnId, c.accentText);

    setColour(juce::ToggleButton::textColourId, c.textPrimary);
    setColour(juce::ToggleButton::tickColourId, c.accent);
    setColour(juce::ToggleButton::tickDisabledColourId, c.textDisabled);

    setColour(juce::ComboBox::backgroundColourId, c.surfaceRaised);
    setColour(juce::ComboBox::textColourId, c.textPrimary);
    setColour(juce::ComboBox::outlineColourId, c.border);
    setColour(juce::ComboBox::arrowColourId, c.textSecondary);
    setColour(juce::ComboBox::buttonColourId, c.surfaceRaised);
    setColour(juce::ComboBox::focusedOutlineColourId, c.focus);

    setColour(juce::PopupMenu::backgroundColourId, c.surfaceRaised);
    setColour(juce::PopupMenu::textColourId, c.textPrimary);
    setColour(juce::PopupMenu::headerTextColourId, c.textSecondary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, c.accent);
    setColour(juce::PopupMenu::highlightedTextColourId, c.accentText);

    setColour(juce::ListBox::backgroundColourId, c.surface);
    setColour(juce::ListBox::outlineColourId, c.border);
    setColour(juce::ListBox::textColourId, c.textPrimary);

    setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, c.borderStrong);
    setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);

    setColour(juce::Slider::backgroundColourId, c.surfaceRaised);
    setColour(juce::Slider::trackColourId, c.accent);
    setColour(juce::Slider::thumbColourId, c.textPrimary);
    setColour(juce::Slider::rotarySliderFillColourId, c.accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, c.borderStrong);
    setColour(juce::Slider::textBoxTextColourId, c.textPrimary);
    setColour(juce::Slider::textBoxBackgroundColourId, c.surfaceRaised);
    setColour(juce::Slider::textBoxOutlineColourId, c.border);
    setColour(juce::Slider::textBoxHighlightColourId, c.accent.withAlpha(0.4f));

    setColour(juce::TextEditor::backgroundColourId, c.surfaceRaised);
    setColour(juce::TextEditor::textColourId, c.textPrimary);
    setColour(juce::TextEditor::outlineColourId, c.border);
    setColour(juce::TextEditor::focusedOutlineColourId, c.focus);
    setColour(juce::TextEditor::highlightColourId, c.accent.withAlpha(0.4f));
    setColour(juce::CaretComponent::caretColourId, c.accent);

    setColour(juce::TooltipWindow::backgroundColourId, c.surfaceRaised);
    setColour(juce::TooltipWindow::textColourId, c.textPrimary);
    setColour(juce::TooltipWindow::outlineColourId, c.borderStrong);

    setColour(juce::AlertWindow::backgroundColourId, c.surface);
    setColour(juce::AlertWindow::textColourId, c.textPrimary);
    setColour(juce::AlertWindow::outlineColourId, c.border);
}

// ----- buttons ------------------------------------------------------------------------

void AuralLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                            juce::Button& button,
                                            const juce::Colour& backgroundColour,
                                            bool isMouseOver,
                                            bool isButtonDown)
{
    const auto& c = theme_.colours;
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const bool on = button.getToggleState();

    juce::Colour fill = on ? backgroundColour : c.surfaceRaised;
    if (!button.isEnabled())
        fill = fill.withAlpha(0.5f);
    else if (isButtonDown)
        fill = on ? c.accent.darker(0.2f) : c.surfacePressed;
    else if (isMouseOver)
        fill = on ? c.accentHover : c.surfaceHover;

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, theme_.radius.md);

    g.setColour(button.hasKeyboardFocus(true) ? c.focus : c.borderStrong);
    g.drawRoundedRectangle(bounds, theme_.radius.md, 1.0f);
}

juce::Font AuralLookAndFeel::getTextButtonFont(juce::TextButton&, int /*buttonHeight*/)
{
    return juce::Font(juce::FontOptions{theme_.type.body});
}

void AuralLookAndFeel::drawToggleButton(juce::Graphics& g,
                                        juce::ToggleButton& button,
                                        bool isMouseOver,
                                        bool isButtonDown)
{
    const float boxSize = 16.0f;
    const auto bounds = button.getLocalBounds().toFloat();
    const float y = bounds.getCentreY() - boxSize * 0.5f;

    drawTickBox(g,
                button,
                4.0f,
                y,
                boxSize,
                boxSize,
                button.getToggleState(),
                button.isEnabled(),
                isMouseOver,
                isButtonDown);

    g.setColour(button.isEnabled() ? theme_.colours.textPrimary : theme_.colours.textDisabled);
    g.setFont(juce::FontOptions{theme_.type.body});
    g.drawFittedText(button.getButtonText(),
                     button.getLocalBounds().withTrimmedLeft(static_cast<int>(boxSize) + 12),
                     juce::Justification::centredLeft,
                     1);
}

void AuralLookAndFeel::drawTickBox(juce::Graphics& g,
                                   juce::Component& /*component*/,
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

    juce::Colour fill = ticked ? c.accent : c.surfaceRaised;
    if (!isEnabled)
        fill = fill.withAlpha(0.4f);
    else if (isMouseOver)
        fill = ticked ? c.accentHover : c.surfaceHover;

    g.setColour(fill);
    g.fillRoundedRectangle(box, theme_.radius.sm);
    g.setColour(ticked ? c.accent : c.borderStrong);
    g.drawRoundedRectangle(box, theme_.radius.sm, 1.0f);

    if (ticked)
    {
        juce::Path tick;
        tick.startNewSubPath(box.getX() + w * 0.25f, box.getCentreY());
        tick.lineTo(box.getX() + w * 0.43f, box.getBottom() - h * 0.3f);
        tick.lineTo(box.getRight() - w * 0.22f, box.getY() + h * 0.3f);
        g.setColour(c.accentText);
        g.strokePath(tick,
                     juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
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

    juce::Colour fill = c.surfaceRaised;
    if (!box.isEnabled())
        fill = fill.withAlpha(0.5f);
    else if (isButtonDown)
        fill = c.surfacePressed;
    else if (box.isMouseOver(true))
        fill = c.surfaceHover;

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, theme_.radius.md);
    g.setColour(box.hasKeyboardFocus(true) ? c.focus : c.borderStrong);
    g.drawRoundedRectangle(bounds, theme_.radius.md, 1.0f);

    // Chevron
    const float arrowSize = 8.0f;
    const auto arrowArea = bounds.removeFromRight(28.0f);
    juce::Path chevron;
    const float cx = arrowArea.getCentreX();
    const float cy = arrowArea.getCentreY();
    chevron.startNewSubPath(cx - arrowSize * 0.5f, cy - arrowSize * 0.25f);
    chevron.lineTo(cx, cy + arrowSize * 0.25f);
    chevron.lineTo(cx + arrowSize * 0.5f, cy - arrowSize * 0.25f);
    g.setColour(box.isEnabled() ? c.textSecondary : c.textDisabled);
    g.strokePath(chevron,
                 juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font AuralLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(juce::FontOptions{theme_.type.body});
}

void AuralLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(theme_.spacing.sm, 1, box.getWidth() - 28 - theme_.spacing.sm, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void AuralLookAndFeel::drawPopupMenuBackgroundWithOptions(juce::Graphics& g,
                                                          int width,
                                                          int height,
                                                          const juce::PopupMenu::Options& /*options*/)
{
    const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(theme_.colours.surfaceRaised);
    g.fillRoundedRectangle(bounds, theme_.radius.md);
    g.setColour(theme_.colours.borderStrong);
    g.drawRoundedRectangle(bounds.reduced(0.5f), theme_.radius.md, 1.0f);
}

juce::Font AuralLookAndFeel::getPopupMenuFont()
{
    return juce::Font(juce::FontOptions{theme_.type.body});
}

// ----- labels -----------------------------------------------------------------------------

juce::Font AuralLookAndFeel::getLabelFont(juce::Label& label)
{
    return label.getFont();
}

// ----- sliders ----------------------------------------------------------------------------

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
    const float trackThickness = 4.0f;
    const float thumbRadius = 7.0f;

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

    g.setColour(c.surfaceRaised);
    g.fillRoundedRectangle(track, trackThickness * 0.5f);
    g.setColour(c.borderStrong);
    g.drawRoundedRectangle(track, trackThickness * 0.5f, 1.0f);

    juce::Rectangle<float> filled = track;
    juce::Point<float> thumb;
    if (horizontal)
    {
        filled.setRight(juce::jlimit(track.getX(), track.getRight(), sliderPos));
        thumb = {sliderPos, track.getCentreY()};
    }
    else
    {
        filled.setTop(juce::jlimit(track.getY(), track.getBottom(), sliderPos));
        thumb = {track.getCentreX(), sliderPos};
    }

    g.setColour(slider.isEnabled() ? c.accent : c.textDisabled);
    g.fillRoundedRectangle(filled, trackThickness * 0.5f);

    g.setColour(slider.isEnabled() ? c.textPrimary : c.textDisabled);
    g.fillEllipse(juce::Rectangle<float>(thumbRadius * 2.0f, thumbRadius * 2.0f).withCentre(thumb));
    g.setColour(c.background);
    g.drawEllipse(juce::Rectangle<float>(thumbRadius * 2.0f, thumbRadius * 2.0f).withCentre(thumb), 1.0f);
}

// ----- scrollbars -------------------------------------------------------------------------

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

    juce::Rectangle<int> thumb;
    if (isScrollbarVertical)
        thumb = {x + 2, thumbStartPosition, width - 4, thumbSize};
    else
        thumb = {thumbStartPosition, y + 2, thumbSize, height - 4};

    const auto& c = theme_.colours;
    g.setColour((isMouseOver || isMouseDown) ? c.textDisabled : c.borderStrong);
    g.fillRoundedRectangle(thumb.toFloat(),
                           static_cast<float>(juce::jmin(thumb.getWidth(), thumb.getHeight())) * 0.5f);
}

} // namespace aural::ui
