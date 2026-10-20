#pragma once

#include "audio/PeakStore.h"
#include "core/Command.h"
#include "core/Model.h"
#include "engine/Meters.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/TrackHeader.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace aural::ui
{

// The arrange view: tracks as rows, clips with waveforms drawn from the peak
// cache, a bars/beats and time ruler, a playhead that follows playback,
// horizontal zoom (Ctrl+wheel, around the pointer), vertical scroll (wheel) and
// horizontal scroll (Shift+wheel / scrollbar), and audio files dropped onto it
// become tracks at the drop position. Click the ruler or empty track space to
// locate; drag in the ruler to set the loop/punch region.
//
// Editing (Phase 6): click selects a clip (Shift adds, Ctrl toggles), drag on
// empty space lassos, drag a clip body moves the selection (across tracks too),
// drag a clip's edge trims it, drag the handle at a clip's top corner sets its
// fade, Ctrl+drag a body vertically sets its gain. Moves and trims snap to the
// grid (G toggles). Keys: Delete removes (E toggles ripple: later clips close the
// gap), Ctrl+D duplicates, S splits at the playhead, J joins, X crossfades
// overlapping neighbours, Ctrl+Shift+N normalises, Left/Right nudge by a grid
// step, Ctrl+A selects all, Escape clears the selection. Every edit is issued as
// commands through onEdit (one undo step each).
//
// Markers and regions live in the ruler's lower row: M adds a marker at the
// playhead, Shift+M a region from the loop region, comma and full stop jump to
// the previous and next marker, a click on a flag selects it (and locates), a
// drag moves it (snapped), a double-click renames it, Delete removes it.
class ArrangeView final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener
{
public:
    explicit ArrangeView(audio::PeakStore& peaks);
    ~ArrangeView() override;

    void setProject(const core::Project* project);
    void setPlayhead(core::SamplePosition position, bool playing, bool recording);
    // The loop/punch region, drawn in the ruler and over the lanes; set by dragging
    // in the ruler.
    void setRegion(core::SamplePosition start, core::SamplePosition end, bool loop, bool punch);
    // Names of the device's active inputs, in callback order, for the track headers.
    void setInputNames(std::vector<std::string> names);
    void setMidiInputNames(std::vector<std::string> names);
    // Live state per track (same order as the project's tracks): whether the input is
    // being heard, and whether a recording is in progress.
    void setMonitoringStates(const std::vector<bool>& monitoring, bool recording);

    std::function<void(core::SamplePosition)> onLocate;
    std::function<void(core::SamplePosition, core::SamplePosition)> onRegionChanged;
    std::function<void(core::Id, int)> onTrackInputChanged;
    std::function<void(core::Id, const std::string&)> onTrackMidiInputChanged;
    std::function<void(core::Id, bool)> onTrackArmChanged;
    std::function<void(core::Id, core::MonitorMode)> onTrackMonitorChanged;
    // Files dropped on the view, with the timeline position under the pointer.
    std::function<void(const std::vector<juce::File>&, core::SamplePosition)> onImportFiles;
    // Extensions the drop target accepts, e.g. "*.wav;*.flac".
    void setAcceptedExtensions(const juce::String& wildcards);

    // ----- editing ----------------------------------------------------------------
    // An edit: the commands that make it, as one undo step with this name.
    std::function<void(std::vector<std::unique_ptr<core::Command>>, const std::string&)> onEdit;
    std::function<void(core::Id trackId, core::Id clipId)> onNormalise;
    // The playhead position the split key uses (the engine's, not the heard one).
    std::function<core::SamplePosition()> playheadForEdits;
    // Editing keys (the window forwards every key here first). True when handled.
    bool handleEditKey(const juce::KeyPress& key);
    bool snapEnabled() const noexcept { return snap_; }
    bool rippleEnabled() const noexcept { return ripple_; }
    // The snap grid: 0 = adaptive (bar/beat/sub-beat by zoom), 1 = bar, 4 = beat,
    // 8 = 1/8, 16 = 1/16, 32 = 1/32 (10-interaction.md 10.3).
    void setSnapGrid(int division);
    int snapGrid() const noexcept { return snapGrid_; }
    juce::String snapGridName() const;
    // Zoom: by a factor around the view's centre, to fit the project, or to the selection.
    void zoomBy(double factor);
    void zoomToFit();
    void zoomToSelection();
    void setSnapEnabled(bool on);
    void setRippleEnabled(bool on);

    struct ClipRef
    {
        core::Id track = core::invalidId;
        core::Id clip = core::invalidId;
        bool midi = false; // a MIDI clip (Track::midiClips) rather than an audio clip
        bool operator==(const ClipRef&) const = default;
    };
    // Double-click on a MIDI clip: open it in the piano roll.
    std::function<void(core::Id trackId, core::Id clipId)> onOpenMidiClip;
    // Track selection (a click on a header or a lane) and clip selection changes.
    std::function<void(core::Id trackId)> onTrackSelected;
    std::function<void()> onSelectionChanged;
    void setSelectedTrack(core::Id trackId);
    core::Id selectedTrack() const noexcept { return selectedTrack_; }
    // The empty state's actions (03-shell.md).
    std::function<void()> onAddAudioTrack, onAddInstrumentTrack, onImportRequested, onAddBus;
    const std::vector<ClipRef>& selection() const noexcept { return selection_; }
    // Single commands from the headers (mute, solo, gain, pan, name, colour, output,
    // reorder, delete) and their gestures.
    std::function<void(std::unique_ptr<core::Command>)> onCommand;
    std::function<void(const std::string& name)> onGestureBegin;
    std::function<void()> onGestureEnd;
    // The meter reading of a track (by index) for the headers; pushMeters() polls
    // it once per UI frame.
    std::function<rt::MeterBlock::Reading(int trackIndex)> trackMeter;
    void pushMeters();
    // The time selection (10-interaction.md): a range across the selected tracks,
    // empty when end <= start.
    core::SamplePosition timeSelectionStart() const noexcept { return timeSelStart_; }
    core::SamplePosition timeSelectionEnd() const noexcept { return timeSelEnd_; }
    void setFollowPlayhead(bool follow) { followPlayhead_ = follow; }
    bool followPlayhead() const noexcept { return followPlayhead_; }
    // The row height: size.trackRowMin, trackRow or trackRowLarge.
    void setRowHeight(int height);
    int currentRowHeight() const noexcept { return rowHeight; }
    void selectAll();
    void clearSelection();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    int rulerHeight = 32;  // size.ruler: the markers row over the bars row
    int headerWidth = 220; // size.trackHeaderWidth; the boundary drags 160..360
    int rowHeight = 56;    // size.trackRow; the corner stepper cycles min/default/large
    int scrollbarHeight = 10;
    int scrollbarWidth = 10;
    static constexpr int markersRowHeight = 14;
    static constexpr int edgeZone = 6;   // px at a clip's ends that trim
    static constexpr int fadeHandle = 8; // px square at a clip's top corners
    static constexpr int gridMinPixels = 16;
    static constexpr int loopHandle = 8; // px at the loop band's ends

    enum class Drag
    {
        none,
        region,
        lasso,
        move,
        trimLeft,
        trimRight,
        fadeIn,
        fadeOut,
        gain,
        marker,
        zoom,         // a vertical drag in the ruler
        loopMove,     // the loop band dragged along
        loopStart,    // its left handle
        loopEnd,      // its right handle
        timeSelect,   // Shift+drag in the bars row: a time range
        headerResize, // the boundary between headers and lanes
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;

    double sampleRate() const noexcept;
    int xForSample(core::SamplePosition s) const noexcept;
    core::SamplePosition sampleForX(int x) const noexcept;
    core::SamplePosition visibleSamples() const noexcept;
    core::SamplePosition contentLength() const noexcept;
    void setZoom(double samplesPerPixel, int anchorX);
    void setScroll(core::SamplePosition firstVisible);
    void setVerticalScroll(int firstRowPixel);
    void updateScrollbar();
    void rebuildHeaders();
    void layoutHeaders();
    int contentHeight() const noexcept;
    int lanesHeight() const noexcept { return getHeight() - rulerHeight - scrollbarHeight; }
    void drawRuler(juce::Graphics& g, juce::Rectangle<int> area);
    void drawClip(juce::Graphics& g,
                  const core::Track& track,
                  const core::AudioClip& clip,
                  juce::Rectangle<int> lane);
    void drawMidiClip(juce::Graphics& g,
                      const core::Track& track,
                      const core::MidiClip& model,
                      juce::Rectangle<int> lane);
    void drawEditOverlay(juce::Graphics& g);
    void drawGrid(juce::Graphics& g, juce::Rectangle<int> area, bool ruler);
    void drawTooltip(juce::Graphics& g, const juce::String& text, juce::Point<int> at);
    juce::Colour trackColour(const core::Track& track, int row) const;
    juce::Rectangle<int> loopBand() const noexcept; // in the bars row, empty without a region
    bool mediaMissing(const std::string& path);
    void refreshMissingMedia();
    void trackMenu(core::Id trackId, juce::Point<int> screen);
    void rulerMenu(juce::Point<int> screen);
    void addTrackMenu();
    void reorderDrag(core::Id trackId, juce::Point<int> screen, bool released);
    void finishTimeSelection();
    void splitAtSelection();
    juce::String positionText(core::SamplePosition position) const;

    // ----- editing helpers --------------------------------------------------------
    int rowAtY(int y) const noexcept; // track index or -1
    juce::Rectangle<int> rowBounds(int row) const noexcept;
    juce::Rectangle<int> clipBounds(const core::AudioClip& clip, int row) const noexcept;
    juce::Rectangle<int>
    spanBounds(core::SamplePosition start, core::SamplePosition length, int row) const noexcept;
    struct Hit
    {
        int row = -1;
        const core::Track* track = nullptr;
        const core::AudioClip* clip = nullptr;
        const core::MidiClip* midiClip = nullptr;
        Drag zone = Drag::none; // move, trimLeft, trimRight, fadeIn, fadeOut
    };
    // A selected clip's span (audio or MIDI), if it still exists.
    struct Span
    {
        core::SamplePosition start = 0, length = 0;
        core::SamplePosition end() const noexcept { return start + length; }
    };
    std::optional<Span> spanOf(const ClipRef& ref) const noexcept;
    const core::MidiClip* findMidiClip(const ClipRef& ref) const noexcept;
    bool isSelected(const ClipRef& ref) const noexcept;
    Hit hitTest(juce::Point<int> p) const noexcept;
    bool isSelected(core::Id track, core::Id clip) const noexcept;
    void select(const ClipRef& ref, bool add);
    core::SamplePosition grid() const noexcept;
    core::SamplePosition snapped(core::SamplePosition s) const noexcept;
    const core::AudioClip* findClip(const ClipRef& ref, const core::Track** track = nullptr) const noexcept;
    void emit(std::vector<std::unique_ptr<core::Command>> commands, const std::string& name);
    void finishMove();
    void finishTrim();
    void finishFade();
    void finishGain();
    void finishLasso();
    void deleteSelection();
    void duplicateSelection();
    void splitAtPlayhead();
    void joinSelection();
    void crossfadeSelection();
    void nudgeSelection(int direction);
    void normaliseSelection();
    // Markers.
    const core::Marker* markerAtRulerPoint(juce::Point<int> p) const noexcept;
    juce::Rectangle<int> markerFlagBounds(const core::Marker& marker) const noexcept;
    void drawMarkers(juce::Graphics& g, juce::Rectangle<int> timeRow);
    void addMarkerAtPlayhead(bool region);
    void jumpToMarker(int direction);
    void renameMarker(core::Id markerId);
    void finishMarkerDrag();
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    juce::MouseCursor cursorFor(Drag zone) const noexcept;

    const Theme& theme_ = Theme::dark();
    audio::PeakStore& peaks_;
    const core::Project* project_ = nullptr;
    core::SamplePosition playhead_ = 0;
    bool playing_ = false;
    bool recording_ = false;
    core::SamplePosition regionStart_ = 0, regionEnd_ = 0;
    bool loop_ = false, punch_ = false;
    core::SamplePosition dragAnchor_ = 0;
    std::vector<std::unique_ptr<TrackHeader>> headers_;
    std::vector<std::string> inputNames_;
    std::vector<std::string> midiInputNames_;
    double samplesPerPixel_ = 48000.0 / 40.0; // 40 px per second at 48 kHz
    core::SamplePosition scroll_ = 0;         // first visible sample
    juce::ScrollBar scrollbar_{false};
    juce::ScrollBar verticalScrollbar_{true};
    int verticalScroll_ = 0; // pixels
    bool updatingScrollbar_ = false;
    juce::StringArray acceptedExtensions_;
    bool dragOver_ = false;
    juce::Point<int> dropPoint_;
    juce::StringArray dropFiles_;
    bool followPlayhead_ = true;
    int hoverRow_ = -1;
    core::Id hoverMarker_ = core::invalidId;
    ClipRef hoverClip_;
    juce::Point<int> dragPoint_;
    double zoomStartSpp_ = 1.0;
    core::SamplePosition loopOriginalStart_ = 0, loopOriginalEnd_ = 0;
    core::SamplePosition timeSelStart_ = 0, timeSelEnd_ = 0;
    std::vector<core::Id> timeSelTracks_;
    std::unordered_set<std::string> missingMedia_;
    core::Id reorderFrom_ = core::invalidId;
    int reorderTo_ = -1;
    double lastMeterMs_ = 0.0;
    IconButton addTrack_, rowsSmaller_, rowsLarger_;
    std::unique_ptr<juce::TextEditor> markerEditor_;
    core::Id editingMarker_ = core::invalidId;

    // editing state
    std::vector<ClipRef> selection_;
    bool snap_ = true;
    int snapGrid_ = 0;
    bool ripple_ = false;
    Drag drag_ = Drag::none;
    ClipRef dragClip_;             // the clip under the pointer when the drag began
    core::AudioClip dragOriginal_; // its state then
    juce::Point<int> dragStart_;   // pointer at mouse down
    core::SamplePosition dragStartSample_ = 0;
    int dragStartRow_ = -1;
    core::SamplePosition moveDelta_ = 0; // preview: move
    int rowDelta_ = 0;
    core::SamplePosition trimStart_ = 0, trimLength_ = 0, trimOffset_ = 0; // preview: trim
    core::SamplePosition fadePreview_ = 0;                                 // preview: fade length
    float gainPreview_ = 1.0f;                                             // preview: gain
    juce::Rectangle<int> lasso_;
    bool draggedFar_ = false;
    core::Id selectedMarker_ = core::invalidId;
    core::Id selectedTrack_ = core::invalidId;
    juce::TextButton addAudio_{"Add audio track"}, addInstrument_{"Add instrument track"},
        import_{"Import audio\xe2\x80\xa6"};
    void layoutEmptyState();
    void selectTrack(core::Id trackId);
    core::Marker dragMarker_;                // the marker being dragged, as it was
    core::SamplePosition markerPreview_ = 0; // its position during the drag
};

} // namespace aural::ui
