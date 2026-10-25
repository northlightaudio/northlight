#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"
#include "core/TempoMap.h"
#include "ui/Theme.h"
#include "ui/timeline/TimelineViewport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace northlight::ui
{

// The tempo track (screens/arrange.md, "Tempo track"): a lane under the ruler that
// draws the tempo over the song as a curve, with a handle at the song's start and
// at each tempo change, and the time signature changes as tags along its top.
//
// Drag a handle up or down to change its tempo (Shift: finer), and a change's
// handle sideways to move it (snapped to the arrange grid). Double-click the lane
// to add a change there; double-click a handle to type its tempo. Right-click a
// handle to ramp into it from the previous tempo, type a value or delete it;
// right-click the lane to add a tempo or time signature change; right-click a
// signature tag to change or remove it. Delete removes the selected change.
// Every edit is one SetTempoMap command through onEdit, made when the gesture ends.
class TempoLane final : public juce::Component
{
public:
    // `view` is the arrange view's time axis (the lane sits at its left edge, so
    // its x is the view's), kept up to date by it; the header column is left of
    // view.originX.
    TempoLane(const Theme& theme, const TimelineViewport& view);

    void setProject(const core::Project* project);
    // The arrange view's snapping rule on a tempo map (a moved change snaps on the
    // tempo before it); positions are not snapped without it.
    std::function<core::SamplePosition(const core::TempoMap&, core::SamplePosition)> snap;
    // The song position the header's readout shows.
    std::function<core::SamplePosition()> playhead;

    std::function<void(core::TempoState, const std::string& name)> onEdit;
    std::function<void()> onHide;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    // A handle: -1 is the song's start, 0.. the tempo changes.
    std::optional<int> handleAt(juce::Point<int> p) const;
    std::optional<int> meterAt(juce::Point<int> p) const;
    juce::Point<float> handlePosition(int index) const;
    // Every handle's position (the start's first), from one tempo map: what paint
    // and hit testing walk, instead of a map per handle.
    std::vector<juce::Point<float>> handlePositions() const;
    float yForBpm(double bpm) const noexcept;
    double bpmForY(float y) const noexcept;
    juce::Rectangle<int> lane() const noexcept;
    core::TempoState shown() const; // the edited state while dragging, else the project's
    void commit(core::TempoState state, const std::string& name);
    double bpmOf(const core::TempoState& state, int index) const noexcept;
    void handleMenu(int index, juce::Point<int> screen);
    void laneMenu(core::SamplePosition at, juce::Point<int> screen);
    void meterMenu(int index, juce::Point<int> screen);
    void typeTempo(int index);
    void addChange(core::SamplePosition at);
    void updateRange();

    const Theme& theme_;
    const TimelineViewport& view_;
    int headerWidth() const noexcept { return view_.originX; }
    const core::Project* project_ = nullptr;
    core::TempoState state_;
    std::optional<core::TempoState> editing_; // while a handle is dragged
    int selected_ = -2;                       // -2: none
    int hover_ = -2;
    int dragging_ = -2;
    double dragStartBpm_ = 120.0;
    double dragStartQuarter_ = 0.0;
    juce::Point<int> dragStart_;
    bool draggedFar_ = false;
    bool movedSideways_ = false;
    double minBpm_ = 60.0, maxBpm_ = 180.0; // the lane's vertical range
    std::unique_ptr<juce::TextEditor> editor_;
};

} // namespace northlight::ui
