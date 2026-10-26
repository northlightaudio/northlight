#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"
#include "ui/Theme.h"
#include "ui/timeline/TimelineViewport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui
{

// What the lane under the piano roll's grid shows: the notes' velocities, or one
// controller (pitch bend, a CC, channel pressure).
struct MidiLane
{
    bool velocity = true;
    core::MidiControllerType type = core::MidiControllerType::controller;
    int number = 0; // the controller (control changes only)
    bool operator==(const MidiLane&) const = default;
    static MidiLane velocities() noexcept { return {}; }
    static MidiLane controller(core::MidiControllerType type, int number = 0) noexcept
    {
        return {false, type, type == core::MidiControllerType::controller ? number : 0};
    }
    // "Pitch bend", "CC 74 Cutoff"; brief: "BEND", "CC 74".
    static juce::String name(const MidiLane& lane, bool brief = false);
    // A lane by a short key ("velocity", "bend", "mod", "expression", "sustain",
    // "pressure", "cc74"); velocities for anything else.
    static MidiLane forKey(const juce::String& key);
};

// The piano roll's controller lane (screens/piano-roll.md): one controller of
// the clip, each event a point and its value a step until the next. Draw paints
// points freehand at the grid (every 4 px with snap off), Shift+drag a straight
// line, replacing what was there in the span; Select clicks, Shift adds, lassos
// and drags points (time and value, Shift one axis), double-click adds; Erase
// removes what it touches. A child of the piano roll over the lane row, on its
// time axis; edits go out through onEdit, one command each.
class ControllerLane final : public juce::Component
{
public:
    explicit ControllerLane(const Theme& theme);

    enum class Tool
    {
        select,
        draw,
        erase,
    };

    void setClip(const core::Project* project, core::Id trackId, core::Id clipId);
    void setLane(const MidiLane& lane);
    void setTool(Tool tool) noexcept { tool_ = tool; }
    void setColour(juce::Colour colour);

    // The owner's time axis in this component's coordinates (clip-relative samples).
    std::function<TimelineViewport()> view;
    // The owner's grid: a position snapped (when snap is on), one step's length, and
    // whether snap is on.
    std::function<core::SamplePosition(core::SamplePosition)> snap;
    std::function<core::SamplePosition()> gridSamples;
    std::function<bool()> snapEnabled;
    std::function<void(std::vector<std::unique_ptr<core::Command>>, const std::string& name)> onEdit;
    // A press here: Delete and Ctrl+A act on the lane until the grid is clicked.
    std::function<void()> onPressed;

    const std::vector<core::Id>& selection() const noexcept { return selection_; }
    void selectAll();
    void clearSelection();
    void deleteSelection();

    // The y of a value, and the value at a y (this component's coordinates).
    int yForValue(int value) const noexcept;
    int valueForY(int y) const noexcept;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

private:
    enum class Drag
    {
        none,
        draw,
        move,
        lasso,
        erase,
    };
    struct Point
    {
        core::SamplePosition time = 0;
        int value = 0;
    };

    const core::MidiClip* clip() const noexcept;
    TimelineViewport axis() const;
    core::SamplePosition timeForX(int x) const noexcept;
    core::SamplePosition drawStep() const noexcept;
    std::vector<core::MidiControllerEvent> events() const;
    std::vector<core::MidiControllerEvent> previewEvents() const;
    const core::MidiControllerEvent* eventAt(juce::Point<int> p) const noexcept;
    bool isSelected(core::Id id) const noexcept;
    juce::String valueText(int value) const;
    void addSegment(Point from, Point to);
    void finishDraw();
    void finishMove();
    void finishLasso();
    void addAt(juce::Point<int> p);
    void emit(std::unique_ptr<core::Command> command, const std::string& name);
    // A middle-button press pans the piano roll: it goes to the owner.
    bool forwardsToOwner(const juce::MouseEvent& e) const noexcept { return e.mods.isMiddleButtonDown(); }

    const Theme& theme_;
    const core::Project* project_ = nullptr;
    core::Id trackId_ = core::invalidId, clipId_ = core::invalidId;
    MidiLane lane_ = MidiLane::controller(core::MidiControllerType::controller, 1);
    Tool tool_ = Tool::select;
    juce::Colour colour_;

    std::vector<core::Id> selection_;
    Drag drag_ = Drag::none;
    bool draggedFar_ = false;
    juce::Point<int> dragStart_, dragPoint_;
    juce::Rectangle<int> lasso_;
    std::map<core::SamplePosition, int> drawPoints_; // the points being drawn, by time
    Point drawStart_, drawLast_;
    bool line_ = false;
    std::vector<core::Id> erased_;
    core::Id dragId_ = core::invalidId;
    core::MidiControllerEvent original_;
    core::SamplePosition moveDelta_ = 0;
    int valueDelta_ = 0;
};

} // namespace northlight::ui
