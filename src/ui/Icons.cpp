#include "ui/Icons.h"

#include "ui/NorthlightLookAndFeel.h"

#include <map>

namespace northlight::ui
{

namespace
{
struct Definition
{
    const char* name;
    const char* d; // SVG path data on the 16 px grid
    bool filled;
    const char* solid = nullptr; // a filled part drawn under a stroked icon (the mark's moon)
};

// Stroked unless `filled`; round caps and joins are applied when drawing.
const Definition definitions[Icons::count] = {
    {"play", "M5 3 L13 8 L5 13 Z", true},
    {"pause", "M4.5 3 H6.75 V13 H4.5 Z M9.25 3 H11.5 V13 H9.25 Z", true},
    {"stop", "M4 4 H12 V12 H4 Z", true},
    {"record", "M8 3.5 A4.5 4.5 0 1 1 7.99 3.5 Z", true},
    {"loop",
     "M3 6.5 V5.5 A2 2 0 0 1 5 3.5 H11 M11 3.5 L9 1.5 M11 3.5 L9 5.5 M13 9.5 V10.5 A2 2 0 0 1 11 12.5 H5 M5 "
     "12.5 L7 10.5 M5 12.5 L7 14.5",
     false},
    {"punch", "M4.5 3 H2.5 V13 H4.5 M11.5 3 H13.5 V13 H11.5 M8 6.5 A1.5 1.5 0 1 1 7.99 6.5 Z", false},
    {"metronome", "M6 2.5 H10 L13 13.5 H3 Z M8 6.5 V13.5 M5.5 7 L11 3.5", false},
    {"countIn", "M6 2.5 H10 L13 13.5 H3 Z M8 6.5 V13.5 M10.5 7 L5 3.5", false},
    {"goToStart", "M4 3 V13 M13 3 L6 8 L13 13 Z", false},
    {"goToEnd", "M12 3 V13 M3 3 L10 8 L3 13 Z", false},
    {"undo", "M6 4 L2.5 7.5 L6 11 M2.5 7.5 H10 A3.5 3.5 0 0 1 10 14.5 H7", false},
    {"redo", "M10 4 L13.5 7.5 L10 11 M13.5 7.5 H6 A3.5 3.5 0 0 0 6 14.5 H9", false},
    {"snap", "M4 2.5 V8 A4 4 0 0 0 12 8 V2.5 M4 2.5 H6.5 V6 M12 2.5 H9.5 V6", false},
    {"ripple", "M2 8 C4 4 6 12 8 8 C10 4 12 12 14 8", false},
    {"zoomIn", "M7 2.5 A4.5 4.5 0 1 1 6.99 2.5 Z M10.2 10.2 L13.5 13.5 M7 5 V9 M5 7 H9", false},
    {"zoomOut", "M7 2.5 A4.5 4.5 0 1 1 6.99 2.5 Z M10.2 10.2 L13.5 13.5 M5 7 H9", false},
    {"zoomFit", "M2.5 6 V2.5 H6 M10 2.5 H13.5 V6 M13.5 10 V13.5 H10 M6 13.5 H2.5 V10", false},
    {"mixer", "M4 2.5 V13.5 M8 2.5 V13.5 M12 2.5 V13.5 M2.5 10 H5.5 M6.5 5 H9.5 M10.5 8 H13.5", false},
    {"browser", "M2.5 3.5 H13.5 V12.5 H2.5 Z M6 3.5 V12.5 M2.5 7 H6", false},
    {"inspector", "M2.5 3 H13.5 M2.5 7 H9 M2.5 11 H11 M11.5 6 A2 2 0 1 1 11.49 6 Z", false},
    {"settings",
     "M8 5.5 A2.5 2.5 0 1 1 7.99 5.5 Z M8 1.5 V3 M8 13 V14.5 M1.5 8 H3 M13 8 H14.5 M3.4 3.4 L4.5 4.5 M11.5 "
     "11.5 L12.6 12.6 M3.4 12.6 L4.5 11.5 M11.5 4.5 L12.6 3.4",
     false},
    {"plugin", "M5 2.5 V5 M11 2.5 V5 M3.5 5 H12.5 V9 A4.5 4.5 0 0 1 3.5 9 Z M8 13.5 V11.5", false},
    {"power", "M8 2 V8 M4.5 4.5 A5 5 0 1 0 11.5 4.5", false},
    {"pin", "M6 2.5 H10 L9 7 L12 10 H4 L7 7 Z M8 10 V14", false},
    {"close", "M4 4 L12 12 M12 4 L4 12", false},
    {"chevronDown", "M4 6 L8 10 L12 6", false},
    {"chevronRight", "M6 4 L10 8 L6 12", false},
    {"check", "M3 8.5 L6.5 12 L13 4.5", false},
    {"search", "M7 2.5 A4.5 4.5 0 1 1 6.99 2.5 Z M10.2 10.2 L13.5 13.5", false},
    {"filter", "M2.5 3.5 H13.5 L9.5 8.5 V13 L6.5 11.5 V8.5 Z", false},
    {"add", "M8 3 V13 M3 8 H13", false},
    {"remove", "M3 8 H13", false},
    {"dragHandle", "M6 4 H6.01 M10 4 H10.01 M6 8 H6.01 M10 8 H10.01 M6 12 H6.01 M10 12 H10.01", false},
    {"mute", "M2.5 6 H5 L8.5 3 V13 L5 10 H2.5 Z M10.5 6 L13.5 10 M13.5 6 L10.5 10", false},
    {"solo", "M2.5 6 H5 L8.5 3 V13 L5 10 H2.5 Z M11 5.5 A3.5 3.5 0 0 1 11 10.5", false},
    {"arm", "M8 3.5 A4.5 4.5 0 1 1 7.99 3.5 Z", true},
    {"monitor", "M2 8 C4 4 12 4 14 8 C12 12 4 12 2 8 Z M8 6.5 A1.5 1.5 0 1 1 7.99 6.5 Z", false},
    {"phase", "M8 2 A6 6 0 1 1 7.99 2 Z M3.5 12.5 L12.5 3.5", false},
    {"link",
     "M6.5 9.5 L9.5 6.5 M7 4.5 L8.5 3 A2.5 2.5 0 0 1 13 7.5 L11.5 9 M9 11.5 L7.5 13 A2.5 2.5 0 0 1 3 8.5 "
     "L4.5 7",
     false},
    {"folder", "M2.5 4 H6.5 L8 5.5 H13.5 V12.5 H2.5 Z", false},
    {"fileAudio",
     "M3.5 2.5 H9.5 L12.5 5.5 V13.5 H3.5 Z M9.5 2.5 V5.5 H12.5 M5.5 10.5 V8.5 M7.5 11.5 V7.5 M9.5 10.5 V8.5",
     false},
    {"fileMidi",
     "M3.5 2.5 H9.5 L12.5 5.5 V13.5 H3.5 Z M9.5 2.5 V5.5 H12.5 M5.5 8 V11.5 M8 8 V11.5 M10.5 8 V11.5",
     false},
    {"marker", "M4 2.5 V13.5 M4 2.5 H11 L9 5.5 L11 8.5 H4", false},
    {"region", "M3 2.5 V13.5 M13 2.5 V13.5 M3 5 H13 M3 11 H13", false},
    {"warning", "M8 2.5 L14 13.5 H2 Z M8 6.5 V9.5 M8 11.5 V11.51", false},
    {"info", "M8 2.5 A5.5 5.5 0 1 1 7.99 2.5 Z M8 7.5 V11 M8 5.5 V5.51", false},
    {"error", "M8 2.5 A5.5 5.5 0 1 1 7.99 2.5 Z M5.5 5.5 L10.5 10.5 M10.5 5.5 L5.5 10.5", false},
    {"success", "M8 2.5 A5.5 5.5 0 1 1 7.99 2.5 Z M5 8 L7.2 10.2 L11 6", false},
    {"loudness", "M2.5 8.5 V10 M5.5 5 V11 M8.5 2.5 V13.5 M11.5 6 V10 M14 7.5 V8.5", false},
    {"cpu",
     "M4 4 H12 V12 H4 Z M6.5 6.5 H9.5 V9.5 H6.5 Z M6 1.5 V4 M10 1.5 V4 M6 12 V14.5 M10 12 V14.5 M1.5 6 H4 "
     "M1.5 10 H4 M12 6 H14.5 M12 10 H14.5",
     false},
    {"disk", "M8 2.5 A5.5 5.5 0 1 1 7.99 2.5 Z M8 6.5 A1.5 1.5 0 1 1 7.99 6.5 Z", false},
    {"midiPort",
     "M8 2.5 A5.5 5.5 0 1 1 7.99 2.5 Z M5.5 7.5 V7.51 M10.5 7.5 V7.51 M8 5.5 V5.51 M6 10.5 V10.51 M10 10.5 "
     "V10.51",
     false},
    {"detach", "M9 2.5 H13.5 V7 M13.5 2.5 L8 8 M6.5 4 H3 V13 H12 V9.5", false},
    {"attach", "M7 9 H2.5 V13.5 M2.5 13.5 L8 8 M13 2.5 H4 V5.5 M13 2.5 V11.5 H10.5", false},
    // The brand mark (docs/design/brand.md): Moonrise, a half moon over the horizon (its reflection left
    // out at 16 px).
    {"mark", "M1.75 10.5 H14.25", false, "M4.25 8.5 A3.75 3.75 0 0 1 11.75 8.5 Z"},
    {"pianoRoll", "M2.5 3.5 H13.5 V12.5 H2.5 Z M5.5 3.5 V12.5 M9.5 3.5 V12.5 M2.5 8 H13.5", false},
    {"diagnostics", "M2 9 H4.5 L6.5 4 L9.5 12 L11.5 7 H14", false},
    {"menu", "M3 4.5 H13 M3 8 H13 M3 11.5 H13", false},
    {"instrument",
     "M2 4 H14 V12.5 H2 Z M5 8.5 V12.5 M8 8.5 V12.5 M11 8.5 V12.5 M4 4 V8.5 H6 V4 M10 4 V8.5 H12 V4",
     false},
    {"pointer", "M4.5 2.5 V12.5 L7 10 L9 13.5 L10.5 12.7 L8.6 9.3 L12 9 Z", false},
    {"rangeTool",
     "M6.5 3 H9.5 M8 3 V13 M6.5 13 H9.5 M3.5 5.5 L2 8 L3.5 10.5 M12.5 5.5 L14 8 L12.5 10.5",
     false},
    {"pencil", "M10.5 2.5 L13.5 5.5 L5.5 13.5 H2.5 V10.5 Z M9 4 L12 7", false},
    {"eraser", "M9.5 2.5 L13.5 6.5 L7 13 H4.5 L2.5 11 Z M6 6 L10 10 M7 13 H13.5", false},
    {"scissors",
     "M4.5 9.5 A2 2 0 1 1 4.49 9.5 Z M11.5 9.5 A2 2 0 1 1 11.49 9.5 Z M5.9 10.1 L11.5 2.5 M10.1 10.1 L4.5 "
     "2.5",
     false},
    {"glue", "M2.5 4.5 H7 V11.5 H2.5 Z M9 4.5 H13.5 V11.5 H9 Z M5.5 8 H10.5", false},
    {"muteClip", "M2.5 4.5 H13.5 V11.5 H2.5 Z M3 13.5 L13 2.5", false},
    {"pitchTool",
     "M2 7 H9 V10 H2 Z M12.5 2.5 V13.5 M10.5 4.5 L12.5 2.5 L14.5 4.5 M10.5 11.5 L12.5 13.5 L14.5 11.5",
     false},
    {"driftTool", "M1.5 11 C3.5 4 6 4 8 8 H14.5", false},
    {"vibratoTool",
     "M1.5 8 C2.5 5 3.5 5 4.5 8 C5.5 11 6.5 11 7.5 8 C8.5 5 9.5 5 10.5 8 C11.5 11 12.5 11 13.5 8",
     false},
    {"breathTool", "M2 6 H9.5 A2 2 0 1 0 7.5 4 M2 9.5 H12 A2 2 0 1 1 10 11.5 M2 13 H6", false},
    // Track kinds (instrument channels use "instrument", folders "folder").
    {"trackAudio", "M1.5 8 H3.5 L5 5 L6.5 11 L8 3 L9.5 13 L11 6 L12.5 8 H14.5", false},
    {"trackMidi", "M6 9.25 A2.25 2.25 0 1 1 5.99 9.25 Z M8.25 11.5 V2.5 C9 4.5 12.5 4.5 12.5 7.5", false},
    {"trackAux", "M5.5 5 A3 3 0 1 1 5.49 5 Z M8.5 8 H14 M11.5 5.5 L14 8 L11.5 10.5", false},
    // The window buttons: thin, 10 px glyphs, after the Windows caption buttons.
    {"windowMinimise", "M3 8 H13", false},
    {"windowMaximise", "M3.5 3.5 H12.5 V12.5 H3.5 Z", false},
    {"windowRestore", "M5.5 3.5 H12.5 V10.5 M3.5 5.5 H10.5 V12.5 H3.5 Z", false},
    {"keyboard", "M2 4.5 H14 V11.5 H2 Z M4.5 7 H4.51 M7 7 H7.01 M9 7 H9.01 M11.5 7 H11.51 M5 9.5 H11", false},
    {"fxDynamics", "M2.5 2.5 V13.5 H13.5 M2.5 13.5 L7.5 8.5 C8.5 7.5 9.5 7 13.5 5.5", false},
    {"fxEq", "M1.5 12.5 C4 12.5 5.3 12.5 6.3 8 C7.2 3.5 8.8 3.5 9.7 8 C10.7 12.5 12 12.5 14.5 12.5", false},
    {"fxSpace", "M3 2.5 V13.5 M6.5 4.5 V11.5 M10 6 V10 M13.5 7.25 V8.75", false},
    {"fxModulation", "M1.5 8 C3.5 2.5 5.5 2.5 8 8 C10.5 13.5 12.5 13.5 14.5 8", false},
    {"fxDrive",
     "M5 11.5 V6.5 A3 3 0 0 1 11 6.5 V11.5 Z M3.5 11.5 H12.5 M6 14 V11.5 M10 14 V11.5 M6.75 9.5 L8 7.5 L9.25 "
     "9.5",
     false},
    {"fxStereo", "M6 4 A4 4 0 1 1 5.99 4 Z M10 4 A4 4 0 1 1 9.99 4 Z", false},
    {"fxGuitar",
     "M8 14 C5.2 11.2 2.5 7.6 2.5 5 C2.5 2.9 5 2 8 2 C11 2 13.5 2.9 13.5 5 C13.5 7.6 10.8 11.2 8 14 Z",
     false},
    {"visible", "M1.5 8 C3.5 4 12.5 4 14.5 8 C12.5 12 3.5 12 1.5 8 Z M8 6 A2 2 0 1 1 7.99 6 Z", false},
    {"hidden", "M1.5 8 C3.5 4 12.5 4 14.5 8 C12.5 12 3.5 12 1.5 8 Z M2.5 13.5 L13.5 2.5", false},
};
} // namespace

juce::Path Icons::path(Icon icon, bool& filled)
{
    static std::map<int, juce::Path> cache;
    const int index = static_cast<int>(icon);
    const auto& definition = definitions[index];
    filled = definition.filled;
    auto it = cache.find(index);
    if (it == cache.end())
        it = cache.emplace(index, juce::Drawable::parseSVGPath(definition.d)).first;
    return it->second;
}

const char* Icons::name(Icon icon)
{
    return definitions[static_cast<int>(icon)].name;
}

const char* Icons::svgPath(Icon icon)
{
    return definitions[static_cast<int>(icon)].d;
}

void Icons::draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds, juce::Colour colour)
{
    bool filled = false;
    auto path = Icons::path(icon, filled);
    // The grid is 16 units; keep the icon's padding by scaling the whole grid.
    const float scale = std::min(bounds.getWidth(), bounds.getHeight()) / 16.0f;
    const auto transform = juce::AffineTransform::scale(scale).translated(
        bounds.getX() + (bounds.getWidth() - 16.0f * scale) * 0.5f,
        bounds.getY() + (bounds.getHeight() - 16.0f * scale) * 0.5f);
    g.setColour(colour);
    if (const auto* solid = definitions[static_cast<int>(icon)].solid)
    {
        static std::map<int, juce::Path> solids;
        auto it = solids.find(static_cast<int>(icon));
        if (it == solids.end())
            it = solids.emplace(static_cast<int>(icon), juce::Drawable::parseSVGPath(solid)).first;
        g.fillPath(it->second, transform);
    }
    if (filled)
        g.fillPath(path, transform);
    else
        g.strokePath(
            path,
            juce::PathStrokeType(1.5f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
            transform);
}

juce::MouseCursor Icons::toolCursor(const Theme& theme, Icon icon)
{
    static std::map<int, juce::MouseCursor> cache;
    if (const auto it = cache.find(static_cast<int>(icon)); it != cache.end())
        return it->second;
    // The hot spot on the 20 px image (the icon drawn at 18 px): the pencil's
    // tip, the eraser's edge, between the scissors' blades, else the middle.
    const juce::Point<int> spot = icon == Icon::pencil     ? juce::Point<int>{2, 17}
                                  : icon == Icon::eraser   ? juce::Point<int>{3, 15}
                                  : icon == Icon::scissors ? juce::Point<int>{9, 3}
                                                           : juce::Point<int>{9, 9};
    juce::Image image(juce::Image::ARGB, 20, 20, true);
    {
        juce::Graphics g(image);
        draw(g, icon, juce::Rectangle<float>(1.0f, 1.0f, 18.0f, 18.0f), theme.colours.canvas);
        draw(g, icon, juce::Rectangle<float>(0.0f, 0.0f, 18.0f, 18.0f), theme.colours.textPrimary);
    }
    return cache.emplace(static_cast<int>(icon), juce::MouseCursor(image, spot.x, spot.y)).first->second;
}

// ----- icon button --------------------------------------------------------------------------

IconButton::IconButton(const Theme& theme, Icon icon, const juce::String& tooltip)
    : juce::Button(Icons::name(icon)), theme_(theme), icon_(icon)
{
    setTooltip(tooltip);
    setTitle(tooltip.upToFirstOccurrenceOf("  ", false, false)); // the name, without the shortcut
    setWantsKeyboardFocus(true);
    setSize(theme_.sizes.control, theme_.sizes.control);
}

void IconButton::setIcon(Icon icon)
{
    if (icon == icon_ && getName() == Icons::name(icon))
        return; // the same icon: nothing to repaint (the header sets it every frame)
    icon_ = icon;
    setName(Icons::name(icon));
    repaint();
}

void IconButton::setCornerText(const juce::String& cornerText)
{
    if (cornerText == corner_)
        return;
    corner_ = cornerText;
    repaint();
}

juce::Rectangle<int> IconButton::menuPart() const noexcept
{
    // The chevron's slot at the right: lg (16 px), beside an icon part as wide as
    // the button is tall.
    return getLocalBounds().removeFromRight(theme_.spacing.lg);
}

void IconButton::setOverMenu(bool over)
{
    if (over != overMenu_)
    {
        overMenu_ = over;
        repaint();
    }
}

void IconButton::mouseMove(const juce::MouseEvent& e)
{
    setOverMenu(onMenu_ && menuPart().contains(e.getPosition()));
    juce::Button::mouseMove(e);
}

void IconButton::mouseExit(const juce::MouseEvent& e)
{
    setOverMenu(false);
    juce::Button::mouseExit(e);
}

void IconButton::mouseDown(const juce::MouseEvent& e)
{
    menuPressed_ = false;
    if (onMenu_ && isEnabled() && (e.mods.isPopupMenu() || menuPart().contains(e.getPosition())))
    {
        // Menus open on press; the button itself is not clicked.
        menuPressed_ = true;
        setOverMenu(menuPart().contains(e.getPosition()));
        repaint();
        onMenu_();
        return;
    }
    juce::Button::mouseDown(e);
}

void IconButton::mouseDrag(const juce::MouseEvent& e)
{
    if (!menuPressed_)
        juce::Button::mouseDrag(e);
}

void IconButton::mouseUp(const juce::MouseEvent& e)
{
    if (std::exchange(menuPressed_, false))
    {
        repaint();
        return;
    }
    juce::Button::mouseUp(e);
}

void IconButton::paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown)
{
    const auto& c = theme_.colours;
    const auto bounds = getLocalBounds().toFloat();
    const float radius = theme_.radius.sm;
    const bool on = getToggleState();
    auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel());
    // Unset colours follow the theme, so an accent or contrast change shows at once.
    const auto onColour = onColour_.isTransparent() ? c.accent : onColour_;
    const auto restColour = restColour_.isTransparent() ? c.textSecondary : restColour_;

    juce::Colour iconColour = restColour;
    // A split button lights only the part under the pointer.
    const bool split = static_cast<bool>(onMenu_);
    const auto part = !split      ? bounds
                      : overMenu_ ? menuPart().toFloat()
                                  : bounds.withTrimmedRight(static_cast<float>(menuPart().getWidth()));
    const auto fillPart = [&](juce::Colour colour)
    {
        g.saveState();
        g.reduceClipRegion(part.toNearestInt());
        g.setColour(colour);
        g.fillRoundedRectangle(bounds.reduced(1.0f), radius);
        g.restoreState();
    };
    // A key keeps its face in every state except solid-on, which fills it.
    const bool keyFace = key_ && !(on && solidOn_);
    if (keyFace)
    {
        const bool live = isEnabled();
        if (lnf != nullptr)
            lnf->drawControlFace(
                g, bounds.reduced(1.0f), radius, c.surface2, live && isButtonDown, live && isMouseOver);
        else
        {
            g.setColour(c.surface2);
            g.fillRoundedRectangle(bounds.reduced(1.0f), radius);
        }
    }
    if (!isEnabled())
        iconColour = c.textDisabled;
    else if (on)
    {
        if (lnf != nullptr)
        {
            const auto& m = theme_.material;
            const auto& glow = onColour == c.record || onColour == c.arm ? m.glowRecord
                               : onColour == c.solo                      ? m.glowSolo
                               : onColour == c.monitor                   ? m.glowMonitor
                                                                         : m.glowAccent;
            lnf->drawGlow(g, bounds.reduced(2.0f), radius, glow);
        }
        if (solidOn_)
        {
            g.setColour(isButtonDown  ? onColour.darker(0.15f)
                        : isMouseOver ? onColour.brighter(0.1f)
                                      : onColour);
            g.fillRoundedRectangle(bounds.reduced(1.0f), radius);
            iconColour = c.textOnColour;
        }
        else
        {
            g.setColour(onColour.withAlpha(0.2f));
            g.fillRoundedRectangle(bounds.reduced(1.0f), radius);
            if (isMouseOver || isButtonDown || menuPressed_)
                fillPart(onColour.withAlpha(0.08f));
            g.setColour(onColour);
            g.drawRoundedRectangle(bounds.reduced(1.5f), radius, 1.0f);
            iconColour = onColour;
        }
    }
    else if (isButtonDown || menuPressed_)
    {
        if (!keyFace)
            fillPart(c.surfacePressed);
        iconColour = c.textPrimary;
    }
    else if (isMouseOver)
    {
        if (!keyFace)
            fillPart(c.surface2);
        iconColour = c.textPrimary;
    }
    // On a split button the icon and the chevron light apart: the part not
    // under the pointer keeps its colour at rest.
    auto chevronColour = iconColour;
    if (split && isEnabled() && !on && (isMouseOver || isButtonDown || menuPressed_))
    {
        if (overMenu_)
            iconColour = restColour;
        else
            chevronColour = restColour;
    }
    if (split && isEnabled() && (isMouseOver || menuPressed_))
    {
        g.setColour(c.lineSubtle);
        g.fillRect(juce::Rectangle<float>(
            static_cast<float>(menuPart().getX()), bounds.getY() + 5.0f, 1.0f, bounds.getHeight() - 10.0f));
    }

    // A split button centres the icon in its part and the chevron in its slot;
    // a plain menu button nudges the icon left for a chevron at its edge.
    const auto iconPart = split ? bounds.withTrimmedRight(static_cast<float>(menuPart().getWidth())) : bounds;
    auto iconArea =
        iconPart.withSizeKeepingCentre(static_cast<float>(iconSize_), static_cast<float>(iconSize_));
    if (chevron_ && !split)
        iconArea.translate(-static_cast<float>(theme_.spacing.xs) * 0.5f, 0.0f);
    Icons::draw(g, icon_, iconArea, iconColour);
    if (chevron_)
    {
        const auto chevronArea =
            split ? menuPart().toFloat().withSizeKeepingCentre(8.0f, 8.0f).translated(0.0f, 1.0f)
                  : juce::Rectangle<float>(bounds.getRight() - static_cast<float>(theme_.spacing.sm) - 2.0f,
                                           bounds.getCentreY() - 3.0f,
                                           8.0f,
                                           8.0f);
        Icons::draw(g, Icon::chevronDown, chevronArea, chevronColour.withMultipliedAlpha(0.8f));
    }
    if (corner_.isNotEmpty())
    {
        g.setFont(theme_.font(Theme::Face::ui, 9.0f, theme_.fonts.weight.semibold));
        g.setColour(iconColour);
        g.drawText(corner_, bounds.reduced(2.0f).toNearestInt(), juce::Justification::bottomRight);
    }
    if (warningDot_)
    {
        g.setColour(c.warning);
        g.fillEllipse(bounds.getRight() - 7.0f, bounds.getY() + 2.0f, 5.0f, 5.0f);
    }
    if (lnf != nullptr)
        lnf->drawFocusRing(g, *this, bounds.reduced(1.0f), radius);
}

} // namespace northlight::ui
