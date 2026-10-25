#pragma once

#include "audio/PeakStore.h"
#include "core/Command.h"
#include "core/Model.h"
#include "core/TempoMap.h"
#include "engine/Meters.h"
#include "engine/Recording.h"
#include "ui/AutomationLanes.h"
#include "ui/Icons.h"
#include "ui/PluginMenu.h"
#include "ui/TakeLanes.h"
#include "ui/TempoLane.h"
#include "ui/Theme.h"
#include "ui/TrackHeader.h"
#include "ui/edit/ClipOps.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace northlight::ui
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
//
// Take lanes (TakeLanes, screens/comping.md): a track's take groups expand into lanes
// under its row; Alt+Up / Alt+Down play the next newer / older take in the comp
// segment at the playhead on the selected track, Alt+T shows or hides its lanes.
class ArrangeView final : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropTarget,
                          private juce::ChangeListener,
                          private juce::ScrollBar::Listener
{
public:
    explicit ArrangeView(audio::PeakStore& peaks);
    ~ArrangeView() override;

    // `valuesOnly`: only values moved (gains, pans, parameters): the headers
    // update, nothing is re-laid out or repainted whole.
    void setProject(const core::Project* project, bool valuesOnly = false);
    void setPlayhead(core::SamplePosition position, bool playing, bool recording);
    // The loop/punch region, drawn in the ruler and over the lanes; set by dragging
    // in the ruler.
    void setRegion(core::SamplePosition start, core::SamplePosition end, bool loop, bool punch);
    // Names of the device's active inputs, in callback order, for the track headers.
    void setInputChoices(std::vector<InputChoice> choices);
    void setMidiInputNames(std::vector<std::string> names);
    // Live state per track (same order as the project's tracks): whether the input is
    // being heard, and whether a recording is in progress.
    void setMonitoringStates(const std::vector<bool>& monitoring, bool recording);
    // The recording in progress (screens/arrange.md "Recording clip"): the window's
    // timer has the session bring this up to date each frame, then calls
    // liveRecordingChanged(). Each audio take draws as a growing clip in `record`
    // with its waveform so far, placed where its clip will land; each MIDI take
    // as the clip it will become, with the notes played so far.
    engine::LiveRecording& liveRecording() noexcept { return live_; }
    void liveRecordingChanged();

    std::function<void(core::SamplePosition)> onLocate;
    std::function<void(core::SamplePosition, core::SamplePosition)> onRegionChanged;
    std::function<void(core::Id, int, const std::string&)> onTrackInputChanged; // channel, or a bus name
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
    // Freeze (render and play the render) or unfreeze a track.
    std::function<void(core::Id trackId, bool freeze)> onFreezeTrack;
    // Stretch a clip to the project's tempo (the owner asks for the clip's tempo).
    std::function<void(core::Id trackId, core::Id clipId)> onConformClip;
    // How far a stretched clip's render has got (0..1), for its header tag.
    std::function<double(const core::AudioClip&)> stretchProgress;
    // "Open in Sampler" on an audio clip's menu: a new Sampler instrument plays it.
    std::function<void(core::Id trackId, core::Id clipId)> onOpenInSampler;
    // The playhead position the split key uses (the engine's, not the heard one).
    std::function<core::SamplePosition()> playheadForEdits;
    // Editing keys (the window forwards every key here first). True when handled.
    bool handleEditKey(const juce::KeyPress& key);
    bool snapEnabled() const noexcept { return snap_; }
    bool rippleEnabled() const noexcept { return ripple_; }
    // The snap grid: 0 = adaptive (bar/beat/sub-beat by zoom), 1 = bar, 4 = beat,
    // 8 = 1/8, 16 = 1/16, 32 = 1/32 (interaction.md 10.3).
    void setSnapGrid(int division);
    int snapGrid() const noexcept { return snapGrid_; }
    juce::String snapGridName() const;
    // Zoom: by a factor around the view's centre, to fit the project, or to the selection.
    void zoomBy(double factor);
    void zoomToFit();
    void zoomToSelection();
    void setSnapEnabled(bool on);
    void setRippleEnabled(bool on);

    using ClipRef = edit::ClipRef; // a MIDI clip (Track::midiClips) or an audio clip
    // Double-click on a MIDI clip: open it in the piano roll.
    std::function<void(core::Id trackId, core::Id clipId)> onOpenMidiClip;
    // An audio clip was double-clicked: the audio editor opens on it.
    std::function<void(core::Id trackId, core::Id clipId)> onOpenAudioClip;
    // Track selection (a click on a header or a lane) and clip selection changes.
    std::function<void(core::Id trackId)> onTrackSelected;
    std::function<void()> onSelectionChanged;
    void setSelectedTrack(core::Id trackId);
    core::Id selectedTrack() const noexcept { return selectedTrack_; }
    // The empty state's actions (screens/shell.md).
    std::function<void()> onAddAudioTrack, onImportRequested, onAddBus;
    // The plugins its "New instrument" menus offer (the shell's shared sources).
    void setPluginSources(const PluginSources* sources) noexcept { pluginSources_ = sources; }
    void openAddTrackMenu() { addTrackMenu(); } // as the "+" button does
    // A MIDI track with a new instrument channel playing `type` (empty: the
    // default instrument).
    std::function<void(const std::string& type)> onAddMidiTrack;
    // A MIDI track playing an existing instrument channel (invalidId: nothing).
    std::function<void(core::Id channel)> onAddMidiTrackPlaying;
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
    // The time selection (interaction.md): a range across the selected tracks,
    // empty when end <= start.
    core::SamplePosition timeSelectionStart() const noexcept { return timeSelStart_; }
    core::SamplePosition timeSelectionEnd() const noexcept { return timeSelEnd_; }
    void setFollowPlayhead(bool follow) { followPlayhead_ = follow; }
    // The time view, for views that link their zoom to this one.
    double samplesPerPixel() const noexcept { return samplesPerPixel_; }
    core::SamplePosition scrollPosition() const noexcept { return scroll_; }
    core::SamplePosition visibleSamplesForBenchmark() const noexcept { return visibleSamples(); }
    void setScroll(core::SamplePosition firstVisible);
    // Zoom and scroll together (an editor linked to this view zoomed or scrolled).
    void setTimeView(double samplesPerPixel, core::SamplePosition firstVisible);
    bool followPlayhead() const noexcept { return followPlayhead_; }
    // The row height: size.trackRowMin, trackRow or trackRowLarge.
    void setRowHeight(int height);
    int currentRowHeight() const noexcept { return rowHeight; }
    // The tools (interaction.md), keys 1 to 7: select; range (a drag in a lane
    // selects a time range across the selected tracks); pencil (a drag in a MIDI
    // track's empty space draws a new clip, a click a bar-long one); erase (a
    // click or a sweep deletes clips); split (a click cuts the clip at the
    // pointer, Shift+click every track there, Alt off the grid); glue (a click joins a clip with
    // the next); mute (a click or a sweep mutes or unmutes clips).
    enum class Tool
    {
        select,
        range,
        pencil,
        erase,
        split,
        glue,
        mute,
    };
    static constexpr int toolCount = 7;
    static const char* toolName(Tool tool) noexcept; // "select", "range", ... (launch flags)
    void setTool(Tool tool);
    Tool tool() const noexcept { return tool_; }
    std::function<void(Tool)> onToolChanged;
    // The clipboard: Ctrl+C/X copy or cut the selected clips, Ctrl+V pastes them
    // at the playhead on the selected track (keeping their spacing and track offsets).
    void copySelection();
    void cutSelection();
    void paste();
    // Ctrl+M: mutes the selected clips when any plays, otherwise unmutes them.
    void toggleMuteSelection();
    // Launch flags and captures: the clip menu on the first clip, and the
    // pointer resting at a point in the view (as if the mouse were there).
    void openFirstClipMenu();
    void pointerAt(juce::Point<int> p, juce::ModifierKeys mods = {});
    juce::Point<int> pointOnRow(int row, double bar) const; // bar as the ruler counts, from 1
    bool clipboardEmpty() const noexcept { return clipboard_.empty(); }
    // Renames the selected track inline (F2).
    void renameSelectedTrack();
    void selectAll();
    void clearSelection();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void modifierKeysChanged(const juce::ModifierKeys& mods) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    // A plugin dragged from the browser ("plugin:<type>") lands on a track; an
    // audio file from its Files tab ("file:<path>") goes where a file from
    // Explorer would.
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    std::function<void(core::Id trackId, const std::string& type)> onInsertPlugin;
    // A MIDI track asked for a new instrument: make a channel for it and aim the track there.
    std::function<void(core::Id midiTrack, const std::string& type)> onPlayNewInstrument;
    // Names for insert types (the automation lanes' names).
    void setDisplayName(std::function<std::string(const std::string&)> name)
    {
        displayName_ = name;
        automation_.displayName = std::move(name);
    }
    // The meter reading of a bus (by index), for the FX track headers.
    std::function<rt::MeterBlock::Reading(int busIndex)> busMeter;
    std::function<rt::MeterBlock::Reading()> masterMeter;
    // The MIDI output device's name (empty when none is chosen), for the track menu.
    std::function<std::string()> midiOutputName;
    // A hosted plugin's parameters, for the automation lane menu.
    void setHostedParameters(std::function<const std::vector<engine::ParameterInfo>*(core::Id)> find)
    {
        automation_.hostedParameters = std::move(find);
    }
    // Opens or closes a track's automation lanes under its row (the header's A).
    void showAutomation(core::Id trackId, bool shown);
    // The tempo track under the ruler (TempoLane): its edits arrive here as one
    // command each.
    void setTempoTrackShown(bool shown);
    bool tempoTrackShown() const noexcept { return tempoShown_; }
    std::function<void(core::TempoState, const std::string& name)> onTempoEdit;
    std::function<void(bool shown)> onTempoTrackShownChanged;

private:
    int rulerHeight = 32; // the ruler and, when shown, the tempo track under it: where the lanes start
    int rulerOnly = 32;   // size.ruler: the markers row over the bars row
    static constexpr int tempoLaneHeight = 52;
    bool tempoShown_ = false;
    int headerWidth = 260; // size.trackHeaderWidth; the boundary drags 160..360
    int rowHeight = 56;    // size.trackRow; the corner stepper cycles min/default/large
    // A row being resized: its channel and the height it shows until release.
    core::Id resizeChannel_ = core::invalidId;
    int resizeHeight_ = 0, resizeStartHeight_ = 0;
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
        rowResize,    // a row's bottom edge: that track's height
        draw,         // the pencil drawing a new MIDI clip
        pan,          // the middle button drags the view
        stretch,      // Alt+drag of an audio clip's right edge: time-stretch
        takes,        // a swipe or a comp boundary (TakeLanes)
        erase,        // the erase tool sweeping over clips
        mute,         // the mute tool sweeping over clips
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;

    double sampleRate() const noexcept;
    int xForSample(core::SamplePosition s) const noexcept;
    core::SamplePosition sampleForX(int x) const noexcept;
    core::SamplePosition visibleSamples() const noexcept;
    core::SamplePosition contentLength() const noexcept;
    void setZoom(double samplesPerPixel, int anchorX);
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
                      juce::Rectangle<int> lane,
                      bool recording = false);
    void drawLiveTake(juce::Graphics& g,
                      const core::Track& track,
                      const engine::LiveTake& take,
                      juce::Rectangle<int> lane);
    void fillClipHeader(juce::Graphics& g, juce::Rectangle<int> block, int headerH, juce::Colour colour);
    void drawEditOverlay(juce::Graphics& g);
    void drawGrid(juce::Graphics& g, juce::Rectangle<int> area, bool ruler);
    void drawTooltip(juce::Graphics& g, const juce::String& text, juce::Point<int> at);
    juce::Colour trackColour(const core::Track& track, int row) const;
    juce::Rectangle<int> loopBand() const noexcept; // in the bars row, empty without a region
    bool mediaMissing(const std::string& path);
    void refreshMissingMedia();
    void clipMenu(const ClipRef& ref, juce::Point<int> screen);
    std::vector<const core::Track*> folderChoices(core::Id trackId) const;
    void drawFolderLane(juce::Graphics& g, const core::Track& folder, juce::Rectangle<int> lane);
    void finishStretch();
    // The track's menu; on an audio clip (clipId) it leads with the clip's items.
    void trackMenu(core::Id trackId, juce::Point<int> screen, core::Id clipId = core::invalidId);
    void rulerMenu(juce::Point<int> screen);
    void addTrackMenu();
    // FX tracks (buses flagged fx) show as rows under the tracks.
    // The rows after the tracks: every bus (groups and FX tracks), then the master.
    std::vector<const core::Channel*> busRows() const;
    int rowCount() const noexcept;
    const core::Channel* channelAtRow(int row) const;
    void fxTrackMenu(core::Id busId, juce::Point<int> screen); // a bus's row (group or FX track)
    void masterMenu(juce::Point<int> screen);
    void targetMenu(core::Id trackId, juce::Rectangle<int> screenArea);
    std::function<std::string(const std::string&)> displayName_;
    void addFxMenu(juce::PopupMenu& menu, int firstId) const;
    void addFxTrack(int choice, int firstId, std::vector<core::Id> sendFrom);
    void reorderDrag(core::Id trackId, juce::Point<int> screen, bool released);
    void finishTimeSelection();
    void splitAtSelection();
    juce::String positionText(core::SamplePosition position) const;
    // Snaps a wanted position to the nearest target within 6 px (clip edges on the
    // row, markers, the loop and punch edges, the playhead) or to the grid; names
    // the target in snapTargetName_ (empty for the grid).
    core::SamplePosition snapWithTargets(core::SamplePosition wanted,
                                         int row,
                                         bool relativeTo = false,
                                         core::SamplePosition original = 0);

    // ----- editing helpers --------------------------------------------------------
    int rowAtY(int y) const noexcept;        // track index or -1
    int channelRowAtY(int y) const noexcept; // any row, FX tracks included, or -1
    // Every row's channel and top (content coordinates; tops has one more entry,
    // the content's end), rebuilt when invalidated.
    struct RowLayout
    {
        std::vector<const core::Channel*> channels;
        std::vector<int> tops;
        // What it was built from: an edit that reallocated or resized either list
        // (before the refresh that follows it) rebuilds it rather than leaving
        // pointers into the old storage.
        const core::Track* tracksData = nullptr;
        size_t tracksSize = 0;
        const core::Bus* busesData = nullptr;
        size_t busesSize = 0;
    };
    const RowLayout& rows() const;
    mutable RowLayout rows_;
    mutable bool rowsValid_ = false;
    // Row motion: a new track's row grows from nothing (its scale, 0..1), and a
    // removed one's space closes (the rows from gapRow_ on sit gapHeight_ lower,
    // easing to 0), so the tracks below glide rather than jump.
    std::map<core::Id, float> rowGrowth_;
    int gapRow_ = -1;
    float gapHeight_ = 0.0f;
    // The rows as the last structural refresh showed them, to tell what came and went.
    std::vector<core::Id> shownRowIds_;
    std::vector<int> shownRowHeights_;
    void animateRowChanges();
    // Mouse handling, by where it began and what it drags.
    void mouseDownRuler(const juce::MouseEvent& e);
    void mouseDownLanes(const juce::MouseEvent& e);
    void dragRowResize(const juce::MouseEvent& e);
    void dragRegion(const juce::MouseEvent& e, core::SamplePosition at);
    void dragMoveClips(const juce::MouseEvent& e, core::SamplePosition at);
    void dragTrimLeft(core::SamplePosition at);
    // A new empty MIDI clip on a MIDI track over [start, end), selected.
    void addMidiClip(core::Id trackId, core::SamplePosition start, core::SamplePosition end);
    // The pencil's clip while it draws.
    core::Id drawTrack_ = core::invalidId;
    core::SamplePosition drawAnchor_ = 0, drawStart_ = 0, drawEnd_ = 0;
    int rowHeightOf(int row) const noexcept;             // a row's height (its own, or the default)
    int rowTop(int row) const noexcept;                  // a row's top edge in view coordinates
    int resizeEdgeAt(juce::Point<int> p) const noexcept; // the row whose bottom edge is at p, or -1
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
    // Tells the owner (onSelectionChanged) when the clip selection differs from what
    // it last heard. Every path that changes selection_ ends here.
    void notifySelection();
    // The snap grid at a position (bars, beats and divisions follow the tempo track).
    core::GridStep gridAt(core::SamplePosition at) const noexcept;
    // The grid line at or before / at or after a position.
    core::SamplePosition lineAtOrBefore(core::SamplePosition s) const noexcept;
    core::SamplePosition lineAtOrAfter(core::SamplePosition s) const noexcept;
    core::SamplePosition snapped(core::SamplePosition s) const noexcept;
    const core::AudioClip* findClip(const ClipRef& ref, const core::Track** track = nullptr) const noexcept;
    void emit(std::vector<std::unique_ptr<core::Command>> commands, const std::string& name);
    void finishMove();
    void finishTrim();
    void finishFade(Drag which); // fadeIn or fadeOut (drag_ is already none on release)
    // A fade drag's curve: up bends it fast (bowed up), down slow; a detent at linear.
    float fadeCurveFor(int dy) const noexcept;
    static juce::String fadeCurveText(float curve);
    void finishGain();
    void finishLasso();
    void deleteSelection();
    void duplicateSelection();
    void splitAtPlayhead();
    void joinSelection();
    void crossfadeSelection();
    void nudgeSelection(int direction, bool fine = false);
    void normaliseSelection();
    void removeClips(const std::vector<ClipRef>& clips);
    // The erase, split, glue and mute tools (the select tool's gestures do not
    // apply with them). Returns whether the press was taken.
    bool usesClipTool() const noexcept;
    bool mouseDownWithTool(const juce::MouseEvent& e, const Hit& hit);
    ClipRef clipAt(const Hit& hit) const noexcept;
    juce::MouseCursor toolCursor() const;
    // The split line: where a split-tool click would cut, and the area it paints.
    void updateSplitHover(juce::Point<int> p, juce::ModifierKeys mods);
    juce::Rectangle<int> splitArea() const;
    // On the grid (unless `free`: Alt) where the grid line falls inside the clip.
    core::SamplePosition splitPosition(int x, const Span& clip, bool free = false) const noexcept;
    void sweepOver(const ClipRef& ref);
    void finishSweep(bool erase);
    bool swept(const ClipRef& ref) const noexcept;
    bool shownMuted(const ClipRef& ref, bool muted) const noexcept;
    void drawToolOverlay(juce::Graphics& g);
    void hatch(juce::Graphics& g,
               juce::Rectangle<int> body,
               juce::Rectangle<int> visible,
               juce::Colour colour,
               float dim);
    void hoverAt(juce::Point<int> p, juce::ModifierKeys mods);
    void toolsMenu();
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
    core::TempoMap tempoMap_; // the project's, rebuilt on every setProject
    core::SamplePosition playhead_ = 0;
    bool playing_ = false;
    bool recording_ = false;
    engine::LiveRecording live_;
    bool hadLive_ = false;
    core::SamplePosition regionStart_ = 0, regionEnd_ = 0;
    bool loop_ = false, punch_ = false;
    core::SamplePosition dragAnchor_ = 0;
    std::vector<std::unique_ptr<TrackHeader>> headers_;
    std::vector<std::unique_ptr<TrackHeader>> fxHeaders_; // one per FX track, after the tracks'
    std::vector<InputChoice> inputChoices_;
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
    int pluginDropRow_ = -1;
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
    Tool tool_ = Tool::select;
    juce::String snapTargetName_;
    core::SamplePosition panScroll_ = 0;
    int panVertical_ = 0;
    std::vector<edit::ClipboardItem> clipboard_;
    int lastPlayheadX_ = -1;
    IconButton addTrack_, rowsSmaller_, rowsLarger_;
    // The tools, in the corner beside "+", in key order; when the corner is too
    // narrow for them, one button shows the current tool and opens a menu of all.
    IconButton selectTool_, rangeTool_, pencilTool_, eraseTool_, splitTool_, glueTool_, muteTool_;
    IconButton toolMenu_;
    std::array<IconButton*, toolCount> toolButtons() noexcept;
    void refreshToolButtons();
    // The edit tools' state: the split line under the pointer, and the clips an
    // erase or mute sweep has passed over (with what the mute tool sets them to).
    core::SamplePosition splitAt_ = -1;
    int splitRow_ = -1;      // the hovered clip's row, or -1 with Shift (every row)
    bool splitAll_ = false;  // Shift: every track
    bool splitFree_ = false; // Alt: off the grid
    Span splitClip_;         // the hovered clip's span
    juce::Point<int> splitPoint_;
    std::vector<ClipRef> sweep_;
    std::optional<bool> sweepMute_;
    juce::Point<int> pointer_{-1, -1}; // the last hover position
    AutomationLanes automation_{theme_};
    // A row's automation lanes: their height, and where they are (under the row
    // and its take lanes). MIDI tracks and folders have none.
    int automationHeight(int row) const noexcept;
    juce::Rectangle<int> automationBlock(int row) const noexcept;
    std::vector<int> valuesTops_; // the row layout at the last values refresh
    TempoLane tempoLane_{theme_}; // after theme_, which it keeps a reference to
    std::unique_ptr<juce::TextEditor> markerEditor_;
    core::Id editingMarker_ = core::invalidId;

    // editing state
    std::vector<ClipRef> selection_;
    std::vector<ClipRef> notifiedSelection_; // what onSelectionChanged last reported
    std::vector<ClipRef> pressSelection_;    // the selection when the button went down
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
    float fadeCurvePreview_ = 0.0f, fadeCurveStart_ = 0.0f;                // the dragged fade's curve
    core::SamplePosition stretchLength_ = 0;                               // preview: stretched length
    bool stretchPending_ = false; // a drawn clip's stretch is still rendering: repaint next frame
    float gainPreview_ = 1.0f;    // preview: gain
    juce::Rectangle<int> lasso_;
    bool draggedFar_ = false;
    core::Id selectedMarker_ = core::invalidId;
    core::Id selectedTrack_ = core::invalidId;
    juce::TextButton addAudio_{"Add audio track"}, addInstrument_{"Add MIDI track"},
        import_{"Import audio\xe2\x80\xa6"};
    void layoutEmptyState();
    void selectTrack(core::Id trackId);
    core::Marker dragMarker_;                // the marker being dragged, as it was
    core::SamplePosition markerPreview_ = 0; // its position during the drag
    const PluginSources* pluginSources_ = nullptr;
    // Take lanes and comping (screens/comping.md): lanes under a track's row, swipes, boundaries.
    TakeLanes takes_{theme_, peaks_, *this};
    TakeLanes::Row takeRow(int row) const; // a track row, for TakeLanes
};

} // namespace northlight::ui
