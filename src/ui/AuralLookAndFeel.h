#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <tuple>

namespace aural::ui
{

// The look and feel every JUCE stock control gets, drawn from the design tokens
// to the component specs in docs/design/02-design-system.md: buttons (secondary,
// primary, ghost, danger), toggles (accent and the semantic M/S/R/monitor
// colours), combos, fields, checkboxes, horizontal sliders, scrollbars, tabs,
// menus, tooltips and progress bars. A control picks a variant through its
// component properties (see the `property*` names); dedicated components
// (fader, knob, meter, segmented control, numeric field, panel header, toast,
// sheet) live in ui/components and draw themselves from the same tokens.
class AuralLookAndFeel final : public juce::LookAndFeel_V4, private juce::MouseListener
{
public:
    explicit AuralLookAndFeel(const Theme& theme);
    ~AuralLookAndFeel() override;

    const Theme& theme() const noexcept { return theme_; }

    // ----- component properties that pick a variant ------------------------------
    // Button: "variant" = "secondary" (default) | "primary" | "ghost" | "danger".
    static const juce::Identifier propertyVariant;
    // Toggle-styled TextButton (on = accent fill) rather than the stock on-colour;
    // "semantic" = "arm" | "solo" | "mute" | "monitor" | "record" picks the colour,
    // and "hardOn" = true fills solidly (the armed state).
    static const juce::Identifier propertySemantic;
    static const juce::Identifier propertyHardOn;
    // Button that opens a menu: a trailing chevron.
    static const juce::Identifier propertyMenu;
    // Icon-only button: square, no label padding.
    static const juce::Identifier propertyIcon;

    // Focus rings are drawn for keyboard focus only: the app reports the last
    // input kind (a Tab or arrow press turns it on, any click turns it off).
    static void setKeyboardFocusVisible(bool visible) noexcept;
    static bool keyboardFocusVisible() noexcept;
    // Draws the 2 px lineFocus ring outside `bounds` when the component has
    // keyboard focus and keyboard navigation is active.
    void drawFocusRing(juce::Graphics& g,
                       const juce::Component& component,
                       juce::Rectangle<float> bounds,
                       float radius) const;

    // ----- the tactile layer (02-design-system.md 2.4), shared with components ---
    // A control face: the material gradient (or a flat fill when no material is
    // loaded), the top highlight and the drop shadow; pressed reverses the
    // gradient and insets the shadow.
    void drawControlFace(juce::Graphics& g,
                         juce::Rectangle<float> bounds,
                         float radius,
                         juce::Colour flat,
                         bool pressed,
                         bool hover) const;
    // A soft glow of a colour around a rounded rectangle (cached per size and
    // colour, drawn as an image, never blurred per frame).
    void
    drawGlow(juce::Graphics& g, juce::Rectangle<float> bounds, float radius, const Theme::Shadow& glow) const;
    // An inset shadow along the top inside a rounded rectangle (a groove, a well).
    void drawInset(juce::Graphics& g,
                   juce::Rectangle<float> bounds,
                   float radius,
                   const Theme::Shadow& inset) const;

    // ----- buttons --------------------------------------------------------------
    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool isMouseOver,
                              bool isButtonDown) override;
    void
    drawButtonText(juce::Graphics& g, juce::TextButton& button, bool isMouseOver, bool isButtonDown) override;
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

    // ----- combo boxes, menus ---------------------------------------------------
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
    void drawPopupMenuItem(juce::Graphics& g,
                           const juce::Rectangle<int>& area,
                           bool isSeparator,
                           bool isActive,
                           bool isHighlighted,
                           bool isTicked,
                           bool hasSubMenu,
                           const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    void getIdealPopupMenuItemSize(const juce::String& text,
                                   bool isSeparator,
                                   int standardMenuItemHeight,
                                   int& idealWidth,
                                   int& idealHeight) override;
    int getPopupMenuBorderSize() override;
    // Menus (and combo lists, submenus) open with the motion table's 120 ms fade
    // and 2 px slide into place.
    void preparePopupMenuWindow(juce::Component& window) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuSectionHeader(juce::Graphics& g,
                                    const juce::Rectangle<int>& area,
                                    const juce::String& sectionName) override;

    // ----- labels, fields -------------------------------------------------------
    juce::Font getLabelFont(juce::Label&) override;
    void
    fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;

    // ----- sliders --------------------------------------------------------------
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
    int getSliderThumbRadius(juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider& slider) override;

    // ----- scrollbars -----------------------------------------------------------
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
    int getDefaultScrollbarWidth() override;

    // ----- tabs -----------------------------------------------------------------
    void
    drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;
    int getTabButtonBestWidth(juce::TabBarButton&, int tabDepth) override;
    int getTabButtonOverlap(int tabDepth) override;

    // ----- tooltips, progress, tables ------------------------------------------
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText,
                                          juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawProgressBar(juce::Graphics& g,
                         juce::ProgressBar& bar,
                         int width,
                         int height,
                         double progress,
                         const juce::String& textToShow) override;
    void drawTableHeaderBackground(juce::Graphics& g, juce::TableHeaderComponent& header) override;
    void drawTableHeaderColumn(juce::Graphics& g,
                               juce::TableHeaderComponent& header,
                               const juce::String& columnName,
                               int columnId,
                               int width,
                               int height,
                               bool isMouseOver,
                               bool isMouseDown,
                               int columnFlags) override;

private:
    void mouseDown(const juce::MouseEvent&) override;
    void drawChevron(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, bool down) const;
    void drawCheck(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour) const;

    const Theme& theme_;
    struct GlowKey
    {
        int width, height;
        juce::uint32 colour;
        float radius, blur;
        bool operator<(const GlowKey& o) const noexcept
        {
            return std::tie(width, height, colour, radius, blur) <
                   std::tie(o.width, o.height, o.colour, o.radius, o.blur);
        }
    };
    mutable std::map<GlowKey, juce::Image> glows_;
};

} // namespace aural::ui
