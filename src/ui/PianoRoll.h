#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aural::ui
{

// The piano roll: one MIDI clip's notes on a pitch/time grid with a keyboard at the
// left, a bar/beat ruler on top and a velocity lane underneath. Edits go out as
// commands (onEdit) and the view is refreshed from the model by the owner, like
// the arrange view. Keys match the arrange view where they overlap: Delete,
// Ctrl+A, Ctrl+D (duplicate), Esc, G (snap), arrows (nudge), Q (quantise).
//
// Mouse: click on empty grid adds a note of one grid step; click a note to select
// (Shift adds, Ctrl toggles); drag a note's body to move it in time and pitch;
// drag its right edge to resize; drag in empty space to lasso; drag in the
// velocity lane over a note's bar to set its velocity. Ctrl+wheel zooms time.
class PianoRoll final : public juce::Component, private juce::ScrollBar::Listener
{
public:
    PianoRoll();
    ~PianoRoll() override;

    // The clip to edit (null project or a missing clip shows nothing).
    void setClip(const core::Project* project, core::Id trackId, core::Id clipId);
    core::Id trackId() const noexcept { return trackId_; }
    core::Id clipId() const noexcept { return clipId_; }
    void setPlayhead(core::SamplePosition position);

    std::function<void(std::vector<std::unique_ptr<core::Command>>, const std::string& name)> onEdit;

    // The grid as a fraction of a beat (1 = a beat, 4 = a sixteenth); snap on/off.
    void setGridDivision(int division);
    int gridDivision() const noexcept { return gridDivision_; }
    void setSnapEnabled(bool on);
    bool snapEnabled() const noexcept { return snap_; }
    // The grid in samples at the project's tempo.
    core::SamplePosition gridSamples() const noexcept;

    const std::vector<core::Id>& selection() const noexcept { return selection_; }
    void selectAll();
    void clearSelection();
    void deleteSelection();
    void duplicateSelection();
    void quantiseSelection(float strength = 1.0f);
    void nudgeSelection(int timeSteps, int semitones);
    bool handleEditKey(const juce::KeyPress& key);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    static constexpr int keyboardWidth = 56;
    static constexpr int rulerHeight = 28;
    static constexpr int velocityHeight = 84;
    static constexpr int rowHeight = 12;
    static constexpr int scrollbarSize = 12;
    static constexpr int edgeZone = 6;

private:
    enum class Drag
    {
        none,
        move,
        resize,
        lasso,
        velocity,
    };

    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    const core::MidiClip* clip() const noexcept;
    juce::Rectangle<int> gridArea() const noexcept;
    juce::Rectangle<int> velocityArea() const noexcept;
    int xForSample(core::SamplePosition s) const noexcept; // clip-relative
    core::SamplePosition sampleForX(int x) const noexcept;
    int yForNote(int note) const noexcept; // top of the row
    int noteForY(int y) const noexcept;
    core::SamplePosition snapped(core::SamplePosition s) const noexcept;
    juce::Rectangle<int> noteBounds(const core::MidiNote& note) const noexcept;
    const core::MidiNote* noteAt(juce::Point<int> p, bool& onRightEdge) const noexcept;
    const core::MidiNote* noteAtVelocityBar(juce::Point<int> p) const noexcept;
    bool isSelected(core::Id note) const noexcept;
    void emit(std::vector<std::unique_ptr<core::Command>> commands, const std::string& name);
    void finishMove();
    void finishResize();
    void finishLasso();
    void finishVelocity();
    void addNoteAt(juce::Point<int> p);
    void updateScrollbars();
    void fitToClip();
    core::MidiNote previewOf(const core::MidiNote& note) const noexcept;
    static juce::String noteName(int note);

    const Theme& theme_ = Theme::dark();
    const core::Project* project_ = nullptr;
    core::Id trackId_ = core::invalidId, clipId_ = core::invalidId;
    core::SamplePosition playhead_ = -1;
    double samplesPerPixel_ = 100.0;
    core::SamplePosition scroll_ = 0; // first visible clip-relative sample
    int verticalScroll_ = 0;          // pixels from the top row (note 127)
    juce::ScrollBar horizontal_{false}, vertical_{true};
    bool updatingScrollbars_ = false;
    int gridDivision_ = 4;
    bool snap_ = true;

    std::vector<core::Id> selection_;
    Drag drag_ = Drag::none;
    bool draggedFar_ = false;
    core::Id dragNote_ = core::invalidId;
    core::MidiNote dragOriginal_;
    juce::Point<int> dragStart_;
    core::SamplePosition moveDelta_ = 0;
    int pitchDelta_ = 0;
    core::SamplePosition lengthPreview_ = 0;
    int velocityPreview_ = 0;
    juce::Rectangle<int> lasso_;
};

} // namespace aural::ui
