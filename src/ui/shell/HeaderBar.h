#pragma once

#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/components/NumericField.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

namespace northlight::ui
{

// A time readout (screens/transport.md): a surface2 field with mono text, in a readout
// well when the material is on. Click to type a position (bars.beats.ticks or
// m:ss.mmm), drag vertically on a segment to nudge it. The owner feeds the text
// and takes the typed value back through onTyped.
class TimeDisplay final : public juce::Component, public juce::SettableTooltipClient
{
public:
    TimeDisplay(const Theme& theme, bool primary);
    void setText(const juce::String& text);
    // Colour states: normal, recording (record), countIn (warning).
    enum class Tone
    {
        normal,
        recording,
        countIn,
    };
    void setTone(Tone tone);
    // Without its own well: the text sits in a readout well the owner paints.
    void setFramed(bool framed);
    // Secondary: textSecondary at rest instead of the readout colour (the
    // clock time beside the primary bars.beats.ticks).
    void setSecondary(bool secondary);
    // As wide as the text plus the readout inset on each side (unframed, the
    // header bar sizes it to this).
    int idealWidth() const;
    std::function<void(const juce::String&)> onTyped;
    std::function<void(int segment, int steps)> onNudge; // segment from the left, steps up/down

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    void beginTyping();
    void endTyping(bool commit);
    int segmentAt(int x) const;
    juce::Font textFont() const;
    int textLeft() const;

    const Theme& theme_;
    bool primary_;
    juce::String text_;
    Tone tone_ = Tone::normal;
    bool framed_ = true, secondary_ = false;
    std::unique_ptr<juce::TextEditor> editor_;
    int dragSegment_ = -1, dragStartY_ = 0, dragSteps_ = 0;
    bool dragged_ = false;
};

// A window button of the custom chrome (screens/shell.md): minimise, maximise/restore or
// close, the header's full height and flat. Hover fills it with surface2; close fills
// with `record` and turns its glyph to textOnColour. Windows routes clicks on it to
// the window (it is a caption control), so it shows hover but never a press.
class WindowButton final : public juce::Button
{
public:
    WindowButton(const Theme& theme, Icon icon, const juce::String& tooltip, bool closes);
    void setIcon(Icon icon)
    {
        icon_ = icon;
        repaint();
    }
    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;

private:
    const Theme& theme_;
    Icon icon_;
    bool closes_;
};

// The project title (screens/shell.md): two lines, left-aligned. A caption-caps label,
// "PROJECT", with " · EDITED" while there are unsaved changes, over the project's
// name with the menu chevron right after it. Borderless; hover shows a surface2
// face. Clicking opens the project menu.
class ProjectTitle final : public juce::Button
{
public:
    explicit ProjectTitle(const Theme& theme);
    void setProject(const juce::String& name, bool modified);
    // The width that shows it all (the name capped at `maxWidth`).
    int idealWidth(int maxWidth) const;
    void paintButton(juce::Graphics& g, bool isMouseOver, bool isButtonDown) override;

private:
    juce::Font nameFont() const;
    juce::String caption() const;
    const Theme& theme_;
    juce::String name_;
    bool modified_ = false;
};

// The header bar (screens/shell.md): app and project menus, undo/redo, the transport
// cluster with time, tempo and signature, and the right cluster of snap, ripple,
// zoom-fit and the panel toggles. The owner pushes the state each UI tick and
// receives actions through the callbacks; nothing here touches the model.
class HeaderBar final : public juce::Component
{
public:
    explicit HeaderBar(const Theme& theme);

    struct State
    {
        juce::String projectName = "Untitled";
        juce::String projectPath;
        bool modified = false;
        bool canUndo = false, canRedo = false;
        juce::String undoName, redoName;
        bool playing = false, recording = false, countingIn = false;
        bool loop = false, loopPossible = true, punch = false, click = false;
        int countInBars = 1;
        bool deviceReady = true;
        juce::String barsText = "1.1.000", timeText = "0:00.000";
        double tempo = 120.0;
        int signatureNumerator = 4, signatureDenominator = 4;
        bool masterClipped = false;
        bool snap = true, ripple = false;
        juce::String snapName = "Adaptive";
        bool mixerVisible = false, editorVisible = false, browserVisible = false;
        // The custom window chrome: the bar is the title bar, with window buttons.
        bool windowControls = false, maximised = false;
        bool operator==(const State&) const = default;
    };
    void setState(const State& state);
    // Rewrites the tooltips that carry a key (after the user rebinds one).
    void refreshTooltips();
    const State& state() const noexcept { return state_; }

    // Actions
    std::function<void()> onAppMenu, onProjectMenu;
    std::function<void()> onUndo, onRedo;
    std::function<void()> onGoToStart, onPlayStop, onStop, onRecord;
    std::function<void()> onToggleLoop, onTogglePunch, onToggleClick;
    std::function<void(int)> onCountInBars;
    std::function<void(const juce::String&)> onBarsTyped, onTimeTyped;
    std::function<void(int segment, int steps)> onBarsNudged, onTimeNudged;
    std::function<void(double)> onTempo;
    std::function<void(int, int)> onSignature;
    std::function<void()> onMasterClipClicked;
    // The snap button: the magnet toggles snap (keeping the grid), its chevron
    // opens the grid menu, placed under `snapButton()`.
    std::function<void()> onToggleSnap, onSnapMenu, onToggleRipple, onZoomFit;
    juce::Component& snapButton() noexcept { return snap_; }
    std::function<void()> onToggleMixer, onToggleEditor, onToggleBrowser;
    std::function<void()> onDeviceWarning;
    std::function<void()> onMinimise, onMaximise, onCloseWindow;

    // What a point (in the bar) is to the window: a window button, client (a
    // control) or caption (empty bar: drag to move, double-click to maximise).
    // Pure geometry: Windows asks during its own hit-testing.
    juce::Component::WindowControlKind windowControlAt(juce::Point<float> point) const;

    // The readout well (bars, time, tempo, signature), as last laid out.
    juce::Rectangle<int> readoutBounds() const noexcept { return readout_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    static constexpr int windowButtonWidth = 46; // as the Windows caption buttons
    // The count-in a click on its toggle moves to: Off, 1, 2, 4 bars, then Off.
    static int nextCountIn(int bars) noexcept;

private:
    class ClipIndicator;
    void refreshFromState();
    juce::Rectangle<int> clipIndicatorBounds() const;
    // Bars, time, tempo and signature: each field's width for its current text.
    std::array<int, 4> readoutWidths() const;

    const Theme& theme_;
    State state_; // what the controls show (the constructor applies the default)
    IconButton app_, undo_, redo_;
    ProjectTitle project_;
    IconButton goToStart_, play_, stop_, record_;
    IconButton loop_, punch_, click_, countIn_;
    TimeDisplay bars_, time_;
    NumericField tempo_;
    std::vector<double> tapTimesMs_; // the last tempo taps
    juce::ComboBox signature_;
    IconButton snap_, ripple_, zoomFit_, mixer_, editor_, browser_;
    WindowButton minimise_, maximise_, closeWindow_;
    // The trays the clusters sit in and the readout well (bars, time, tempo,
    // signature); set by resized().
    std::vector<juce::Rectangle<int>> trays_;
    juce::Rectangle<int> readout_;
    std::array<int, 4> readoutWidths_{};
    bool clipLit_ = false;
    bool lastPlaying_ = false, lastRecording_ = false;
};

} // namespace northlight::ui
