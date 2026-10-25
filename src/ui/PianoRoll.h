#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "core/TempoMap.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/components/SegmentedControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui
{

// The piano roll (screens/piano-roll.md): a toolbar (tool, snap, quantise, link zoom,
// audition, the scale highlight), a 64 px keyboard, a bar/beat ruler, the note
// grid and an 80 px lane that shows the notes' velocities or one controller
// (pitch bend, a CC, channel pressure; the menu on the lane's name picks it).
// It edits one MIDI clip; the track's other clips show as ghosts.
// Edits go out as commands (onEdit) and the view is refreshed from the model by
// the owner, like the arrange view.
//
// Tools: Draw adds a note of the last-used length at the snapped position (drag
// sets the length; Alt disables snap); Select clicks, Shift adds, Ctrl toggles,
// lassos, drags move (vertical = transpose, with a tooltip), edges resize,
// Ctrl+drag duplicates, double-click adds; Erase removes what is clicked or
// dragged across. Keys: 1/2/3 tools, Delete, Ctrl+A, Ctrl+D, Esc, Q quantises at
// the menu's strength (Shift+Q the ends too), arrows nudge (Shift fine, Ctrl an
// octave), + and - transpose, [ and ] change the grid.
//
// A controller lane draws each event as a point and its value as a step until
// the next. Draw paints points freehand at the grid (every 4 px with snap off),
// Shift+drag a straight line, replacing what was there in the span; Select
// clicks, Shift adds, lassos and drags points (time and value, Shift one axis),
// double-click adds; Erase removes what it touches; Delete removes the selection.
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
    // A key was pressed or released on the keyboard, or a note is being drawn,
    // clicked or dragged to another pitch (while auditioning is on): the owner
    // previews the note through the track's instrument.
    std::function<void(core::Id trackId, int note, bool on)> onPreviewNote;
    // Hearing notes while editing them (the toolbar's speaker toggle).
    void setAudition(bool on);
    bool audition() const noexcept { return audition_; }
    // The "link zoom to arrange" toggle: the owner feeds setTimeView while linked,
    // and a zoom or scroll here goes back through onTimeViewChanged (timeline samples
    // per pixel, the first visible timeline sample), so the link works both ways.
    bool zoomLinked() const noexcept { return linked_; }
    void setTimeView(double samplesPerPixel, core::SamplePosition absoluteFirstVisible);
    std::function<void(double samplesPerPixel, core::SamplePosition firstVisible)> onTimeViewChanged;

    // The grid as a fraction of a beat (1 = a beat, 4 = a sixteenth); snap on/off.
    void setGridDivision(int division);
    int gridDivision() const noexcept { return gridDivision_; }
    void setSnapEnabled(bool on);
    bool snapEnabled() const noexcept { return snap_; }
    // The grid on the tempo track (a fraction of the beat where the clip starts),
    // and its length in samples there.
    core::GridStep gridStep() const noexcept;
    core::SamplePosition gridSamples() const noexcept;

    enum class Tool
    {
        draw,
        select,
        erase,
    };
    void setTool(Tool tool);
    Tool tool() const noexcept { return tool_; }

    const std::vector<core::Id>& selection() const noexcept { return selection_; }
    void selectAll();
    void clearSelection();
    void deleteSelection();
    void duplicateSelection();
    void quantiseSelection(float strength, bool ends = false);
    void nudgeSelection(int timeSteps, int semitones, bool fine = false);
    void transposeSelection(int semitones);
    bool handleEditKey(const juce::KeyPress& key);

    // What the lane under the grid shows: the notes' velocities, or one controller.
    struct Lane
    {
        bool velocity = true;
        core::MidiControllerType type = core::MidiControllerType::controller;
        int number = 0; // the controller (control changes only)
        bool operator==(const Lane&) const = default;
        static Lane velocities() noexcept { return {}; }
        static Lane controller(core::MidiControllerType type, int number = 0) noexcept
        {
            return {false, type, type == core::MidiControllerType::controller ? number : 0};
        }
    };
    void setLane(Lane lane);
    const Lane& lane() const noexcept { return lane_; }
    // "Pitch bend", "CC 74 Cutoff"; brief: "BEND", "CC 74".
    static juce::String laneName(const Lane& lane, bool brief = false);
    // A lane by a short key ("velocity", "bend", "mod", "expression", "sustain",
    // "pressure", "cc74"); velocities for anything else.
    static Lane laneForKey(const juce::String& key);
    const std::vector<core::Id>& controllerSelection() const noexcept { return ctlSelection_; }
    void deleteControllerSelection();
    // Shading the rows and keys outside the project key's scale (the Scale toggle).
    void setScaleHighlight(bool on);
    bool scaleHighlight() const noexcept { return scaleHighlight_; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    bool keyPressed(const juce::KeyPress& key) override;

    static constexpr int keyboardWidth = 64;
    static constexpr int toolbarHeight = 32;
    static constexpr int rowHeight = 12;
    static constexpr int edgeZone = 6;

private:
    enum class Drag
    {
        none,
        draw,
        move,
        resize,
        lasso,
        velocity,
        erase,
        key, // a key held on the keyboard
        pan, // the middle button drags the view
        ctlDraw,
        ctlMove,
        ctlLasso,
        ctlErase,
    };
    struct ControlPoint
    {
        core::SamplePosition time = 0;
        int value = 0;
    };

    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    void notifyTimeView(); // tells a linked owner where the view went
    const core::MidiClip* clip() const noexcept;
    const core::Track* track() const noexcept;
    juce::Colour trackColour() const;
    juce::Rectangle<int> gridArea() const noexcept;
    juce::Rectangle<int> rulerArea() const noexcept;
    juce::Rectangle<int> keysArea() const noexcept;
    juce::Rectangle<int> velocityArea() const noexcept;
    int xForSample(core::SamplePosition s) const noexcept; // clip-relative
    core::SamplePosition sampleForX(int x) const noexcept;
    int yForNote(int note) const noexcept; // top of the row
    int noteForY(int y) const noexcept;
    core::SamplePosition snapped(core::SamplePosition s, bool force = false) const noexcept;
    juce::Rectangle<int> noteBounds(const core::MidiNote& note) const noexcept;
    const core::MidiNote* noteAt(juce::Point<int> p, bool& onRightEdge) const noexcept;
    const core::MidiNote* noteAtVelocityBar(juce::Point<int> p) const noexcept;
    bool isSelected(core::Id note) const noexcept;
    void emit(std::vector<std::unique_ptr<core::Command>> commands, const std::string& name);
    void finishMove(bool duplicate);
    void finishResize();
    void finishLasso();
    void finishVelocity();
    void finishDraw();
    void eraseAt(juce::Point<int> p);
    void addNoteAt(juce::Point<int> p, core::SamplePosition length);
    void updateScrollbars();
    void fitToClip();
    core::MidiNote previewOf(const core::MidiNote& note) const noexcept;
    void drawTooltip(juce::Graphics& g, const juce::String& text, juce::Point<int> at);
    // The controller lane.
    juce::Rectangle<int> laneLabelArea() const noexcept;
    int yForValue(int value) const noexcept;
    int valueForY(int y) const noexcept;
    core::SamplePosition laneTimeForX(int x) const noexcept;
    core::SamplePosition drawStep() const noexcept;
    std::vector<core::MidiControllerEvent> laneEvents() const;
    std::vector<core::MidiControllerEvent> previewLaneEvents() const;
    const core::MidiControllerEvent* controllerAt(juce::Point<int> p) const noexcept;
    bool isControllerSelected(core::Id id) const noexcept;
    juce::String valueText(int value) const;
    void paintLane(juce::Graphics& g, juce::Rectangle<int> area, juce::Colour colour);
    void showLaneMenu();
    void laneMouseDown(const juce::MouseEvent& e);
    void addDrawSegment(ControlPoint from, ControlPoint to);
    void finishControllerDraw();
    void finishControllerMove();
    void finishControllerLasso();
    void addControllerAt(juce::Point<int> p);
    void showSnapMenu();
    void showQuantiseMenu();
    void refreshToolbar();
    static juce::String noteName(int note);
    // Sounds `note` (ending any note sounding) while auditioning is on.
    void auditionNote(int note);
    void stopAudition();

    const Theme& theme_ = Theme::dark();
    const core::Project* project_ = nullptr;
    core::TempoMap tempoMap_;
    core::Id trackId_ = core::invalidId, clipId_ = core::invalidId;
    core::SamplePosition playhead_ = -1;
    double samplesPerPixel_ = 100.0;
    core::SamplePosition scroll_ = 0; // first visible clip-relative sample
    int verticalScroll_ = 0;          // pixels from the top row (note 127)
    juce::ScrollBar horizontal_{false}, vertical_{true};
    bool updatingScrollbars_ = false;
    int gridDivision_ = 4;
    bool snap_ = true;
    bool linked_ = false;
    float quantiseStrength_ = 1.0f;
    Tool tool_ = Tool::select;
    core::SamplePosition lastLength_ = 0; // the draw tool's length (0 = a grid step)
    int velocityHeight_ = 80;
    int rulerHeight_ = 32;
    int scrollbarSize_ = 10;

    // The toolbar: the tools as in the arrange view's corner (select, pencil,
    // erase; the same icons, keys 1, 3, 4 and cursors).
    IconButton selectTool_, pencilTool_, eraseTool_;
    void refreshToolButtons();
    juce::MouseCursor toolCursor() const;
    juce::TextButton snapButton_, quantiseButton_;
    IconButton link_, auditionButton_;
    juce::TextButton scaleButton_;
    bool scaleHighlight_ = true;
    bool audition_ = true;
    int auditioned_ = -1; // the note sounding while editing

    std::vector<core::Id> selection_;
    Drag drag_ = Drag::none;
    bool draggedFar_ = false;
    bool duplicating_ = false;
    core::Id dragNote_ = core::invalidId;
    core::MidiNote dragOriginal_;
    juce::Point<int> dragStart_, dragPoint_;
    core::SamplePosition moveDelta_ = 0;
    int pitchDelta_ = 0;
    core::SamplePosition lengthPreview_ = 0;
    int velocityPreview_ = 0;
    std::map<core::Id, int> velocityRamp_; // per-note velocities while dragging across bars
    juce::Rectangle<int> lasso_;
    core::MidiNote drawNote_; // the note being drawn
    int heldKey_ = -1;
    bool fitPending_ = false;
    core::SamplePosition panScroll_ = 0;
    int panVertical_ = 0;
    core::Id hoverNote_ = core::invalidId;
    bool hoverEdge_ = false;

    // The controller lane.
    Lane lane_;
    bool laneFocus_ = false; // the last click was in the lane (Delete and Ctrl+A act there)
    std::vector<core::Id> ctlSelection_;
    std::map<core::SamplePosition, int> ctlDrawPoints_; // the points being drawn, by time
    ControlPoint ctlDrawStart_, ctlDrawLast_;
    bool ctlLine_ = false;
    std::vector<core::Id> ctlErased_;
    core::Id ctlDragId_ = core::invalidId;
    core::MidiControllerEvent ctlOriginal_;
    core::SamplePosition ctlMoveDelta_ = 0;
    int ctlValueDelta_ = 0;
};

} // namespace northlight::ui
