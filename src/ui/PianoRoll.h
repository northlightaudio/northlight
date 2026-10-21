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

namespace aural::ui
{

// The piano roll (09-piano-roll.md): a toolbar (tool, snap, quantise, link zoom,
// scale), a 64 px keyboard, a bar/beat ruler, the note grid and an 80 px
// velocity lane. It edits one MIDI clip; the track's other clips show as ghosts.
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
    // The "link zoom to arrange" toggle: the owner feeds setTimeView while linked.
    bool zoomLinked() const noexcept { return linked_; }
    void setTimeView(double samplesPerPixel, core::SamplePosition absoluteFirstVisible);

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
    };

    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
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

    // The toolbar.
    SegmentedControl tools_;
    juce::TextButton snapButton_, quantiseButton_;
    IconButton link_, auditionButton_;
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
};

} // namespace aural::ui
