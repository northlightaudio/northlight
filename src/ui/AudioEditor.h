#pragma once

#include "audio/PeakStore.h"
#include "core/Command.h"
#include "core/Model.h"
#include "core/TempoMap.h"
#include "engine/pitch/Correction.h"
#include "engine/pitch/PitchAnalysis.h"
#include "engine/pitch/VocalService.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/components/SegmentedControl.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace northlight::ui
{

// The audio editor (docs/design/screens/pitch-editing.md, "The audio clip editor"): one audio
// clip, opened by double-clicking it.
//
// Wave mode shows the clip's waveform with its gain line (drag it) and fade handles
// (drag the squares at the top corners).
//
// Pitch mode analyses the clip's media the first time (a progress line shows while
// it runs) and shows its notes as blobs shaped by their loudness on a keyboard grid,
// the pitch curve as sung (faint) and as edited (bright) through them, and the
// breaths, consonants and silence in a lane below. Tools:
//   Pitch    click selects (Shift adds, Ctrl toggles), drag in empty space lassos,
//            drag a note up or down to move it (snaps to semitones; Alt moves in
//            cents), drag the handle at a join to set the transition, double-click
//            snaps a note to the nearest semitone
//   Split    click a note to split it there; click a join to merge the two notes
//   Drift    drag up or down to keep more or less of the slow drift
//   Vibrato  drag up or down to deepen or flatten the vibrato
//   Breath   click a segment in the lane to mute or unmute it
// Keys: 1-5 tools, Up/Down move the selection a semitone (Shift: 10 cents), M mutes,
// Ctrl+A selects all, Esc clears. The toolbar has Correct (to the key, with live
// preview), Breaths (mute or reduce them all) and the key.
//
// Edits go out as SetVocalEdit commands through onEdit; the owner refreshes the view
// from the model (setClip) like the piano roll.
class AudioEditor final : public juce::Component,
                          private juce::Timer,
                          private juce::ScrollBar::Listener,
                          private juce::ChangeListener
{
public:
    AudioEditor(audio::PeakStore& peaks, engine::VocalService& vocal);
    ~AudioEditor() override;

    void setClip(const core::Project* project, core::Id trackId, core::Id clipId);
    core::Id trackId() const noexcept { return trackId_; }
    core::Id clipId() const noexcept { return clipId_; }
    void setPlayhead(core::SamplePosition timelinePosition);
    bool zoomLinked() const noexcept { return linked_; }
    void setZoomLinked(bool linked);
    // The view in the clip's source samples (a stretched clip's source runs at its
    // stretch against the timeline).
    core::SamplePosition playheadSource() const noexcept { return playhead_; }
    double sourceSamplesPerPixel() const noexcept { return samplesPerPixel_; }
    double firstVisibleSource() const noexcept { return scroll_; }
    void setTimeView(double samplesPerPixel, core::SamplePosition absoluteFirstVisible);
    // While linked, a zoom or scroll here: the timeline samples per pixel and the
    // first visible timeline sample, for the owner to set on the arrange view.
    std::function<void(double samplesPerPixel, core::SamplePosition firstVisible)> onTimeViewChanged;
    // Zooms to `samplesPerPixel` with `secondsIntoClip` at the left edge (launch flags).
    void zoomTo(double samplesPerPixel, double secondsIntoClip);

    enum class Mode
    {
        wave,
        pitch,
    };
    void setMode(Mode mode);
    Mode mode() const noexcept { return mode_; }

    enum class Tool
    {
        pitch,
        split,
        drift,
        vibrato,
        breath,
    };
    void setTool(Tool tool);
    Tool tool() const noexcept { return tool_; }

    std::function<void(std::vector<std::unique_ptr<core::Command>>, const std::string& name)> onEdit;
    // The correction sheet previews live inside a gesture; Cancel undoes it.
    std::function<void(const std::string& name)> onGestureBegin;
    std::function<void()> onGestureEnd;
    std::function<void()> onUndo;
    // Hear a stretch of the timeline (a clicked note, while auditioning is on).
    std::function<void(core::SamplePosition from, core::SamplePosition to)> onAudition;

    // The tools' actions, also for the menus and launch flags.
    void correctToKey(const engine::pitch::CorrectionOptions& options,
                      const std::string& name = "Correct pitch");
    void setBreaths(float gainDb, bool muted);
    void selectAll();
    const std::vector<core::Id>& selection() const noexcept { return selection_; }
    // The analysis shown (null until it is ready).
    const engine::pitch::PitchAnalysis* analysis() const noexcept { return analysis_.get(); }

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

    static constexpr int toolbarHeight = 32;
    static constexpr int keyboardWidth = 64;
    static constexpr int rowHeight = 14;  // a semitone
    static constexpr int laneHeight = 24; // the breaths and consonants
    static constexpr int infoHeight = 24;

private:
    enum class Drag
    {
        none,
        lasso,
        pitch,
        transition,
        drift,
        vibrato,
        gain,
        fadeIn,
        fadeOut,
        pan,
    };
    class CorrectSheet;

    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    void notifyTimeView(); // tells a linked owner where the view went
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    const core::AudioClip* clip() const noexcept;
    const core::Track* track() const noexcept;
    juce::Colour trackColour() const;
    // What is drawn and edited: the model's edit, or the drag's preview of it.
    const core::VocalEdit& shownEdit() const noexcept;
    void refreshCurves();
    void ensureEdit();
    void emit(core::VocalEdit edit, const std::string& name);

    juce::Rectangle<int> contentArea() const noexcept; // under the ruler, left of the vertical bar
    juce::Rectangle<int> gridArea() const noexcept;    // pitch mode: the note grid
    juce::Rectangle<int> keysArea() const noexcept;
    juce::Rectangle<int> laneArea() const noexcept;
    juce::Rectangle<int> infoArea() const noexcept;
    juce::Rectangle<int> rulerArea() const noexcept;
    int leftEdge() const noexcept;
    float xForSource(double source) const noexcept;
    core::SamplePosition sourceForX(float x) const noexcept;
    float yForCents(float cents) const noexcept;
    float centsForY(float y) const noexcept;
    void fitToClip();
    void fitPitchRange();
    void updateScrollbars();

    const core::VocalNote* noteAt(juce::Point<int> p) const;
    const core::VocalSegment* segmentAt(juce::Point<int> p) const;
    // A join between two notes near x (the left note), or null.
    const core::VocalNote* joinAt(juce::Point<int> p) const;
    bool isSelected(core::Id id) const noexcept;
    float levelAt(core::SamplePosition source) const noexcept;

    void paintToolbarBackground(juce::Graphics& g);
    void paintRuler(juce::Graphics& g);
    void paintWave(juce::Graphics& g);
    void paintPitch(juce::Graphics& g);
    void paintNote(juce::Graphics& g, const core::VocalNote& note, juce::Colour colour);
    void paintLane(juce::Graphics& g);
    void paintInfo(juce::Graphics& g);
    void drawTooltip(juce::Graphics& g, const juce::String& text, juce::Point<int> at);
    static juce::String noteName(float cents);

    void showNoteMenu(const core::VocalNote& note);
    void showSegmentMenu(const core::VocalSegment& segment);
    void showBreathMenu();
    void showKeyMenu();
    void showCorrectSheet();
    void refreshToolbar();
    void nudgeSelection(float cents);
    void toggleMuteSelection();

    const Theme& theme_ = Theme::dark();
    audio::PeakStore& peaks_;
    engine::VocalService& vocal_;
    const core::Project* project_ = nullptr;
    core::TempoMap tempoMap_; // the project's, rebuilt by setClip (every project change)
    core::Id trackId_ = core::invalidId, clipId_ = core::invalidId;
    Mode mode_ = Mode::wave;
    Tool tool_ = Tool::pitch;
    core::SamplePosition playhead_ = -1; // source samples
    double samplesPerPixel_ = 200.0;
    double scroll_ = 0.0;      // first visible source sample
    float topCents_ = 7900.0f; // the pitch at the top of the grid
    bool linked_ = false;
    bool audition_ = false;
    bool fitPending_ = true;
    bool pitchFitted_ = false;
    core::Id editRequestedFor_ = core::invalidId; // the clip whose edit was created from its analysis

    std::shared_ptr<const engine::pitch::PitchAnalysis> analysis_;
    std::string analysisPath_;
    core::VocalEdit curvesFor_; // the edit the cached curves belong to
    std::vector<float> edited_; // edited pitch per analysis frame
    float levelFloor_ = -60.0f; // levels map from here ...
    float levelTop_ = -6.0f;    // ... to here onto a blob's height

    // The toolbar.
    SegmentedControl modes_{theme_};
    // The pitch tools as icon buttons, as the arrange view's and the piano roll's
    // (keys 1 to 5); shown in pitch mode.
    IconButton pitchTool_, splitTool_, driftTool_, vibratoTool_, breathTool_;
    std::array<IconButton*, 5> toolButtons() noexcept;
    // Pitch edits play only on a clip that is neither stretched nor transposed (the
    // stretch renders from the media file): pitch mode says so and edits nothing.
    bool pitchEditable() const noexcept;
    void paintNotPitchEditable(juce::Graphics& g);
    juce::TextButton correctButton_, breathsButton_, keyButton_;
    IconButton link_, auditionButton_;
    juce::ScrollBar horizontal_{false}, vertical_{true};
    bool updatingScrollbars_ = false;

    std::vector<core::Id> selection_;
    Drag drag_ = Drag::none;
    bool dragged_ = false;
    juce::Point<int> dragStart_, dragPoint_;
    core::Id dragNote_ = core::invalidId;
    core::VocalEdit dragBase_; // the edit when the drag began
    core::VocalEdit preview_;  // the edit the drag would make
    bool previewing_ = false;
    juce::String dragTip_;
    juce::Rectangle<int> lasso_;
    float dragGainDb_ = 0.0f;
    core::SamplePosition dragFade_ = 0;
    double panScroll_ = 0.0;
    float panTop_ = 0.0f;
    core::Id hoverNote_ = core::invalidId;
    core::Id hoverJoin_ = core::invalidId; // the left note of the join under the pointer
    std::vector<core::Id> flagged_;        // notes the last correction left alone (too far off)
    int ticks_ = 0;
};

} // namespace northlight::ui
