#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace northlight::ui
{

// The icon set of docs/design/design-system.md section 2.5: vector paths on a
// 16 px grid, 1.5 px strokes with round caps, 1 px inner padding; monochrome, so
// the caller gives the colour. The same paths are written to assets/icons as SVG
// by scripts/export-icons.py for the design canvas.
enum class Icon
{
    play,
    pause,
    stop,
    record,
    loop,
    punch,
    metronome,
    countIn,
    goToStart,
    goToEnd,
    undo,
    redo,
    snap,
    ripple,
    zoomIn,
    zoomOut,
    zoomFit,
    mixer,
    browser,
    inspector,
    settings,
    plugin,
    power,
    pin,
    close,
    chevronDown,
    chevronRight,
    check,
    search,
    filter,
    add,
    remove,
    dragHandle,
    mute,
    solo,
    arm,
    monitor,
    phase,
    link,
    folder,
    fileAudio,
    fileMidi,
    marker,
    region,
    warning,
    info,
    error,
    success,
    loudness,
    cpu,
    disk,
    midiPort,
    detach,
    attach,
    mark, // the Northlight mark: Moonrise, a half moon over the horizon
    pianoRoll,
    diagnostics,
    menu,
    instrument, // a keyboard: the instruments rack
    pointer,    // the select tool
    rangeTool,  // the range tool: an I-beam between brackets
    pencil,     // the pencil tool: draws MIDI clips
    eraser,     // the erase tool: an eraser on its edge, over a line
    scissors,   // the split tool: open scissors, blades up
    glue,       // the glue tool: two clips bridged into one
    muteClip,   // the mute tool: a clip struck through
    // The audio editor's pitch tools (split uses the scissors).
    pitchTool,      // a note with an arrow up and down
    driftTool,      // a slow wander settling into a straight line
    vibratoTool,    // a quick wave
    breathTool,     // a breath of wind
    trackAudio,     // track kinds (ui/TrackIcon.h): a waveform
    trackMidi,      // an eighth note
    trackAux,       // an output jack with an arrow: an instrument's extra output
    windowMinimise, // the window buttons in the header (custom window chrome)
    windowMaximise,
    windowRestore,
    keyboard, // a computer keyboard: shortcuts
    // Effect types (the plugin menus): a compressor's bent transfer line, an EQ's
    // bell, a decaying train of echoes, a sine, a vacuum tube, two overlapping circles,
    // a plectrum.
    fxDynamics,
    fxEq,
    fxSpace,
    fxModulation,
    fxDrive,
    fxStereo,
    fxGuitar,
    // An eye, open and struck through: shown and hidden (an automation lane's hide).
    visible,
    hidden,
};

struct Icons
{
    // The path on the 16 px grid; `filled` says whether it is a solid shape
    // (play, stop, record) rather than a stroke.
    static juce::Path path(Icon icon, bool& filled);
    // Draws the icon scaled into `bounds` (square, the icon's own padding kept).
    static void draw(juce::Graphics& g, Icon icon, juce::Rectangle<float> bounds, juce::Colour colour);
    static const char* name(Icon icon);
    static const char* svgPath(Icon icon);
    // An edit tool's icon as its cursor (pencil, eraser, scissors, glue,
    // muteClip): the icon in textPrimary over a canvas outline, with the hot spot
    // where the tool acts. Built once per icon. The arrange view and the piano
    // roll share them.
    static juce::MouseCursor toolCursor(const Theme& theme, Icon icon);
    static constexpr int count = static_cast<int>(Icon::hidden) + 1;
};

// An icon button (28×28 default): ghost at rest, the icon in textSecondary,
// textPrimary on hover, accent (or the semantic colour) when toggled on, with
// the tooltip required of every icon. A small figure in the corner carries a
// value (the count-in bars); a chevron marks a menu.
class IconButton final : public juce::Button
{
public:
    IconButton(const Theme& theme, Icon icon, const juce::String& tooltip);

    void setIcon(Icon icon);
    Icon icon() const noexcept { return icon_; }
    // The colour for the on state: "accent" (default), "record", "solo", "monitor", "warning".
    void setOnColour(juce::Colour colour)
    {
        onColour_ = colour;
        repaint();
    }
    void setIconSize(int px)
    {
        iconSize_ = px;
        repaint();
    }
    // A 9 px figure overlaid in the bottom-right corner (empty = none).
    void setCornerText(const juce::String& text);
    void setShowsMenuChevron(bool show)
    {
        chevron_ = show;
        repaint();
    }
    // A split button: the chevron's part opens a menu on press (as does a
    // right-click anywhere), the rest clicks as usual. Hover and press light the
    // part under the pointer, with a hairline between the two.
    void setMenuPart(std::function<void()> onMenu)
    {
        onMenu_ = std::move(onMenu);
        chevron_ = static_cast<bool>(onMenu_);
        repaint();
    }
    juce::Rectangle<int> menuPart() const noexcept;
    // The whole button fills with the on colour (the recording state).
    void setSolidWhenOn(bool solid)
    {
        solidOn_ = solid;
        repaint();
    }
    // Drawn as a key in the control material (design-system.md 2.4) at rest,
    // rather than a bare icon that only shows a face on hover.
    void setKey(bool key)
    {
        if (key == key_)
            return;
        key_ = key;
        repaint();
    }
    // A warning dot in the top-right corner.
    void setWarningDot(bool show)
    {
        if (show == warningDot_)
            return;
        warningDot_ = show;
        repaint();
    }
    // Icon colour at rest when not toggled (default textSecondary).
    void setRestColour(juce::Colour colour)
    {
        if (colour == restColour_)
            return;
        restColour_ = colour;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    void setOverMenu(bool over);
    const Theme& theme_;
    std::function<void()> onMenu_;
    bool overMenu_ = false, menuPressed_ = false;
    Icon icon_;
    juce::Colour onColour_, restColour_;
    juce::String corner_;
    int iconSize_ = 16;
    bool chevron_ = false, solidOn_ = false, warningDot_ = false, key_ = false;
};

} // namespace northlight::ui
