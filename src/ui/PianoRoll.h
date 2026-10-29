#pragma once

#include "core/CommandFwd.h"
#include "core/Model.h"
#include "core/TempoMap.h"
#include "engine/Recording.h"
#include "ui/Icons.h"
#include "ui/KeyboardMenu.h"
#include "ui/Theme.h"
#include "ui/components/SegmentedControl.h"
#include "ui/edit/NoteGrid.h"
#include "ui/edit/NoteOps.h"
#include "ui/pianoroll/ControllerLane.h"
#include "ui/timeline/TimelineViewport.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <bitset>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace northlight::ui
{

// The piano roll (screens/piano-roll.md): a toolbar (tools, snap grid, quantise,
// default velocity, link zoom, follow, audition, other tracks' ghosts, named rows,
// the scale highlight and the project key), a 64 px keyboard, a bar/beat ruler,
// the note grid and one or more lanes under it (each the notes' velocities or one
// controller: pitch bend, a CC, channel pressure; the menu on a lane's name picks
// it, adds a lane or removes one; a lane's top edge drags its height).
// It edits one MIDI clip; the track's other clips show as ghosts, and with the
// ghosts toggle the other MIDI tracks' notes too. An instrument that names its
// notes (a drum kit) labels the rows with them, and the named-rows toggle hides
// the rows it does not play. Edits go out as commands (onEdit) and the view is
// refreshed from the model by the owner, like the arrange view.
//
// Tools (keys as the arrange view's): Select (1) clicks, Shift adds, Ctrl
// toggles, lassos, drags move (vertical = transpose, with a tooltip), edges
// resize, Ctrl+drag duplicates, double-click adds; Draw (3) adds a note of the
// last-used length and the default velocity at the snapped position (drag sets
// the length; Alt disables snap); Erase (4) removes what is clicked or dragged
// across; Split (5) cuts a note at the pointer (the selection's notes there too);
// Glue (6) joins a note with the next of its pitch (a selection's runs); Mute (7)
// mutes or unmutes what it is clicked or dragged across (muted notes draw hollow
// and play nothing). Right-click opens the note menu: clipboard, quantise, swing
// and humanise, velocity, length and legato, transpose, mute, split and join,
// select all of a pitch.
//
// Keys: 1, 3-7 tools; Delete; Ctrl+A, Esc; Ctrl+C, Ctrl+X, Ctrl+V (paste at the
// playhead inside the clip, else at the pointer), Ctrl+D (duplicate after the
// selection); Q quantises at the menu's strength and swing (Shift+Q the ends
// too); G snap; [ and ] the grid coarser or finer; arrows nudge (Shift fine,
// Ctrl+Up/Down an octave); + and - transpose a semitone; Ctrl+Left/Right select
// the previous or next note (Shift extends); Alt+Left/Right shorten or lengthen
// by a grid step (Shift: a quarter of one); Ctrl+M mutes; S splits at the
// playhead; J joins; Z zooms to the notes; Ctrl+= and Ctrl+- zoom time.
// Wheel scrolls the notes, Shift+wheel time, Ctrl+wheel zooms time at the
// pointer, Ctrl+Shift+wheel (or Alt+wheel) zooms the rows.
//
// A controller lane draws each event as a point and its value as a step until
// the next. Draw paints points freehand at the grid (every 4 px with snap off),
// Shift+drag a straight line, replacing what was there in the span; Select
// clicks, Shift adds, lassos and drags points (time and value, Shift one axis),
// double-click adds; Erase removes what it touches; Delete removes the selection.
// The velocity lane drags a bar's cap: the selection moves with it, each by the
// same amount (Alt sets them all to the value); dragging across bars ramps them.
class PianoRoll final : public juce::Component, public KeyboardMenu, private juce::ScrollBar::Listener
{
public:
    explicit PianoRoll(const Theme& theme = Theme::dark());
    ~PianoRoll() override;

    // The clip to edit (null project or a missing clip shows nothing).
    void setClip(const core::Project* project, core::Id trackId, core::Id clipId);
    core::Id trackId() const noexcept { return trackId_; }
    core::Id clipId() const noexcept { return clipId_; }
    void setPlayhead(core::SamplePosition position);
    // The recording in progress (the arrange view's, filled once a frame): the
    // notes coming in on this track draw in `record` until they land in the clip.
    void setLiveRecording(const engine::LiveRecording* live);
    void liveRecordingChanged();

    std::function<void(std::vector<std::unique_ptr<core::Command>>, const std::string& name)> onEdit;
    // A key was pressed or released on the keyboard, or a note is being drawn,
    // clicked or dragged to another pitch (while auditioning is on): the owner
    // previews the note through the track's instrument.
    std::function<void(core::Id trackId, int note, bool on)> onPreviewNote;
    // The note selection changed (the inspector shows the selected notes).
    std::function<void()> onSelectionChanged;
    // Hearing notes while editing them (the toolbar's speaker toggle).
    void setAudition(bool on);
    bool audition() const noexcept { return audition_; }
    // The "link zoom to arrange" toggle: the owner feeds setTimeView while linked,
    // and a zoom or scroll here goes back through onTimeViewChanged (timeline samples
    // per pixel, the first visible timeline sample), so the link works both ways.
    bool zoomLinked() const noexcept { return linked_; }
    void setTimeView(double samplesPerPixel, core::SamplePosition absoluteFirstVisible);
    std::function<void(double samplesPerPixel, core::SamplePosition firstVisible)> onTimeViewChanged;
    // Following the playhead (the toolbar toggle): a playhead that leaves the view
    // while it plays turns the page.
    void setFollowPlayhead(bool on);
    bool followPlayhead() const noexcept { return follow_; }
    double samplesPerPixel() const noexcept { return samplesPerPixel_; }
    core::SamplePosition scrollPosition() const noexcept { return scroll_; }

    // The snap grid (edit/NoteGrid.h): an index into noteGrids; snap on/off.
    void setGrid(std::size_t index);
    std::size_t grid() const noexcept { return grid_; }
    void setSnapEnabled(bool on);
    bool snapEnabled() const noexcept { return snap_; }
    // The grid on the tempo track at a clip-relative position (a fraction of the
    // beat of the bar there, as the arrange view counts it), and its length in
    // samples there.
    core::GridStep gridStep(core::SamplePosition at = 0) const noexcept;
    core::SamplePosition gridSamples(core::SamplePosition at = 0) const noexcept;

    enum class Tool
    {
        draw,
        select,
        erase,
        split,
        glue,
        mute,
    };
    void setTool(Tool tool);
    Tool tool() const noexcept { return tool_; }

    const std::vector<core::Id>& selection() const noexcept { return selection_; }
    void setSelection(std::vector<core::Id> notes);
    void selectAll();
    void clearSelection();
    void deleteSelection();
    void duplicateSelection();
    void copySelection();
    void cutSelection();
    // At the playhead when it is inside the clip, else at the pointer's last place
    // in the grid, else at the view's left edge; the pasted notes are selected.
    void paste();
    void pasteAt(core::SamplePosition clipRelative);
    bool clipboardEmpty() const noexcept;
    void quantiseSelection(float strength, bool ends = false);
    void quantiseSelection(const noteops::QuantiseOptions& options);
    void humaniseSelection(core::SamplePosition timing, int velocity);
    void nudgeSelection(int timeSteps, int semitones, bool fine = false);
    void transposeSelection(int semitones);
    void stepSelection(int direction, bool extend);
    void changeLength(int steps, bool fine);
    // Alt+Up / Down: the selected notes louder or softer by `delta` (8, Shift 1).
    void changeVelocity(int delta);
    // Insert (step entry, interaction.md 10.4): a note right after the last
    // selected one, at its pitch, length and velocity; with none selected, a grid
    // step long at the playhead (else the view's left edge) on the middle row of
    // the view. The new note is selected and sounds.
    void insertNote();
    // Shift+F10 or the Menu key: the note menu, at the first selected note.
    bool showKeyboardMenu() override;
    void legatoSelection();
    void toggleMuteSelection();
    void splitSelectionAt(core::SamplePosition clipRelative);
    void joinSelection();
    void selectPitch(int note);
    void zoomToFitNotes();
    bool handleEditKey(const juce::KeyPress& key);

    // New notes take this velocity: the last one edited, or one picked in the
    // toolbar's menu.
    int defaultVelocity() const noexcept { return velocity_; }
    void setDefaultVelocity(int velocity);

    // What the lanes under the grid show: the notes' velocities, or a controller
    // each (drawn and edited by a ControllerLane over its row). The first lane is
    // lane(); setLane changes it.
    using Lane = MidiLane;
    void setLane(Lane lane);
    const Lane& lane() const noexcept { return lanes_.front().lane; }
    // Another lane under the last (at most maxLanes), and one taken away (never the last).
    void addLane(Lane lane);
    void removeLane(std::size_t index);
    std::size_t laneCount() const noexcept { return lanes_.size(); }
    const Lane& laneAt(std::size_t index) const noexcept { return lanes_[index].lane; }
    int laneHeight(std::size_t index) const noexcept { return lanes_[index].height; }
    void setLaneHeight(std::size_t index, int height);
    // "Pitch bend", "CC 74 Cutoff"; brief: "BEND", "CC 74".
    static juce::String laneName(const Lane& lane, bool brief = false);
    // A lane by a short key ("velocity", "bend", "mod", "expression", "sustain",
    // "pressure", "cc74"); velocities for anything else.
    static Lane laneForKey(const juce::String& key);
    const std::vector<core::Id>& controllerSelection() const noexcept;
    void deleteControllerSelection();
    // Shading the rows and keys outside the project key's scale (the Scale toggle).
    void setScaleHighlight(bool on);
    // Other MIDI tracks' notes as ghosts (the toolbar's ghost toggle).
    void setGhostTracks(bool on);
    // An instrument that names its notes: the rows carry the names; with
    // namedRowsOnly, only the named rows (and any with notes) show.
    bool hasNoteNames() const noexcept { return namedNotes_.any(); }
    juce::String rowName(int note) const;
    void setNamedRowsOnly(bool on);
    bool namedRowsOnly() const noexcept { return namedOnly_; }
    // The rows' height (the vertical zoom).
    int rowHeight() const noexcept { return rowHeight_; }
    void setRowHeight(int pixels);
    // Pixels from the top of the first row (the vertical scroll).
    int verticalScroll() const noexcept { return verticalScroll_; }

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override; // the region focus ring
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
    static constexpr int defaultRowHeight = 12;
    static constexpr int minRowHeight = 6, maxRowHeight = 32;
    static constexpr int edgeZone = 6;
    static constexpr int defaultLaneHeight = 80, controllerLaneHeight = 120;
    static constexpr int minLaneHeight = 40, maxLaneHeight = 320;
    static constexpr std::size_t maxLanes = 4;
    static constexpr int laneResizeZone = 3; // px either side of a lane's top edge

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
        mute,
        key,        // a key held on the keyboard
        pan,        // the middle button drags the view
        laneHeight, // a lane's top edge
    };
    struct LaneRow
    {
        Lane lane;
        int height = defaultLaneHeight;
        std::unique_ptr<ControllerLane> controller; // null for velocities
    };

    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    void notifyTimeView(); // tells a linked owner where the view went
    void selectionChanged();
    const core::MidiClip* clip() const noexcept;
    const core::Track* track() const noexcept;
    juce::Colour trackColour() const;
    juce::Colour colourOf(const core::Track& track) const;
    juce::Rectangle<int> gridArea() const noexcept;
    juce::Rectangle<int> rulerArea() const noexcept;
    juce::Rectangle<int> keysArea() const noexcept;
    int lanesHeight() const noexcept;
    juce::Rectangle<int> lanesArea() const noexcept; // every lane, under the grid
    juce::Rectangle<int> laneArea(std::size_t index) const noexcept;
    juce::Rectangle<int> laneLabelArea(std::size_t index) const noexcept;
    int laneEdgeAt(juce::Point<int> p) const noexcept; // the lane whose top edge is at p, or -1
    int laneIndexAt(juce::Point<int> p) const noexcept;
    int velocityLaneAt(juce::Point<int> p) const noexcept; // a velocity lane under p, or -1
    // The time axis, in clip-relative samples from the keyboard's edge.
    TimelineViewport view() const noexcept { return {keyboardWidth, scroll_, samplesPerPixel_}; }
    // The same axis in timeline samples, for the grid and the ruler.
    TimelineViewport timelineView(const core::MidiClip& clip) const noexcept
    {
        return {keyboardWidth, clip.start + scroll_, samplesPerPixel_};
    }
    int xForSample(core::SamplePosition s) const noexcept; // clip-relative
    core::SamplePosition sampleForX(int x) const noexcept;
    int yForNote(int note) const noexcept; // top of the row (hidden rows: off the grid)
    int noteForY(int y) const noexcept;
    bool rowShown(int note) const noexcept { return rowOf_[static_cast<size_t>(note & 127)] >= 0; }
    int rowsShown() const noexcept { return static_cast<int>(rows_.size()); }
    int maxVerticalScroll() const noexcept;
    void rebuildRows();
    void refreshNoteNames();
    core::SamplePosition snapped(core::SamplePosition s, bool force = false) const noexcept;
    juce::Rectangle<int> noteBounds(const core::MidiNote& note) const noexcept;
    const core::MidiNote* noteAt(juce::Point<int> p, bool& onRightEdge) const noexcept;
    const core::MidiNote* noteAtVelocityBar(juce::Point<int> p, int lane) const noexcept;
    bool isSelected(core::Id note) const noexcept;
    void emit(std::vector<std::unique_ptr<core::Command>> commands, const std::string& name);
    // An edit's notes as commands (set, add, remove), one step; the added notes
    // are selected when `selectAdded`.
    void emitEdit(const noteops::NoteEdit& edit, const std::string& name, bool selectAdded = false);
    std::vector<core::Id> targets() const; // the selection, or every note when none
    void finishMove(bool duplicate);
    void finishResize();
    void finishLasso();
    void finishVelocity(bool absolute);
    void finishDraw();
    void finishMuteSweep();
    void eraseAt(juce::Point<int> p);
    void muteAt(juce::Point<int> p);
    void splitAt(juce::Point<int> p);
    void glueAt(juce::Point<int> p);
    void addNoteAt(juce::Point<int> p, core::SamplePosition length);
    void addNote(const core::MidiNote& note, const std::string& name); // selects it
    void updateScrollbars();
    void fitToClip();
    core::MidiNote previewOf(const core::MidiNote& note) const noexcept;
    void drawTooltip(juce::Graphics& g, const juce::String& text, juce::Point<int> at);
    // paint() in parts, over what one paint covers.
    struct PaintFrame
    {
        juce::Rectangle<int> painted, grid, paintedGrid, lanes, ruler, keys;
        const core::MidiClip* clip = nullptr;
        const core::Track* track = nullptr;
        juce::Colour colour;
        core::MusicalKey key;
    };
    void paintToolbar(juce::Graphics& g, const PaintFrame& f);
    void paintRows(juce::Graphics& g, const PaintFrame& f);
    void paintNotes(juce::Graphics& g, const PaintFrame& f);
    void paintGhosts(juce::Graphics& g, const PaintFrame& f);
    void paintLiveNotes(juce::Graphics& g, const PaintFrame& f);
    void paintNote(juce::Graphics& g, const core::MidiNote& note, juce::Colour colour, bool selected);
    void paintGridOverlay(juce::Graphics& g, const PaintFrame& f);
    void paintKeys(juce::Graphics& g, const PaintFrame& f);
    void paintRuler(juce::Graphics& g, const PaintFrame& f);
    void paintLanes(juce::Graphics& g, const PaintFrame& f);
    void paintVelocityLane(juce::Graphics& g, const PaintFrame& f, juce::Rectangle<int> area);
    int yForValue(std::size_t lane,
                  int value) const noexcept; // a controller lane's, in this view's coordinates
    void showLaneMenu(std::size_t index);
    void showSnapMenu();
    void showQuantiseMenu();
    void showVelocityMenu();
    void showKeyMenu();
    void showNoteMenu(juce::Point<int> at);
    void refreshToolbar();
    void layoutLanes();
    std::unique_ptr<ControllerLane> makeControllerLane();
    static juce::String noteName(int note);
    juce::String labelFor(int note) const; // the instrument's name for a note, else its name
    // Sounds `note` (ending any note sounding) while auditioning is on.
    void auditionNote(int note);
    void stopAudition();
    static noteops::Clipboard& clipboard();
    std::uint32_t nextSeed() noexcept { return seed_ = seed_ * 1664525u + 1013904223u; }

    const Theme& theme_;
    const core::Project* project_ = nullptr;
    core::TempoMap tempoMap_;
    core::Id trackId_ = core::invalidId, clipId_ = core::invalidId;
    core::SamplePosition playhead_ = -1;
    double samplesPerPixel_ = 100.0;
    core::SamplePosition scroll_ = 0; // first visible clip-relative sample
    int verticalScroll_ = 0;          // pixels from the top row
    int rowHeight_ = defaultRowHeight;
    std::vector<int> rows_;        // the notes shown, top to bottom
    std::array<int, 128> rowOf_{}; // each note's row, -1 when hidden
    std::bitset<128> namedNotes_;  // notes the instrument names
    std::array<juce::String, 128> noteNames_;
    std::string namesFor_; // the instrument type the names came from
    bool namedOnly_ = false;
    juce::ScrollBar horizontal_{false}, vertical_{true};
    bool updatingScrollbars_ = false;
    std::size_t grid_ = defaultNoteGrid;
    bool snap_ = true;
    bool linked_ = false;
    bool follow_ = false;
    // The keys whose notes sound at the playhead (lit on the keyboard): a playhead
    // frame repaints only the keys that changed.
    std::bitset<128> soundingKeys() const;
    std::bitset<128> lit_;
    float quantiseStrength_ = 1.0f;
    float swing_ = 0.0f; // a share of a grid step (noteops::QuantiseOptions)
    Tool tool_ = Tool::select;
    core::SamplePosition lastLength_ = 0; // the draw tool's length (0 = a grid step)
    int velocity_ = 100;                  // new notes' velocity
    int rulerHeight_ = 32;
    int scrollbarSize_ = 10;
    std::uint32_t seed_ = 0x51ed5eedu; // humanise and randomise

    // The toolbar: the tools as in the arrange view's corner (select, pencil,
    // erase, split, glue, mute; the same icons, keys 1, 3-7 and cursors).
    IconButton selectTool_, pencilTool_, eraseTool_, splitTool_, glueTool_, muteTool_;
    std::array<IconButton*, 6> toolButtons() noexcept;
    void refreshToolButtons();
    juce::MouseCursor toolCursor() const;
    juce::TextButton snapButton_, quantiseButton_, velocityButton_;
    IconButton link_, followButton_, auditionButton_, ghostButton_, namedButton_;
    juce::TextButton scaleButton_, keyButton_;
    bool scaleHighlight_ = true;
    bool audition_ = true;
    bool ghostTracks_ = false;
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
    int velocityLane_ = -1;                // the lane a velocity drag is in
    bool velocityAbsolute_ = false;        // Alt: every bar to the value
    std::map<core::Id, int> velocityRamp_; // per-note velocities while dragging across bars
    std::vector<core::Id> swept_;          // the mute tool's sweep
    bool muteTo_ = true;                   // what the sweep sets
    juce::Rectangle<int> lasso_;
    core::MidiNote drawNote_; // the note being drawn
    int heldKey_ = -1;
    bool fitPending_ = false;
    core::SamplePosition panScroll_ = 0;
    int panVertical_ = 0;
    core::Id hoverNote_ = core::invalidId;
    bool hoverEdge_ = false;
    int hoverLaneEdge_ = -1;
    int resizingLane_ = -1, resizeStartHeight_ = 0;
    std::optional<core::SamplePosition> pointer_; // the pointer's last clip-relative time in the grid

    // The lanes, top to bottom.
    std::vector<LaneRow> lanes_;
    int focusLane_ = -1; // the lane last clicked (Delete and Ctrl+A act there); -1 the grid

    const engine::LiveRecording* live_ = nullptr;
    bool liveShown_ = false; // the last paint drew live notes
};

} // namespace northlight::ui
