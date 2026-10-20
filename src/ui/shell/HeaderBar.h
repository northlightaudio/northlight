#pragma once

#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/components/NumericField.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace aural::ui
{

// A time readout (06-transport.md): a surface2 field with mono text, in a readout
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

    const Theme& theme_;
    bool primary_;
    juce::String text_;
    Tone tone_ = Tone::normal;
    std::unique_ptr<juce::TextEditor> editor_;
    int dragSegment_ = -1, dragStartY_ = 0, dragSteps_ = 0;
    bool dragged_ = false;
};

// The header bar (03-shell.md): app and project menus, undo/redo, the transport
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
    };
    void setState(const State& state);
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
    std::function<void()> onToggleSnap, onSnapMenu, onToggleRipple, onZoomFit;
    std::function<void()> onToggleMixer, onToggleEditor, onToggleBrowser;
    std::function<void()> onDeviceWarning;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    class ClipIndicator;
    void refreshFromState();
    juce::Rectangle<int> clipIndicatorBounds() const;

    const Theme& theme_;
    State state_;
    IconButton app_, undo_, redo_;
    juce::TextButton project_;
    IconButton goToStart_, play_, stop_, record_;
    IconButton loop_, punch_, click_, countIn_;
    TimeDisplay bars_, time_;
    NumericField tempo_;
    std::vector<double> tapTimesMs_; // the last tempo taps
    juce::ComboBox signature_;
    IconButton snap_, ripple_, zoomFit_, mixer_, editor_, browser_;
    bool clipLit_ = false;
    bool lastPlaying_ = false, lastRecording_ = false;
};

} // namespace aural::ui
