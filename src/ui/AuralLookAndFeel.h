#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace aural::ui
{

// Dark, flat look driven entirely by Theme tokens. Installed as the default
// LookAndFeel by the application.
class AuralLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit AuralLookAndFeel(const Theme& theme);

    const Theme& theme() const noexcept { return theme_; }

    // Buttons
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool isMouseOver,
                              bool isButtonDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawToggleButton(juce::Graphics& g,
                          juce::ToggleButton& button,
                          bool isMouseOver,
                          bool isButtonDown) override;
    void drawTickBox(juce::Graphics& g,
                     juce::Component& component,
                     float x,
                     float y,
                     float w,
                     float h,
                     bool ticked,
                     bool isEnabled,
                     bool isMouseOver,
                     bool isButtonDown) override;

    // Combo boxes and menus
    void drawComboBox(juce::Graphics& g,
                      int width,
                      int height,
                      bool isButtonDown,
                      int buttonX,
                      int buttonY,
                      int buttonW,
                      int buttonH,
                      juce::ComboBox& box) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    void drawPopupMenuBackgroundWithOptions(juce::Graphics& g,
                                            int width,
                                            int height,
                                            const juce::PopupMenu::Options& options) override;
    juce::Font getPopupMenuFont() override;

    // Labels
    juce::Font getLabelFont(juce::Label&) override;

    // Sliders
    void drawLinearSlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPos,
                          float minSliderPos,
                          float maxSliderPos,
                          juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;

    // Scrollbars
    void drawScrollbar(juce::Graphics& g,
                       juce::ScrollBar& scrollbar,
                       int x,
                       int y,
                       int width,
                       int height,
                       bool isScrollbarVertical,
                       int thumbStartPosition,
                       int thumbSize,
                       bool isMouseOver,
                       bool isMouseDown) override;
    int getDefaultScrollbarWidth() override { return 10; }

private:
    const Theme& theme_;
};

} // namespace aural::ui
