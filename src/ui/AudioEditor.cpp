#include "ui/AudioEditor.h"

#include "core/Command.h"
#include "core/NoteNames.h"
#include "core/TempoMap.h"
#include "engine/Gain.h"
#include "engine/pitch/VocalOps.h"
#include "engine/pitch/VocalRender.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/PaintStats.h"
#include "ui/Shortcuts.h"
#include "ui/audioeditor/CorrectSheet.h"
#include "ui/audioeditor/PitchEditing.h"
#include "ui/audioeditor/PitchGrid.h"
#include "ui/components/DragTip.h"
#include "ui/components/KeyMenu.h"
#include "ui/timeline/TimeGrid.h"
#include "ui/timeline/WaveformPainter.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

namespace pitch = engine::pitch;

namespace
{
using core::isBlackKey;

// The editor's x axis is the clip's source; the model keeps the clip's length and
// fades in timeline samples, which a stretch scales (Model.h).
double sourceEnd(const core::AudioClip& c) noexcept
{
    return static_cast<double>(c.sourceOffset + c.sourceLength());
}
double toSourceSpan(const core::AudioClip& c, double timelineSamples) noexcept
{
    return timelineSamples / c.stretch;
}
double toTimelineSpan(const core::AudioClip& c, double sourceSamples) noexcept
{
    return sourceSamples * c.stretch;
}

juce::String percent(float amount)
{
    return juce::String(static_cast<int>(std::lround(amount * 100.0f))) + "%";
}

juce::String decibels(float db)
{
    if (db <= -60.0f)
        return juce::String::fromUTF8("\xe2\x88\x92\xe2\x88\x9e dB");
    const auto text = juce::String(std::abs(db), 1);
    return (db < -0.05f ? juce::String::fromUTF8("\xe2\x88\x92") : db > 0.05f ? "+" : "") + text + " dB";
}

constexpr float gainMinDb = -48.0f, gainMaxDb = 6.0f;

// A fade handle dragged up or down shapes the curve as the arrange view's does
// (screens/arrange.md "Fades"): 80 px from linear to either end, a 4 px detent at linear.
float fadeCurveFor(float startCurve, int dy) noexcept
{
    constexpr float pixelsPerUnit = 80.0f;
    const float curve = std::clamp(startCurve - static_cast<float>(dy) / pixelsPerUnit, -1.0f, 1.0f);
    return std::abs(curve) * pixelsPerUnit < 4.0f ? 0.0f : curve;
}

juce::String fadeCurveText(float curve)
{
    if (curve == 0.0f)
        return "linear";
    return juce::String(curve > 0.0f ? "fast " : "slow ") +
           juce::String(juce::roundToInt(std::abs(curve) * 100.0f)) + "%";
}
} // namespace

// ----- setup ----------------------------------------------------------------------------------

AudioEditor::AudioEditor(audio::PeakStore& peaks, engine::VocalService& vocal, const Theme& theme)
    : theme_(theme), peaks_(peaks), vocal_(vocal),
      pitchTool_(theme_, Icon::pitchTool, "Pitch: drag a note to a semitone, Alt in cents  1"),
      splitTool_(theme_, Icon::scissors, "Split: click a note to split it, a join to merge two  2"),
      driftTool_(theme_, Icon::driftTool, "Drift: drag a note down to steady its slow drift  3"),
      vibratoTool_(theme_, Icon::vibratoTool, "Vibrato: drag a note down to flatten its vibrato  4"),
      breathTool_(theme_, Icon::breathTool, "Breath: click a breath to mute or unmute it  5"),
      link_(theme_, Icon::link, "Link zoom to the arrange view"),
      followButton_(theme_, Icon::followPlayhead, "Follow the playhead: turn the page as it plays"),
      auditionButton_(theme_, Icon::monitor, "Hear a note when it is clicked")
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    for (auto* bar : {&horizontal_, &vertical_})
    {
        bar->setAutoHide(false);
        bar->addListener(this);
        addAndMakeVisible(*bar);
    }
    modes_.setSegments({"Wave", "Pitch"});
    modes_.setSelected(0, juce::dontSendNotification);
    modes_.onChange = [this](int index) { setMode(index == 1 ? Mode::pitch : Mode::wave); };
    addAndMakeVisible(modes_);
    const auto tools = toolButtons();
    for (size_t i = 0; i < tools.size(); ++i)
    {
        tools[i]->setIconSize(theme_.sizes.iconSmall);
        tools[i]->onClick = [this, i] { setTool(static_cast<Tool>(i)); };
        tools[i]->setToggleState(i == 0, juce::dontSendNotification);
        addChildComponent(*tools[i]);
    }
    for (auto* b : {&correctButton_, &breathsButton_, &keyButton_})
    {
        b->getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
        addChildComponent(*b);
    }
    correctButton_.setButtonText("Correct");
    correctButton_.setTooltip(
        "Correct the notes towards the key (the selected ones, or all), with a preview");
    correctButton_.onClick = [this] { showCorrectSheet(); };
    breathsButton_.setButtonText("Breaths");
    breathsButton_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    breathsButton_.setTooltip("Mute or turn down every breath");
    breathsButton_.onClick = [this] { showBreathMenu(); };
    keyButton_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    keyButton_.setTooltip("The project's key: what Correct aims at and the grid highlights");
    keyButton_.onClick = [this] { showKeyMenu(); };
    link_.setClickingTogglesState(true);
    link_.onClick = [this]
    {
        linked_ = link_.getToggleState();
        repaint();
    };
    addAndMakeVisible(link_);
    followButton_.setClickingTogglesState(true);
    followButton_.onClick = [this] { setFollowPlayhead(followButton_.getToggleState()); };
    addAndMakeVisible(followButton_);
    auditionButton_.setClickingTogglesState(true);
    auditionButton_.setToggleState(audition_, juce::dontSendNotification);
    auditionButton_.onClick = [this] { audition_ = auditionButton_.getToggleState(); };
    addAndMakeVisible(auditionButton_);
    peaks_.addChangeListener(this);
    refreshToolbar();
    startTimerHz(15);
}

AudioEditor::~AudioEditor()
{
    peaks_.removeChangeListener(this);
    horizontal_.removeListener(this);
    vertical_.removeListener(this);
}

void AudioEditor::setClip(const core::Project* project, core::Id trackId, core::Id clipId)
{
    const bool same = project == project_ && trackId == trackId_ && clipId == clipId_;
    project_ = project;
    tempoMap_ = project != nullptr ? core::tempoMapOf(*project) : core::TempoMap();
    trackId_ = trackId;
    clipId_ = clipId;
    const auto* c = clip();
    if (c == nullptr)
    {
        selection_.clear();
        analysis_.reset();
        repaint();
        return;
    }
    const auto selected = selection_.size();
    std::erase_if(selection_, [c](core::Id id) { return c->vocal.findNote(id) == nullptr; });
    if (selection_.size() != selected)
        selectionChanged();
    if (!same)
    {
        if (!selection_.empty())
        {
            selection_.clear();
            selectionChanged();
        }
        flagged_.clear();
        fitPending_ = true;
        pitchFitted_ = false;
        analysis_.reset();
        if (getWidth() > 0)
            fitToClip();
    }
    if (analysisPath_ != c->mediaPath)
        analysis_.reset();
    refreshToolbar();
    updateScrollbars();
    repaint();
}

void AudioEditor::setPlayhead(core::SamplePosition timelinePosition)
{
    const auto* c = clip();
    const auto source = c != nullptr && timelinePosition >= c->start && timelinePosition < c->end()
                            ? c->sourceAt(timelinePosition - c->start)
                            : -1;
    if (source == playhead_)
        return;
    const auto area = contentArea();
    // Following: a playhead past either edge turns the page (it lands an eighth in).
    if (follow_ && source >= 0 && area.getWidth() > 16)
    {
        const float x = xForSource(static_cast<double>(source));
        if (x < static_cast<float>(area.getX()) || x > static_cast<float>(area.getRight() - 8))
        {
            playhead_ = source;
            scroll_ = source - static_cast<core::SamplePosition>(
                                   std::llround(area.getWidth() / 8.0 * samplesPerPixel_));
            updateScrollbars();
            notifyTimeView();
            repaint();
            return;
        }
    }
    const auto repaintAt = [&](core::SamplePosition s)
    {
        if (s >= 0)
            repaint(static_cast<int>(xForSource(static_cast<double>(s))) - 2,
                    area.getY(),
                    5,
                    getHeight() - area.getY());
    };
    repaintAt(playhead_);
    playhead_ = source;
    repaintAt(playhead_);
}

void AudioEditor::setFollowPlayhead(bool on)
{
    follow_ = on;
    followButton_.setToggleState(on, juce::dontSendNotification);
}

void AudioEditor::setSemitoneHeight(float pixels)
{
    semitoneHeight_ = std::clamp(pixels, minSemitoneHeight, maxSemitoneHeight);
    updateScrollbars();
    repaint();
}

void AudioEditor::zoomPitch(float factor, float aroundY)
{
    // The pitch under the pointer stays put.
    const float display = centsForY(aroundY) + pitchOffset();
    semitoneHeight_ = std::clamp(semitoneHeight_ * factor, minSemitoneHeight, maxSemitoneHeight);
    topCents_ =
        std::clamp(display + (aroundY - static_cast<float>(gridArea().getY())) / semitoneHeight_ * 100.0f,
                   1200.0f,
                   12800.0f);
    updateScrollbars();
    repaint();
}

float AudioEditor::pitchOffset() const noexcept
{
    const auto* c = clip();
    return c != nullptr ? static_cast<float>(c->transpose * 100.0) : 0.0f;
}

void AudioEditor::selectionChanged()
{
    if (onSelectionChanged)
        onSelectionChanged();
}

void AudioEditor::stepSelection(int direction, bool extend)
{
    // The next (or previous) note the clip plays after the last selected one (or
    // before the first); with nothing selected, the first or last.
    const auto* cl = clip();
    if (cl == nullptr || cl->vocal.notes.empty())
        return;
    const auto& notes = cl->vocal.notes;
    int at = -1;
    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
        if (isSelected(notes[static_cast<size_t>(i)].id))
        {
            if (direction > 0 || at < 0)
                at = i;
            if (direction < 0)
                break;
        }
    const auto inClip = [&](const core::VocalNote& n)
    { return n.end > cl->sourceOffset && static_cast<double>(n.start) < sourceEnd(*cl); };
    int next = at < 0 ? (direction > 0 ? 0 : static_cast<int>(notes.size()) - 1) : at + direction;
    while (next >= 0 && next < static_cast<int>(notes.size()) && !inClip(notes[static_cast<size_t>(next)]))
        next += direction;
    if (next < 0 || next >= static_cast<int>(notes.size()))
        return;
    const auto& n = notes[static_cast<size_t>(next)];
    if (!extend)
        selection_.clear();
    if (!isSelected(n.id))
        selection_.push_back(n.id);
    // Keep it in view.
    const auto area = gridArea();
    if (xForSource(static_cast<double>(n.start)) < static_cast<float>(area.getX()) ||
        xForSource(static_cast<double>(n.end)) > static_cast<float>(area.getRight()))
    {
        scroll_ = n.start -
                  static_cast<core::SamplePosition>(std::llround(area.getWidth() / 4.0 * samplesPerPixel_));
        updateScrollbars();
        notifyTimeView();
    }
    selectionChanged();
    repaint();
}

void AudioEditor::setZoomLinked(bool linked)
{
    link_.setToggleState(linked, juce::dontSendNotification);
    linked_ = linked;
    repaint();
}

void AudioEditor::setTimeView(double samplesPerPixel, core::SamplePosition absoluteFirstVisible)
{
    const auto* c = clip();
    if (!linked_ || c == nullptr)
        return;
    // The arrange view's zoom is in timeline samples; the editor's in source samples.
    const double source = static_cast<double>(c->sourceOffset) +
                          toSourceSpan(*c, static_cast<double>(absoluteFirstVisible - c->start));
    const double sourcePerPixel = std::max(1.0, toSourceSpan(*c, samplesPerPixel));
    const auto first = static_cast<core::SamplePosition>(std::llround(source));
    if (std::abs(sourcePerPixel - samplesPerPixel_) < 1.0e-9 && first == scroll_)
        return;
    samplesPerPixel_ = sourcePerPixel;
    scroll_ = first;
    updateScrollbars();
    repaint();
}

void AudioEditor::zoomTo(double samplesPerPixel, double secondsIntoClip)
{
    const auto* c = clip();
    if (c == nullptr || project_ == nullptr)
        return;
    samplesPerPixel_ = std::clamp(samplesPerPixel, 1.0, 100000.0);
    scroll_ = c->sourceOffset + static_cast<core::SamplePosition>(
                                    std::llround(toSourceSpan(*c, secondsIntoClip * project_->sampleRate)));
    fitPending_ = false;
    updateScrollbars();
    repaint();
}

void AudioEditor::setMode(Mode mode)
{
    mode_ = mode;
    modes_.setSelected(mode == Mode::pitch ? 1 : 0, juce::dontSendNotification);
    refreshToolbar();
    resized();
    if (mode_ == Mode::pitch)
        timerCallback(); // starts the analysis now
    repaint();
}

void AudioEditor::setTool(Tool tool)
{
    tool_ = tool;
    const auto tools = toolButtons();
    for (size_t i = 0; i < tools.size(); ++i)
        tools[i]->setToggleState(static_cast<Tool>(i) == tool_, juce::dontSendNotification);
    repaint();
}

std::array<IconButton*, 5> AudioEditor::toolButtons() noexcept
{
    return {&pitchTool_, &splitTool_, &driftTool_, &vibratoTool_, &breathTool_};
}

const core::Track* AudioEditor::track() const noexcept
{
    return project_ != nullptr ? project_->findTrack(trackId_) : nullptr;
}

const core::AudioClip* AudioEditor::clip() const noexcept
{
    const auto* t = track();
    return t != nullptr ? t->findClip(clipId_) : nullptr;
}

juce::Colour AudioEditor::trackColour() const
{
    const auto* t = track();
    if (t == nullptr || project_ == nullptr)
        return theme_.colours.accent;
    return theme_.track.forChannel(*project_, t->id);
}

const core::VocalEdit& AudioEditor::shownEdit() const noexcept
{
    static const core::VocalEdit none;
    if (previewing_)
        return preview_;
    const auto* c = clip();
    return c != nullptr ? c->vocal : none;
}

void AudioEditor::emit(core::VocalEdit edit, const std::string& name)
{
    if (!onEdit || clip() == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::make_unique<core::SetVocalEdit>(trackId_, clipId_, std::move(edit), name));
    onEdit(std::move(commands), name);
}

void AudioEditor::ensureEdit()
{
    const auto* c = clip();
    if (mode_ != Mode::pitch || c == nullptr || analysis_ == nullptr || c->vocal.active() ||
        editRequestedFor_ == clipId_)
        return;
    editRequestedFor_ = clipId_;
    emit(analysis_->initialEdit(), "Analyse pitch");
}

void AudioEditor::refreshCurves()
{
    if (analysis_ == nullptr)
    {
        edited_.clear();
        return;
    }
    const auto& edit = shownEdit();
    if (!edited_.empty() && edit == curvesFor_)
        return;
    curvesFor_ = edit;
    edited_ = pitch::editedCents(*analysis_, pitch::shiftCurve(*analysis_, edit));
}

void AudioEditor::timerCallback()
{
    ++ticks_;
    const auto* c = clip();
    if (c == nullptr || project_ == nullptr)
        return;
    if (mode_ == Mode::pitch && analysis_ == nullptr)
    {
        analysis_ = vocal_.analysis(c->mediaPath, project_->sampleRate);
        analysisPath_ = c->mediaPath;
        if (analysis_ != nullptr)
        {
            // Blobs: from 40 dB under the loudest voiced frame.
            float top = -120.0f;
            for (int f = 0; f < analysis_->frames(); ++f)
                if (analysis_->cents[static_cast<size_t>(f)] > 0.0f)
                    top = std::max(top, analysis_->levelDb[static_cast<size_t>(f)]);
            levelTop_ = top > -100.0f ? top : -6.0f;
            levelFloor_ = levelTop_ - 40.0f;
            edited_.clear();
            ensureEdit();
            if (!pitchFitted_)
            {
                fitPitchRange();
                pitchFitted_ = true;
            }
        }
        repaint(); // the progress line
    }
    else if (mode_ == Mode::pitch && !c->vocal.active())
        ensureEdit();
}

void AudioEditor::changeListenerCallback(juce::ChangeBroadcaster*)
{
    repaint(); // peaks arrived
}

// ----- geometry ------------------------------------------------------------------------------

int AudioEditor::leftEdge() const noexcept
{
    return mode_ == Mode::pitch ? keyboardWidth : 0;
}

juce::Rectangle<int> AudioEditor::rulerArea() const noexcept
{
    return {leftEdge(), toolbarHeight, std::max(0, getWidth() - leftEdge()), theme_.sizes.ruler - 8};
}

juce::Rectangle<int> AudioEditor::contentArea() const noexcept
{
    const int top = rulerArea().getBottom();
    const int bar = theme_.sizes.scrollbar;
    const int bottom = getHeight() - bar - (mode_ == Mode::pitch ? laneHeight + infoHeight : 0);
    return {leftEdge(), top, std::max(0, getWidth() - leftEdge() - bar), std::max(0, bottom - top)};
}

juce::Rectangle<int> AudioEditor::gridArea() const noexcept
{
    return contentArea();
}

juce::Rectangle<int> AudioEditor::keysArea() const noexcept
{
    const auto grid = gridArea();
    return {0, grid.getY(), keyboardWidth, grid.getHeight()};
}

juce::Rectangle<int> AudioEditor::laneArea() const noexcept
{
    const auto grid = gridArea();
    return {grid.getX(), grid.getBottom(), grid.getWidth(), laneHeight};
}

juce::Rectangle<int> AudioEditor::infoArea() const noexcept
{
    const auto lane = laneArea();
    return {0, lane.getBottom(), getWidth(), infoHeight};
}

TimelineViewport AudioEditor::timelineView(const core::AudioClip& clip) const noexcept
{
    const auto first = clip.start + static_cast<core::SamplePosition>(std::llround(
                                        static_cast<double>(scroll_ - clip.sourceOffset) * clip.stretch));
    return {leftEdge(), first, samplesPerPixel_ * clip.stretch};
}

float AudioEditor::xForSource(double source) const noexcept
{
    return static_cast<float>(view().exactX(source));
}

core::SamplePosition AudioEditor::sourceForX(float x) const noexcept
{
    return view().sampleAt(static_cast<double>(x));
}

// `cents` are the edit's (the media's pitch); the grid shows the pitch as it plays.
float AudioEditor::yForCents(float cents) const noexcept
{
    return static_cast<float>(gridArea().getY()) +
           (topCents_ - (cents + pitchOffset())) / 100.0f * semitoneHeight_;
}

float AudioEditor::centsForY(float y) const noexcept
{
    return topCents_ - (y - static_cast<float>(gridArea().getY())) / semitoneHeight_ * 100.0f - pitchOffset();
}

void AudioEditor::fitToClip()
{
    const auto* c = clip();
    const int width = contentArea().getWidth();
    if (c == nullptr || width < 32)
        return;
    samplesPerPixel_ = std::max(
        1.0, static_cast<double>(std::max<core::SamplePosition>(1, c->sourceLength())) / (width - 16));
    scroll_ = c->sourceOffset;
    fitPending_ = false;
    updateScrollbars();
}

void AudioEditor::fitPitchRange()
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    const auto& notes =
        c->vocal.active() ? c->vocal.notes : (analysis_ != nullptr ? analysis_->notes : c->vocal.notes);
    float lo = 1.0e9f, hi = -1.0e9f;
    for (const auto& n : notes)
    {
        if (static_cast<double>(n.end) < static_cast<double>(c->sourceOffset) ||
            static_cast<double>(n.start) > sourceEnd(*c))
            continue;
        lo = std::min({lo, n.centre, n.target});
        hi = std::max({hi, n.centre, n.target});
    }
    if (lo > hi)
    {
        lo = 4800.0f;
        hi = 7200.0f;
    }
    lo += pitchOffset();
    hi += pitchOffset();
    const float visible = static_cast<float>(gridArea().getHeight()) / semitoneHeight_ * 100.0f;
    const float middle = 0.5f * (lo + hi);
    topCents_ = std::round((std::max(hi + 300.0f, middle + 0.5f * visible)) / 100.0f) * 100.0f + 50.0f;
    updateScrollbars();
}

void AudioEditor::updateScrollbars()
{
    const auto* c = clip();
    updatingScrollbars_ = true;
    const auto area = contentArea();
    if (c != nullptr)
    {
        const double start = static_cast<double>(c->sourceOffset);
        const double length = static_cast<double>(std::max<core::SamplePosition>(1, c->sourceLength()));
        const double visible = area.getWidth() * samplesPerPixel_;
        const double first = static_cast<double>(scroll_);
        horizontal_.setRangeLimits(std::min(start, first), std::max(start + length, first + visible));
        horizontal_.setCurrentRange(first, visible, juce::dontSendNotification);
    }
    const double rows = static_cast<double>(area.getHeight()) / semitoneHeight_ * 100.0;
    vertical_.setRangeLimits(0.0, 12800.0);
    vertical_.setCurrentRange(12800.0 - topCents_, rows, juce::dontSendNotification);
    vertical_.setVisible(mode_ == Mode::pitch && pitchEditable());
    updatingScrollbars_ = false;
}

void AudioEditor::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (updatingScrollbars_)
        return;
    if (bar == &horizontal_)
    {
        scroll_ = static_cast<core::SamplePosition>(std::llround(newRangeStart));
        notifyTimeView();
    }
    else
        topCents_ = static_cast<float>(12800.0 - newRangeStart);
    repaint();
}

void AudioEditor::notifyTimeView()
{
    const auto* c = clip();
    if (!linked_ || c == nullptr || !onTimeViewChanged)
        return;
    const double first = toTimelineSpan(*c, static_cast<double>(scroll_ - c->sourceOffset));
    onTimeViewChanged(toTimelineSpan(*c, samplesPerPixel_),
                      c->start + static_cast<core::SamplePosition>(std::llround(first)));
}

void AudioEditor::resized()
{
    auto bar = getLocalBounds()
                   .removeFromTop(toolbarHeight)
                   .reduced(theme_.spacing.sm, (toolbarHeight - theme_.sizes.controlCompact) / 2);
    modes_.setBounds(bar.removeFromLeft(std::min(bar.getWidth(), modes_.preferredWidth())));
    bar.removeFromLeft(theme_.spacing.sm);
    const bool pitchMode = mode_ == Mode::pitch;
    for (auto* b : toolButtons())
        b->setVisible(pitchMode);
    correctButton_.setVisible(pitchMode);
    breathsButton_.setVisible(pitchMode);
    keyButton_.setVisible(pitchMode);
    if (pitchMode)
    {
        for (auto* b : toolButtons())
            b->setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
        bar.removeFromLeft(theme_.spacing.sm);
        correctButton_.setBounds(bar.removeFromLeft(72));
        bar.removeFromLeft(theme_.spacing.xs);
        breathsButton_.setBounds(bar.removeFromLeft(84));
        bar.removeFromLeft(theme_.spacing.xs);
        keyButton_.setBounds(bar.removeFromLeft(120));
        bar.removeFromLeft(theme_.spacing.sm);
    }
    link_.setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
    bar.removeFromLeft(theme_.spacing.xs);
    followButton_.setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
    bar.removeFromLeft(theme_.spacing.xs);
    auditionButton_.setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
    auditionButton_.setVisible(pitchMode);

    const int s = theme_.sizes.scrollbar;
    horizontal_.setBounds(leftEdge(), getHeight() - s, std::max(0, getWidth() - leftEdge() - s), s);
    const auto area = contentArea();
    vertical_.setBounds(getWidth() - s, area.getY(), s, area.getHeight());
    if (fitPending_)
        fitToClip();
    updateScrollbars();
}

// ----- hit testing -----------------------------------------------------------------------------

bool AudioEditor::isSelected(core::Id id) const noexcept
{
    return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

float AudioEditor::levelAt(core::SamplePosition source) const noexcept
{
    if (analysis_ == nullptr || analysis_->hop <= 0)
        return 0.0f;
    const auto f = static_cast<size_t>(
        std::clamp<core::SamplePosition>(source / analysis_->hop, 0, analysis_->frames() - 1));
    return std::clamp(
        (analysis_->levelDb[f] - levelFloor_) / std::max(1.0f, levelTop_ - levelFloor_), 0.0f, 1.0f);
}

const core::VocalNote* AudioEditor::noteAt(juce::Point<int> p) const
{
    if (!gridArea().contains(p))
        return nullptr;
    return pitchedit::noteAt(
        shownEdit(),
        sourceForX(static_cast<float>(p.x)),
        static_cast<float>(p.y),
        [this](float cents) { return yForCents(cents); },
        static_cast<float>(semitoneHeight_));
}

const core::VocalSegment* AudioEditor::segmentAt(juce::Point<int> p) const
{
    const auto s = sourceForX(static_cast<float>(p.x));
    for (const auto& seg : shownEdit().segments)
        if (s >= seg.start && s < seg.end)
            return &seg;
    return nullptr;
}

const core::VocalNote* AudioEditor::joinAt(juce::Point<int> p) const
{
    if (!gridArea().contains(p))
        return nullptr;
    return pitchedit::joinAt(
        shownEdit().notes,
        static_cast<float>(p.x),
        static_cast<float>(p.y),
        [this](double source) { return xForSource(source); },
        [this](float cents) { return yForCents(cents); },
        static_cast<float>(semitoneHeight_));
}

// ----- painting -------------------------------------------------------------------------------

juce::String AudioEditor::noteName(float cents)
{
    const int note = static_cast<int>(std::lround(cents / 100.0f));
    return juce::String(core::noteName(note));
}

void AudioEditor::drawTooltip(juce::Graphics& g, const juce::String& text, juce::Point<int> at)
{
    paintDragTip(
        g,
        theme_,
        text,
        at,
        getLocalBounds().withTrimmedRight(theme_.sizes.scrollbar).withTrimmedBottom(theme_.sizes.scrollbar));
}

void AudioEditor::paintToolbarBackground(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.setColour(c.surface0);
    g.fillRect(0, 0, getWidth(), toolbarHeight);
    g.setColour(c.lineSubtle);
    g.fillRect(0, toolbarHeight - 1, getWidth(), 1);

    // Status at the right: the clip, or the analysis while it runs.
    const auto* cl = clip();
    juce::String status = cl != nullptr ? juce::String::fromUTF8(cl->name.c_str()) : juce::String("No clip");
    if (cl != nullptr && mode_ == Mode::pitch && project_ != nullptr && analysis_ == nullptr)
    {
        const auto st = vocal_.status(cl->mediaPath, project_->sampleRate);
        if (st.state == engine::VocalService::State::failed)
            status = "Could not analyse: " + juce::String::fromUTF8(st.error.c_str());
        else
            status = "Analysing pitch " + juce::String(static_cast<int>(st.progress * 100.0)) + "%";
    }
    g.setFont(theme_.caption());
    g.setColour(c.textTertiary);
    auto right = getLocalBounds().removeFromTop(toolbarHeight).reduced(theme_.spacing.sm, 0);
    right.setLeft((auditionButton_.isVisible() ? auditionButton_ : followButton_).getRight() +
                  theme_.spacing.sm);
    g.drawText(status, right, juce::Justification::centredRight, true);
}

void AudioEditor::paintRuler(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto ruler = rulerArea();
    g.setColour(c.surface0);
    g.fillRect(ruler);
    g.setColour(c.lineSubtle);
    g.fillRect(ruler.getX(), ruler.getBottom() - 1, ruler.getWidth(), 1);
    const auto* cl = clip();
    if (cl == nullptr || project_ == nullptr)
        return;
    // The song's bars over the clip's source (a stretched clip's source runs at
    // its stretch against the timeline).
    paintBarsRuler(g, theme_, tempoMap_, timelineView(*cl), ruler.withTrimmedBottom(1));
}

void AudioEditor::paintWave(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto area = contentArea();
    const auto painted = g.getClipBounds();
    const auto* cl = clip();
    if (cl == nullptr || project_ == nullptr)
        return;
    const auto colour = trackColour();
    const auto peaks = peaks_.viewFor(cl->mediaPath,
                                      project_->sampleRate,
                                      sourceForX(static_cast<float>(area.getX())),
                                      sourceForX(static_cast<float>(area.getRight())) + 1,
                                      samplesPerPixel_);
    const double clipStart = static_cast<double>(cl->sourceOffset);
    const double clipEnd = sourceEnd(*cl);
    // Outside the clip the media dims.
    g.setColour(c.laneAlt);
    g.fillRect(area);
    const float x0 = std::max(static_cast<float>(area.getX()), xForSource(clipStart));
    const float x1 = std::min(static_cast<float>(area.getRight()), xForSource(clipEnd));
    g.setColour(c.canvas);
    g.fillRect(juce::Rectangle<float>(
        x0, static_cast<float>(area.getY()), std::max(0.0f, x1 - x0), static_cast<float>(area.getHeight())));
    if (peaks.empty())
    {
        g.setColour(c.textTertiary);
        g.setFont(theme_.caption());
        g.drawText("Reading the waveform", area, juce::Justification::centred);
        return;
    }
    // At the level the clip plays (its gain through its fades); outside the clip
    // the media dims and plays as it is.
    const WaveformStyle style{
        colour.withAlpha(theme_.track.waveformAlpha), colour.withAlpha(0.25f), c.lineSubtle, 0.45f};
    paintWaveform(
        g,
        area,
        std::clamp(peaks.numChannels(), 1, 2),
        painted.getX(),
        painted.getRight(),
        style,
        [&](int ch, int x)
        {
            const auto s0 = sourceForX(static_cast<float>(x));
            const auto s1 = std::max(s0 + 1, sourceForX(static_cast<float>(x + 1)));
            const bool inside = static_cast<double>(s0) >= clipStart && static_cast<double>(s0) < clipEnd;
            const auto r = peaks.lookup(ch, s0, s1);
            const auto offset = static_cast<core::SamplePosition>(
                std::llround(toTimelineSpan(*cl, static_cast<double>(s0 - cl->sourceOffset))));
            return WaveColumn{r.minimum, r.maximum, inside ? clipLevelAt(*cl, offset) : 1.0f, inside};
        });
    // Gain line and fade handles.
    const float gainDb = drag_ == Drag::gain ? dragGainDb_ : engine::gainToDb(cl->gain, gainMinDb);
    const float gy =
        static_cast<float>(area.getY()) +
        static_cast<float>(area.getHeight()) *
            (1.0f - (std::clamp(gainDb, gainMinDb, gainMaxDb) - gainMinDb) / (gainMaxDb - gainMinDb));
    g.setColour(c.automation);
    g.drawLine(x0, gy, x1, gy, 1.5f);
    g.setFont(theme_.caption());
    g.drawText(decibels(gainDb),
               static_cast<int>(x0) + 6,
               static_cast<int>(gy) - 16,
               80,
               14,
               juce::Justification::centredLeft);
    const auto fadeIn = drag_ == Drag::fadeIn ? dragFade_ : cl->fadeIn;
    const auto fadeOut = drag_ == Drag::fadeOut ? dragFade_ : cl->fadeOut;
    const float fi = xForSource(clipStart + toSourceSpan(*cl, static_cast<double>(fadeIn)));
    const float fo = xForSource(clipEnd - toSourceSpan(*cl, static_cast<double>(fadeOut)));
    g.setColour(c.textSecondary);
    // Each fade's curve, from its silent end (at the clip's edge) to full level.
    const auto curve = [&](float silent, float full, float k)
    {
        juce::Path path;
        constexpr int steps = 24;
        for (int i = 0; i <= steps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float x = silent + (full - silent) * t;
            const float y = static_cast<float>(area.getBottom()) -
                            core::fadeShape(t, k) * static_cast<float>(area.getHeight());
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        g.strokePath(path, juce::PathStrokeType(1.0f));
    };
    const float inCurve = drag_ == Drag::fadeIn ? dragCurve_ : cl->fadeInCurve;
    const float outCurve = drag_ == Drag::fadeOut ? dragCurve_ : cl->fadeOutCurve;
    if (fadeIn > 0)
        curve(xForSource(clipStart), fi, core::fadeShapeK(inCurve));
    if (fadeOut > 0)
        curve(xForSource(clipEnd), fo, core::fadeShapeK(outCurve));
    g.setColour(c.textPrimary);
    g.fillRect(fi - 4.0f, static_cast<float>(area.getY()) + 2.0f, 8.0f, 8.0f);
    g.fillRect(fo - 4.0f, static_cast<float>(area.getY()) + 2.0f, 8.0f, 8.0f);
}

void AudioEditor::paintNote(juce::Graphics& g, const core::VocalNote& note, juce::Colour colour)
{
    const auto& c = theme_.colours;
    const auto grid = gridArea();
    const float x0 = xForSource(static_cast<double>(note.start));
    const float x1 = xForSource(static_cast<double>(note.end));
    if (x1 < grid.getX() || x0 > grid.getRight())
        return;
    const float yc = yForCents(note.target);
    if (yc < grid.getY() - semitoneHeight_ * 2 || yc > grid.getBottom() + semitoneHeight_ * 2)
        return;
    // The blob: as thick as the note is loud.
    juce::Path blob;
    const float step = 2.0f;
    std::vector<juce::Point<float>> top;
    for (float x = x0; x <= x1; x += step)
    {
        const float level = levelAt(sourceForX(std::min(x, x1)));
        top.emplace_back(x, yc - (1.5f + level * semitoneHeight_ * 0.75f));
    }
    top.emplace_back(x1, yc - 1.5f);
    blob.startNewSubPath(x0, yc);
    for (const auto& p : top)
        blob.lineTo(p);
    for (auto it = top.rbegin(); it != top.rend(); ++it)
        blob.lineTo(it->x, 2.0f * yc - it->y);
    blob.closeSubPath();
    const bool selected = isSelected(note.id);
    const bool flagged = std::find(flagged_.begin(), flagged_.end(), note.id) != flagged_.end();
    g.setColour(note.muted ? colour.withAlpha(0.15f) : colour.withAlpha(selected ? 0.95f : 0.7f));
    g.fillPath(blob);
    if (selected || flagged || note.id == hoverNote_)
    {
        g.setColour(flagged ? c.warning : selected ? c.textPrimary : c.textSecondary);
        g.strokePath(blob, juce::PathStrokeType(selected || flagged ? 1.5f : 1.0f));
    }
    // The label: the note it now plays and how far from it.
    if (x1 - x0 > 44.0f)
    {
        juce::String label = pitchedit::label(note.target + pitchOffset());
        if (note.muted)
            label += "  muted";
        g.setFont(theme_.caption());
        g.setColour(selected ? c.textPrimary : c.textSecondary);
        g.drawText(label,
                   static_cast<int>(x0) + 2,
                   static_cast<int>(yc - semitoneHeight_ * 1.9f),
                   static_cast<int>(x1 - x0),
                   12,
                   juce::Justification::centredLeft,
                   true);
    }
}

void AudioEditor::paintLane(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto lane = laneArea();
    g.setColour(c.surface0);
    g.fillRect(lane);
    g.setColour(c.lineSubtle);
    g.fillRect(lane.getX(), lane.getY(), lane.getWidth(), 1);
    g.setFont(theme_.caption());
    for (const auto& s : shownEdit().segments)
    {
        if (s.kind == core::VocalSegmentKind::silence && s.isIdentity())
            continue;
        const float x0 = std::max(static_cast<float>(lane.getX()), xForSource(static_cast<double>(s.start)));
        const float x1 =
            std::min(static_cast<float>(lane.getRight()), xForSource(static_cast<double>(s.end)));
        if (x1 <= x0)
            continue;
        auto r = juce::Rectangle<float>(
            x0, static_cast<float>(lane.getY()) + 4.0f, x1 - x0, static_cast<float>(lane.getHeight()) - 8.0f);
        if (s.kind == core::VocalSegmentKind::breath)
        {
            g.setColour(s.muted ? c.surface2 : c.textTertiary.withAlpha(0.55f));
            g.fillRoundedRectangle(r, theme_.radius.sm);
            // Hatching marks a breath.
            g.saveState();
            g.reduceClipRegion(r.toNearestInt());
            g.setColour(c.canvas.withAlpha(0.35f));
            for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 6.0f)
                g.drawLine(x, r.getBottom(), x + r.getHeight(), r.getY(), 1.0f);
            g.restoreState();
            if (s.muted || s.gainDb != 0.0f)
            {
                g.setColour(c.textSecondary);
                g.drawRoundedRectangle(r.reduced(0.5f), theme_.radius.sm, 1.0f);
            }
            if (r.getWidth() > 40.0f)
            {
                g.setColour(s.muted ? c.textTertiary : c.textPrimary);
                g.drawText(s.muted            ? "muted"
                           : s.gainDb != 0.0f ? decibels(s.gainDb)
                                              : juce::String("breath"),
                           r.toNearestInt().reduced(4, 0),
                           juce::Justification::centredLeft,
                           true);
            }
        }
        else
        {
            g.setColour(s.kind == core::VocalSegmentKind::consonant ? c.textTertiary.withAlpha(0.3f)
                                                                    : c.surface1);
            g.fillRoundedRectangle(r.reduced(0.0f, 4.0f), theme_.radius.sm);
            if (!s.isIdentity())
            {
                g.setColour(c.textSecondary);
                g.drawRoundedRectangle(r.reduced(0.5f, 4.5f), theme_.radius.sm, 1.0f);
            }
        }
    }
}

void AudioEditor::paintInfo(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto info = infoArea();
    g.setColour(c.surface0);
    g.fillRect(info);
    g.setColour(c.lineSubtle);
    g.fillRect(info.getX(), info.getY(), info.getWidth(), 1);
    g.setFont(theme_.caption());
    juce::String text;
    const auto& edit = shownEdit();
    if (selection_.size() == 1)
    {
        if (const auto* n = edit.findNote(selection_[0]))
        {
            const float target = n->target + pitchOffset(), centre = n->centre + pitchOffset();
            const float nearest = std::round(target / 100.0f) * 100.0f;
            text << noteName(target) << "  " << pitchedit::signedCents(target - nearest)
                 << "   \xc2\xb7   sung " << noteName(centre) << " "
                 << pitchedit::signedCents(centre - std::round(centre / 100.0f) * 100.0f)
                 << "   \xc2\xb7   drift " << percent(n->drift) << "   \xc2\xb7   vibrato "
                 << percent(n->vibrato) << "   \xc2\xb7   "
                 << (n->muted ? juce::String("muted") : decibels(n->gainDb));
        }
    }
    else if (selection_.size() > 1)
        text << juce::String(static_cast<int>(selection_.size())) << " notes selected";
    else if (!flagged_.empty())
        text << juce::String(static_cast<int>(flagged_.size()))
             << " notes left alone by Correct: more than a semitone from the key (outlined)";
    else
        switch (tool_)
        {
            case Tool::pitch:
                text = "Drag a note up or down (Alt: in cents). Drag a join to set its slide. Double-click "
                       "splits a note (a join: merges).";
                break;
            case Tool::split:
                text = "Click a note to split it; click a join to merge the two notes.";
                break;
            case Tool::drift:
                text = "Drag down to steady a note's slow drift, up to bring it back.";
                break;
            case Tool::vibrato:
                text = "Drag down to flatten the vibrato, up to deepen it.";
                break;
            case Tool::breath:
                text = "Click a breath to mute or unmute it. Right-click for its gain and kind.";
                break;
        }
    g.setColour(c.textSecondary);
    g.drawText(text, info.reduced(theme_.spacing.sm, 0), juce::Justification::centredLeft, true);
}

void AudioEditor::paintPitch(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    const auto grid = gridArea();
    const auto keys = keysArea();
    const auto* cl = clip();
    const core::MusicalKey key = project_ != nullptr ? project_->key : core::MusicalKey{};

    // Rows: one per semitone; black keys darker, out-of-key rows darker still; the keyboard.
    paintPitchGrid(g, theme_, grid, keys, topCents_, static_cast<float>(semitoneHeight_), key);

    // Bar, beat and sub-beat lines, as the arrange view's.
    if (cl != nullptr && project_ != nullptr)
    {
        paintGridLines(g, theme_, tempoMap_, timelineView(*cl), grid);
        // Outside the clip.
        const float x0 = xForSource(static_cast<double>(cl->sourceOffset));
        const float x1 = xForSource(static_cast<double>(cl->sourceOffset + cl->sourceLength()));
        g.setColour(c.canvas.withAlpha(0.6f));
        if (x0 > grid.getX())
            g.fillRect(juce::Rectangle<float>(static_cast<float>(grid.getX()),
                                              static_cast<float>(grid.getY()),
                                              x0 - grid.getX(),
                                              static_cast<float>(grid.getHeight())));
        if (x1 < grid.getRight())
            g.fillRect(juce::Rectangle<float>(x1,
                                              static_cast<float>(grid.getY()),
                                              grid.getRight() - x1,
                                              static_cast<float>(grid.getHeight())));
    }

    if (analysis_ == nullptr)
    {
        g.setColour(c.textSecondary);
        g.setFont(theme_.body());
        juce::String text = "Analysing pitch";
        if (cl != nullptr && project_ != nullptr)
        {
            const auto st = vocal_.status(cl->mediaPath, project_->sampleRate);
            if (st.state == engine::VocalService::State::failed)
                text = "Could not analyse this clip: " + juce::String::fromUTF8(st.error.c_str());
            else
            {
                text << " " << static_cast<int>(st.progress * 100.0) << "%";
                const auto bar =
                    grid.withSizeKeepingCentre(std::min(240, grid.getWidth() - 32), 4).translated(0, 18);
                g.setColour(c.surface2);
                g.fillRect(bar);
                g.setColour(c.accent);
                g.fillRect(bar.withWidth(static_cast<int>(bar.getWidth() * st.progress)));
                g.setColour(c.textSecondary);
            }
        }
        g.drawText(text, grid, juce::Justification::centred);
        return;
    }

    refreshCurves();
    g.saveState();
    g.reduceClipRegion(grid);
    const auto colour = trackColour();
    const auto& edit = shownEdit();
    const auto painted = g.getClipBounds();
    for (const auto& n : edit.notes)
    {
        // A note's blob, outline and label stay within a label's width of it.
        const float left = xForSource(static_cast<double>(n.start));
        const float right = xForSource(static_cast<double>(n.end));
        if (right + 64.0f < static_cast<float>(painted.getX()) ||
            left - 64.0f > static_cast<float>(painted.getRight()))
            continue;
        paintstats::counters().notesDrawn++;
        paintNote(g, n, colour);
    }

    // The curves: as sung, faint; as edited, bright, through the notes.
    const auto& a = *analysis_;
    const int hop = a.hop;
    const int paintedLeft = std::max(grid.getX(), painted.getX());
    const int paintedRight = std::min(grid.getRight(), painted.getRight());
    const int f0 = std::max(0, static_cast<int>(sourceForX(static_cast<float>(paintedLeft)) / hop) - 1);
    const int f1 =
        std::min(a.frames(), static_cast<int>(sourceForX(static_cast<float>(paintedRight)) / hop) + 2);
    const auto strokeCurve = [&](const std::vector<float>& cents, juce::Colour col, float width)
    {
        juce::Path p;
        bool open = false;
        for (int f = f0; f < f1; ++f)
        {
            const float v = f < static_cast<int>(cents.size()) ? cents[static_cast<size_t>(f)] : 0.0f;
            if (v <= 0.0f)
            {
                open = false;
                continue;
            }
            const float x = xForSource(static_cast<double>(f) * hop);
            const float y = yForCents(v);
            if (!open)
                p.startNewSubPath(x, y);
            else
                p.lineTo(x, y);
            open = true;
        }
        g.setColour(col);
        g.strokePath(
            p, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    if (edit.changesSound())
        strokeCurve(a.cents, c.textTertiary.withAlpha(0.7f), 1.0f);
    strokeCurve(edited_, c.textPrimary.withAlpha(0.9f), 1.25f);

    // Transition handles at joins.
    for (size_t i = 0; i + 1 < edit.notes.size(); ++i)
    {
        const auto& l = edit.notes[i];
        const auto& r = edit.notes[i + 1];
        if (l.end != r.start || (l.id != hoverJoin_ && !(drag_ == Drag::transition && dragNote_ == l.id)))
            continue;
        const float x = xForSource(static_cast<double>(l.end));
        const float half =
            static_cast<float>((l.transitionOut + r.transitionIn) / 2) / static_cast<float>(samplesPerPixel_);
        const float y = 0.5f * (yForCents(l.target) + yForCents(r.target));
        g.setColour(c.accent);
        g.fillRect(x - half, y - 1.0f, 2.0f * half, 2.0f);
        g.fillEllipse(x - 4.0f, y - 4.0f, 8.0f, 8.0f);
    }
    g.restoreState();

    if (drag_ == Drag::lasso)
    {
        g.setColour(c.selectionFill);
        g.fillRect(lasso_);
        g.setColour(c.selectionOutline);
        g.drawRect(lasso_, 1);
    }
}

void AudioEditor::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.canvas);
    paintToolbarBackground(g);
    paintRuler(g);
    if (clip() == nullptr)
    {
        g.setColour(c.textTertiary);
        g.setFont(theme_.body());
        g.drawText("Double-click an audio clip to edit it here", contentArea(), juce::Justification::centred);
        return;
    }
    if (mode_ == Mode::wave)
        paintWave(g);
    else
    {
        paintPitch(g);
        paintLane(g);
        paintInfo(g);
    }
    // The playhead.
    if (playhead_ >= 0)
    {
        const float x = xForSource(static_cast<double>(playhead_));
        const auto area = contentArea();
        if (x >= area.getX() && x <= area.getRight())
        {
            g.setColour(c.playhead);
            g.fillRect(x,
                       static_cast<float>(rulerArea().getY()),
                       1.0f,
                       static_cast<float>(area.getBottom() - rulerArea().getY()));
        }
    }
    if (dragTip_.isNotEmpty() && drag_ != Drag::none)
        drawTooltip(g, dragTip_, dragPoint_);
}

// ----- mouse ----------------------------------------------------------------------------------

void AudioEditor::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto* cl = clip();
    if (cl == nullptr || (mode_ == Mode::pitch && !pitchEditable()))
        return;
    dragStart_ = dragPoint_ = e.getPosition();
    dragged_ = false;
    dragTip_.clear();
    if (e.mods.isMiddleButtonDown())
    {
        drag_ = Drag::pan;
        panScroll_ = scroll_;
        panTop_ = topCents_;
        return;
    }
    if (mode_ == Mode::wave)
    {
        const auto area = contentArea();
        if (!area.contains(e.getPosition()))
            return;
        const float fi = xForSource(static_cast<double>(cl->sourceOffset) +
                                    toSourceSpan(*cl, static_cast<double>(cl->fadeIn)));
        const float fo = xForSource(sourceEnd(*cl) - toSourceSpan(*cl, static_cast<double>(cl->fadeOut)));
        const float gainDb = engine::gainToDb(cl->gain, gainMinDb);
        const float gy =
            static_cast<float>(area.getY()) +
            static_cast<float>(area.getHeight()) *
                (1.0f - (std::clamp(gainDb, gainMinDb, gainMaxDb) - gainMinDb) / (gainMaxDb - gainMinDb));
        if (e.y < area.getY() + 14 && std::abs(static_cast<float>(e.x) - fi) < 7.0f)
        {
            drag_ = Drag::fadeIn;
            dragFade_ = cl->fadeIn;
            dragCurve_ = dragCurveStart_ = cl->fadeInCurve;
        }
        else if (e.y < area.getY() + 14 && std::abs(static_cast<float>(e.x) - fo) < 7.0f)
        {
            drag_ = Drag::fadeOut;
            dragFade_ = cl->fadeOut;
            dragCurve_ = dragCurveStart_ = cl->fadeOutCurve;
        }
        else if (std::abs(static_cast<float>(e.y) - gy) < 5.0f)
        {
            drag_ = Drag::gain;
            dragGainDb_ = gainDb;
        }
        return;
    }

    if (analysis_ == nullptr || !cl->vocal.active())
        return;
    const auto& edit = cl->vocal;
    // The lane: segments toggle in any tool; right-click for the menu.
    if (laneArea().contains(e.getPosition()) ||
        (tool_ == Tool::breath && gridArea().contains(e.getPosition())))
    {
        if (const auto* seg = segmentAt(e.getPosition()))
        {
            if (e.mods.isPopupMenu())
                showSegmentMenu(*seg);
            else
            {
                auto next = edit;
                auto* s = next.findSegment(seg->id);
                s->muted = !s->muted;
                emit(std::move(next), s->muted ? "Mute breath" : "Unmute breath");
            }
        }
        return;
    }
    if (!gridArea().contains(e.getPosition()))
        return;
    const auto* join = joinAt(e.getPosition());
    const auto* note = noteAt(e.getPosition());
    if (e.mods.isPopupMenu())
    {
        if (note != nullptr)
        {
            if (!isSelected(note->id))
            {
                selection_ = {note->id};
                selectionChanged();
            }
            showNoteMenu(*note);
        }
        repaint();
        return;
    }
    dragBase_ = edit;
    preview_ = edit;
    switch (tool_)
    {
        case Tool::split:
            if (join != nullptr)
            {
                auto next = edit;
                if (pitch::mergeWithNext(next, *analysis_, join->id))
                    emit(std::move(next), "Merge notes");
            }
            else if (note != nullptr)
            {
                auto next = edit;
                if (pitch::splitNote(next, *analysis_, note->id, sourceForX(static_cast<float>(e.x))))
                    emit(std::move(next), "Split note");
            }
            return;
        case Tool::pitch:
            if (join != nullptr && note == nullptr)
            {
                drag_ = Drag::transition;
                dragNote_ = join->id;
                return;
            }
            [[fallthrough]];
        case Tool::drift:
        case Tool::vibrato:
            if (note == nullptr)
            {
                if (!e.mods.isShiftDown() && !e.mods.isCtrlDown())
                    selection_.clear();
                drag_ = Drag::lasso;
                lasso_ = {};
                repaint();
                return;
            }
            if (e.mods.isCtrlDown())
            {
                if (isSelected(note->id))
                    std::erase(selection_, note->id);
                else
                    selection_.push_back(note->id);
            }
            else if (e.mods.isShiftDown())
            {
                if (!isSelected(note->id))
                    selection_.push_back(note->id);
            }
            else if (!isSelected(note->id))
                selection_ = {note->id};
            dragNote_ = note->id;
            drag_ = tool_ == Tool::pitch ? Drag::pitch : tool_ == Tool::drift ? Drag::drift : Drag::vibrato;
            if (audition_ && onAudition)
                onAudition(cl->start + static_cast<core::SamplePosition>(std::llround(toTimelineSpan(
                                           *cl, static_cast<double>(note->start - cl->sourceOffset)))),
                           cl->start + static_cast<core::SamplePosition>(std::llround(toTimelineSpan(
                                           *cl, static_cast<double>(note->end - cl->sourceOffset)))));
            repaint();
            return;
        case Tool::breath:
            return;
    }
}

void AudioEditor::mouseDrag(const juce::MouseEvent& e)
{
    const auto* cl = clip();
    if (cl == nullptr || drag_ == Drag::none)
        return;
    dragPoint_ = e.getPosition();
    const int dx = e.x - dragStart_.x, dy = e.y - dragStart_.y;
    if (!dragged_ && std::abs(dx) < 3 && std::abs(dy) < 3)
        return;
    dragged_ = true;
    switch (drag_)
    {
        case Drag::pan:
            scroll_ = panScroll_ - static_cast<core::SamplePosition>(std::llround(dx * samplesPerPixel_));
            topCents_ = panTop_ - static_cast<float>(dy) / semitoneHeight_ * 100.0f;
            updateScrollbars();
            break;
        case Drag::gain:
        {
            const auto area = contentArea();
            const float norm = 1.0f - static_cast<float>(e.y - area.getY()) / std::max(1, area.getHeight());
            dragGainDb_ = std::clamp(gainMinDb + norm * (gainMaxDb - gainMinDb), gainMinDb, gainMaxDb);
            if (!e.mods.isAltDown())
                dragGainDb_ = std::round(dragGainDb_ * 2.0f) / 2.0f;
            dragTip_ = decibels(dragGainDb_);
            break;
        }
        case Drag::fadeIn:
        case Drag::fadeOut:
        {
            const auto s = sourceForX(static_cast<float>(e.x));
            const double sourceFade = drag_ == Drag::fadeIn ? static_cast<double>(s - cl->sourceOffset)
                                                            : sourceEnd(*cl) - static_cast<double>(s);
            dragFade_ = std::clamp<core::SamplePosition>(
                static_cast<core::SamplePosition>(std::llround(toTimelineSpan(*cl, sourceFade))),
                0,
                cl->length);
            // Up or down shapes the curve, as the arrange view's handles do.
            dragCurve_ = fadeCurveFor(dragCurveStart_, dy);
            dragTip_ = "Fade " +
                       juce::String(static_cast<double>(dragFade_) * 1000.0 / project_->sampleRate, 0) +
                       " ms  " + fadeCurveText(dragCurve_);
            break;
        }
        case Drag::lasso:
        {
            lasso_ = juce::Rectangle<int>(dragStart_, e.getPosition()).getIntersection(gridArea());
            const auto s0 = sourceForX(static_cast<float>(lasso_.getX()));
            const auto s1 = sourceForX(static_cast<float>(lasso_.getRight()));
            const float c0 = centsForY(static_cast<float>(lasso_.getBottom())),
                        c1 = centsForY(static_cast<float>(lasso_.getY()));
            if (!e.mods.isShiftDown() && !e.mods.isCtrlDown())
                selection_.clear();
            for (const auto& n : cl->vocal.notes)
                if (n.end > s0 && n.start < s1 && n.target + 50.0f > c0 && n.target - 50.0f < c1 &&
                    !isSelected(n.id))
                    selection_.push_back(n.id);
            break;
        }
        case Drag::pitch:
        {
            const auto* dragged = dragBase_.findNote(dragNote_);
            if (dragged == nullptr)
                break;
            // Snapped to the semitones the note plays at (a transposed clip's too).
            const float delta = pitchedit::dragDelta(dragged->target + pitchOffset(),
                                                     -static_cast<float>(dy) / semitoneHeight_ * 100.0f,
                                                     e.mods.isAltDown());
            preview_ = dragBase_;
            for (auto& n : preview_.notes)
                if (isSelected(n.id))
                    n.target = std::clamp(n.target + delta, 2400.0f, 10800.0f);
            previewing_ = true;
            dragTip_ = pitchedit::label(preview_.findNote(dragNote_)->target + pitchOffset());
            break;
        }
        case Drag::transition:
        {
            preview_ = dragBase_;
            auto& notes = preview_.notes;
            for (size_t i = 0; i + 1 < notes.size(); ++i)
                if (notes[i].id == dragNote_)
                {
                    const auto base = (notes[i].transitionOut + notes[i + 1].transitionIn) / 2;
                    auto half = std::max<core::SamplePosition>(
                        0,
                        base + static_cast<core::SamplePosition>(std::abs(dx) * samplesPerPixel_ *
                                                                 (dx >= 0 ? 1 : -1)));
                    notes[i].transitionOut = std::min(half, notes[i].length() * 2 / 5);
                    notes[i + 1].transitionIn = std::min(half, notes[i + 1].length() * 2 / 5);
                    half = notes[i].transitionOut + notes[i + 1].transitionIn;
                    dragTip_ = "Slide " +
                               juce::String(static_cast<double>(half) * 1000.0 / project_->sampleRate, 0) +
                               " ms";
                }
            previewing_ = true;
            break;
        }
        case Drag::drift:
        case Drag::vibrato:
        {
            const auto* dragged = dragBase_.findNote(dragNote_);
            if (dragged == nullptr)
                break;
            const bool drift = drag_ == Drag::drift;
            const float start = drift ? dragged->drift : dragged->vibrato;
            const float amount = std::clamp(
                start - static_cast<float>(dy) / (drift ? 150.0f : 100.0f), 0.0f, drift ? 1.0f : 2.0f);
            const float rounded = std::round(amount * 20.0f) / 20.0f;
            preview_ = dragBase_;
            for (auto& n : preview_.notes)
                if (isSelected(n.id))
                    (drift ? n.drift : n.vibrato) = rounded;
            previewing_ = true;
            dragTip_ = (drift ? "Drift " : "Vibrato ") + percent(rounded);
            break;
        }
        case Drag::none:
            break;
    }
    repaint();
}

void AudioEditor::mouseUp(const juce::MouseEvent&)
{
    const auto* cl = clip();
    const auto finished = drag_;
    drag_ = Drag::none;
    dragTip_.clear();
    if (cl != nullptr && dragged_)
        switch (finished)
        {
            case Drag::gain:
                if (onEdit)
                {
                    std::vector<std::unique_ptr<core::Command>> commands;
                    commands.push_back(std::make_unique<core::SetClipGain>(
                        trackId_, clipId_, engine::dbToGain(dragGainDb_, gainMinDb)));
                    onEdit(std::move(commands), "Set clip gain");
                }
                break;
            case Drag::fadeIn:
            case Drag::fadeOut:
                if (onEdit)
                {
                    std::vector<std::unique_ptr<core::Command>> commands;
                    commands.push_back(std::make_unique<core::SetClipFades>(
                        trackId_,
                        clipId_,
                        finished == Drag::fadeIn ? dragFade_ : cl->fadeIn,
                        finished == Drag::fadeOut ? dragFade_ : cl->fadeOut,
                        finished == Drag::fadeIn ? dragCurve_ : cl->fadeInCurve,
                        finished == Drag::fadeOut ? dragCurve_ : cl->fadeOutCurve));
                    onEdit(std::move(commands), "Set clip fades");
                }
                break;
            case Drag::pitch:
                if (previewing_ && preview_ != cl->vocal)
                    emit(preview_, "Move pitch");
                break;
            case Drag::transition:
                if (previewing_ && preview_ != cl->vocal)
                    emit(preview_, "Set slide");
                break;
            case Drag::drift:
                if (previewing_ && preview_ != cl->vocal)
                    emit(preview_, "Set drift");
                break;
            case Drag::vibrato:
                if (previewing_ && preview_ != cl->vocal)
                    emit(preview_, "Set vibrato");
                break;
            default:
                break;
        }
    previewing_ = false;
    if (mode_ == Mode::pitch)
        selectionChanged(); // clicks and lassos change it
    repaint();
}

void AudioEditor::mouseMove(const juce::MouseEvent& e)
{
    if (mode_ != Mode::pitch || analysis_ == nullptr)
        return;
    const auto* note = noteAt(e.getPosition());
    const auto* join = tool_ == Tool::pitch || tool_ == Tool::split ? joinAt(e.getPosition()) : nullptr;
    const auto hover = note != nullptr ? note->id : core::invalidId;
    const auto hoverJoin = join != nullptr ? join->id : core::invalidId;
    if (hover != hoverNote_ || hoverJoin != hoverJoin_)
    {
        hoverNote_ = hover;
        hoverJoin_ = hoverJoin;
        repaint();
    }
    setMouseCursor(join != nullptr && tool_ == Tool::pitch   ? juce::MouseCursor::LeftRightResizeCursor
                   : note != nullptr && tool_ == Tool::pitch ? juce::MouseCursor::UpDownResizeCursor
                   : tool_ == Tool::split && note != nullptr ? Icons::toolCursor(theme_, Icon::scissors)
                                                             : juce::MouseCursor::NormalCursor);
}

void AudioEditor::mouseExit(const juce::MouseEvent&)
{
    if (hoverNote_ != core::invalidId || hoverJoin_ != core::invalidId)
    {
        hoverNote_ = hoverJoin_ = core::invalidId;
        repaint();
    }
}

void AudioEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    const auto* cl = clip();
    if (cl == nullptr || (mode_ == Mode::pitch && !pitchEditable()))
        return;
    // Wave mode (interaction.md 10.2, double-click resets a control): the gain
    // line goes back to 0 dB, a fade handle removes its fade.
    if (mode_ == Mode::wave)
    {
        const auto area = contentArea();
        if (!area.contains(e.getPosition()) || !onEdit)
            return;
        const float fi = xForSource(static_cast<double>(cl->sourceOffset) +
                                    toSourceSpan(*cl, static_cast<double>(cl->fadeIn)));
        const float fo = xForSource(sourceEnd(*cl) - toSourceSpan(*cl, static_cast<double>(cl->fadeOut)));
        const float gainDb = engine::gainToDb(cl->gain, gainMinDb);
        const float gy =
            static_cast<float>(area.getY()) +
            static_cast<float>(area.getHeight()) *
                (1.0f - (std::clamp(gainDb, gainMinDb, gainMaxDb) - gainMinDb) / (gainMaxDb - gainMinDb));
        std::vector<std::unique_ptr<core::Command>> commands;
        const bool onFadeIn = e.y < area.getY() + 14 && std::abs(static_cast<float>(e.x) - fi) < 7.0f;
        const bool onFadeOut = e.y < area.getY() + 14 && std::abs(static_cast<float>(e.x) - fo) < 7.0f;
        if (onFadeIn || onFadeOut)
        {
            commands.push_back(std::make_unique<core::SetClipFades>(
                trackId_, clipId_, onFadeIn ? 0 : cl->fadeIn, onFadeOut ? 0 : cl->fadeOut));
            onEdit(std::move(commands), "Remove fade");
        }
        else if (std::abs(static_cast<float>(e.y) - gy) < 5.0f && cl->gain != 1.0f)
        {
            commands.push_back(std::make_unique<core::SetClipGain>(trackId_, clipId_, 1.0f));
            onEdit(std::move(commands), "Reset clip gain");
        }
        return;
    }
    // Pitch mode, the pitch tool (pitch-editing.md "Split and merge: double-click
    // splits"): a note splits where it is double-clicked; a join merges its notes.
    if (analysis_ == nullptr || tool_ != Tool::pitch || !cl->vocal.active())
        return;
    auto next = cl->vocal;
    const auto* note = noteAt(e.getPosition());
    if (const auto* join = joinAt(e.getPosition()); join != nullptr && note == nullptr)
    {
        if (pitch::mergeWithNext(next, *analysis_, join->id))
            emit(std::move(next), "Merge notes");
    }
    else if (note != nullptr)
    {
        if (pitch::splitNote(next, *analysis_, note->id, sourceForX(static_cast<float>(e.x))))
            emit(std::move(next), "Split note");
    }
}

void AudioEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Ctrl+Shift+wheel (or Alt+wheel) zooms the pitches around the pointer.
    if (mode_ == Mode::pitch && ((e.mods.isCtrlDown() && e.mods.isShiftDown()) || e.mods.isAltDown()))
    {
        const float delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
        zoomPitch(static_cast<float>(1.0 / TimelineViewport::wheelZoomFactor(delta)),
                  static_cast<float>(e.y));
        return;
    }
    if (e.mods.isCtrlDown())
    {
        const auto zoomed = view().wheelZoomed(wheel.deltaY, e.x, 1.0, 100000.0);
        samplesPerPixel_ = zoomed.samplesPerPixel;
        scroll_ = zoomed.first;
        notifyTimeView();
    }
    else if (e.mods.isShiftDown() || mode_ == Mode::wave)
    {
        scroll_ -= static_cast<core::SamplePosition>(std::llround(wheel.deltaY * 200.0 * samplesPerPixel_));
        notifyTimeView();
    }
    else
        topCents_ = std::clamp(topCents_ + wheel.deltaY * 600.0f, 1200.0f, 12800.0f);
    updateScrollbars();
    repaint();
}

// ----- keys and actions ----------------------------------------------------------------------

void AudioEditor::selectAll()
{
    selection_.clear();
    if (const auto* cl = clip())
        for (const auto& n : cl->vocal.notes)
            if (n.end > cl->sourceOffset && static_cast<double>(n.start) < sourceEnd(*cl))
                selection_.push_back(n.id);
    selectionChanged();
    repaint();
}

void AudioEditor::nudgeSelection(float cents)
{
    const auto* cl = clip();
    if (cl == nullptr || selection_.empty())
        return;
    auto next = cl->vocal;
    for (auto& n : next.notes)
        if (isSelected(n.id))
            n.target = std::clamp(n.target + cents, 2400.0f, 10800.0f);
    emit(std::move(next), "Move pitch");
}

void AudioEditor::toggleMuteSelection()
{
    const auto* cl = clip();
    if (cl == nullptr || selection_.empty())
        return;
    auto next = cl->vocal;
    bool anyUnmuted = false;
    for (const auto& n : next.notes)
        anyUnmuted = anyUnmuted || (isSelected(n.id) && !n.muted);
    for (auto& n : next.notes)
        if (isSelected(n.id))
            n.muted = anyUnmuted;
    emit(std::move(next), anyUnmuted ? "Mute notes" : "Unmute notes");
}

bool AudioEditor::keyPressed(const juce::KeyPress& key)
{
    if (mode_ != Mode::pitch || !pitchEditable())
        return false;
    const auto code = key.getKeyCode();
    const auto mods = key.getModifiers();
    // The keys come from the keyboard map (ui/Shortcuts.h), so a rebinding applies.
    if (const int tool = Shortcuts::matchIndex(Command::audioEditorTool, key); tool >= 0)
    {
        setTool(static_cast<Tool>(tool));
        return true;
    }
    if (Shortcuts::matches(Command::audioEditorNudgePitch, key))
    {
        const float step = mods.isShiftDown() ? 10.0f : 100.0f;
        nudgeSelection(code == juce::KeyPress::upKey ? step : -step);
        return true;
    }
    if (Shortcuts::matches(Command::audioEditorStepSelection, key))
    {
        stepSelection(code == juce::KeyPress::leftKey ? -1 : 1, mods.isShiftDown());
        return true;
    }
    if (Shortcuts::matches(Command::audioEditorMute, key))
    {
        toggleMuteSelection();
        return true;
    }
    if (Shortcuts::matches(Command::audioEditorSelectAll, key))
    {
        selectAll();
        return true;
    }
    if (Shortcuts::matches(Command::audioEditorClearSelection, key))
    {
        selection_.clear();
        selectionChanged();
        repaint();
        return true;
    }
    return false;
}

core::MusicalKey AudioEditor::suggestedKey() const
{
    if (project_ != nullptr && project_->key.isSet())
        return project_->key;
    return detectedKey();
}

core::MusicalKey AudioEditor::detectedKey() const
{
    const auto* cl = clip();
    if (cl == nullptr || !cl->vocal.active())
        return {};
    // Detected at the pitch the clip plays (a transposed clip's notes moved by it).
    auto notes = cl->vocal.notes;
    for (auto& n : notes)
    {
        n.centre += pitchOffset();
        n.target += pitchOffset();
    }
    return pitch::detectKey(notes);
}

void AudioEditor::correctToKey(const pitch::CorrectionOptions& options, const std::string& name)
{
    correctToKey(options, suggestedKey(), name);
}

void AudioEditor::correctToKey(const pitch::CorrectionOptions& options,
                               core::MusicalKey key,
                               const std::string& name)
{
    const auto* cl = clip();
    if (cl == nullptr || project_ == nullptr || !cl->vocal.active() || !onEdit || !pitchEditable())
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    if (!key.isSet())
        key = suggestedKey();
    // The key Correct aims at is the project's (shared with the piano roll's scale).
    if (key.isSet() && !(key == project_->key))
        commands.push_back(std::make_unique<core::SetProjectKey>(key));
    // The selected notes, or every note the clip plays.
    std::vector<core::VocalNote> subset;
    std::vector<std::uint16_t> chords;
    const auto allChords =
        options.chordAware ? pitch::chordTonesUnder(*project_, trackId_, *cl) : std::vector<std::uint16_t>{};
    for (size_t i = 0; i < cl->vocal.notes.size(); ++i)
    {
        const auto& n = cl->vocal.notes[i];
        const bool inClip = n.end > cl->sourceOffset && static_cast<double>(n.start) < sourceEnd(*cl);
        if (inClip && (selection_.empty() || isSelected(n.id)))
        {
            // At the pitch it plays: a transposed clip's key is the heard one.
            auto shifted = n;
            shifted.centre += pitchOffset();
            shifted.target += pitchOffset();
            subset.push_back(shifted);
            chords.push_back(i < allChords.size() ? allChords[i] : 0);
        }
    }
    const auto result = pitch::correctToKey(subset, key, project_->sampleRate, options, chords);
    auto next = cl->vocal;
    for (const auto& corrected : result.notes)
        if (auto* n = next.findNote(corrected.id))
        {
            *n = corrected;
            n->centre -= pitchOffset();
            n->target -= pitchOffset();
        }
    flagged_ = result.flagged;
    commands.push_back(std::make_unique<core::SetVocalEdit>(trackId_, clipId_, std::move(next), name));
    onEdit(std::move(commands), name);
    repaint();
}

void AudioEditor::setBreaths(float gainDb, bool muted)
{
    const auto* cl = clip();
    if (cl == nullptr || !cl->vocal.active() || !pitchEditable())
        return;
    auto next = cl->vocal;
    pitch::setSegments(next, core::VocalSegmentKind::breath, gainDb, muted);
    emit(std::move(next), muted ? "Mute breaths" : gainDb != 0.0f ? "Reduce breaths" : "Restore breaths");
}

bool AudioEditor::pitchEditable() const noexcept
{
    // A stretched or transposed clip stretches its edited voice (MediaCache).
    return clip() != nullptr;
}

void AudioEditor::refreshToolbar()
{
    const bool editable = pitchEditable();
    for (auto* b : toolButtons())
        b->setEnabled(editable);
    correctButton_.setEnabled(editable);
    breathsButton_.setEnabled(editable);
    keyButton_.setButtonText(keymenu::name(project_ != nullptr ? project_->key : core::MusicalKey{}));
}

void AudioEditor::showCorrectSheet()
{
    const auto* cl = clip();
    if (cl == nullptr || !cl->vocal.active())
        return;
    auto sheet = std::make_unique<CorrectSheet>(*this, theme_, !selection_.empty());
    juce::CallOutBox::launchAsynchronously(std::move(sheet), correctButton_.getScreenBounds(), nullptr);
}

void AudioEditor::showBreathMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Mute every breath");
    menu.addItem(2, juce::String::fromUTF8("Turn breaths down 12 dB"));
    menu.addItem(3, juce::String::fromUTF8("Turn breaths down 6 dB"));
    menu.addSeparator();
    menu.addItem(4, "Restore every breath");
    juce::Component::SafePointer<AudioEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(breathsButton_),
                       [safe](int choice)
                       {
                           if (safe == nullptr || choice == 0)
                               return;
                           if (choice == 1)
                               safe->setBreaths(0.0f, true);
                           else if (choice == 2)
                               safe->setBreaths(-12.0f, false);
                           else if (choice == 3)
                               safe->setBreaths(-6.0f, false);
                           else
                               safe->setBreaths(0.0f, false);
                       });
}

void AudioEditor::showKeyMenu()
{
    const auto current = project_ != nullptr ? project_->key : core::MusicalKey{};
    const auto* cl = clip();
    auto menu = keymenu::build(current, cl != nullptr && cl->vocal.active());
    juce::Component::SafePointer<AudioEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(keyButton_),
                       [safe](int choice)
                       {
                           if (safe == nullptr || choice == 0 || !safe->onEdit)
                               return;
                           core::MusicalKey key;
                           if (choice == keymenu::detectItem)
                           {
                               key = safe->detectedKey(); // from the notes as they play
                               if (!key.isSet())
                                   return;
                           }
                           else if (const auto chosen = keymenu::keyFor(choice))
                               key = *chosen;
                           else
                               return;
                           std::vector<std::unique_ptr<core::Command>> commands;
                           commands.push_back(std::make_unique<core::SetProjectKey>(key));
                           safe->onEdit(std::move(commands), "Set key");
                           safe->refreshToolbar();
                           safe->repaint();
                       });
}

void AudioEditor::showNoteMenu(const core::VocalNote& note)
{
    juce::PopupMenu menu;
    const bool many = selection_.size() > 1;
    menu.addItem(1, many ? "Snap the notes to semitones" : "Snap to the nearest semitone");
    menu.addItem(2, many ? "Reset the notes' pitch" : "Reset pitch, drift and vibrato");
    menu.addItem(3, note.muted ? "Unmute" : "Mute");
    menu.addSeparator();
    menu.addItem(4, "Louder (+3 dB)");
    menu.addItem(5,
                 juce::String::fromUTF8("Quieter (\xe2\x88\x92"
                                        "3 dB)"));
    menu.addItem(6, "Gain to 0 dB");
    menu.addSeparator();
    menu.addItem(7, "Merge with the next note", !many);
    menu.addItem(8, "Select all notes");
    juce::Component::SafePointer<AudioEditor> safe(this);
    const auto id = note.id;
    menu.showMenuAsync(
        juce::PopupMenu::Options(),
        [safe, id](int choice)
        {
            if (safe == nullptr || choice == 0)
                return;
            const auto* cl = safe->clip();
            if (cl == nullptr || safe->analysis_ == nullptr)
                return;
            auto next = cl->vocal;
            const auto ids = safe->selection_.empty() ? std::vector<core::Id>{id} : safe->selection_;
            switch (choice)
            {
                case 1:
                    engine::pitch::snapToSemitones(next, ids);
                    safe->emit(std::move(next), "Snap pitch");
                    break;
                case 2:
                    engine::pitch::resetPitch(next, ids);
                    safe->emit(std::move(next), "Reset pitch");
                    break;
                case 3:
                    safe->toggleMuteSelection();
                    break;
                case 4:
                case 5:
                case 6:
                    for (auto& n : next.notes)
                        if (std::find(ids.begin(), ids.end(), n.id) != ids.end())
                            n.gainDb =
                                choice == 6
                                    ? 0.0f
                                    : std::clamp(n.gainDb + (choice == 4 ? 3.0f : -3.0f), -48.0f, 12.0f);
                    safe->emit(std::move(next), "Set note gain");
                    break;
                case 7:
                    if (engine::pitch::mergeWithNext(next, *safe->analysis_, id))
                        safe->emit(std::move(next), "Merge notes");
                    break;
                case 8:
                    safe->selectAll();
                    break;
                default:
                    break;
            }
        });
}

void AudioEditor::showSegmentMenu(const core::VocalSegment& segment)
{
    juce::PopupMenu menu;
    menu.addItem(1, segment.muted ? "Unmute" : "Mute");
    menu.addItem(2, "Turn down 12 dB");
    menu.addItem(3, "Restore");
    menu.addSeparator();
    menu.addItem(10, "It is a breath", true, segment.kind == core::VocalSegmentKind::breath);
    menu.addItem(11, "It is a consonant", true, segment.kind == core::VocalSegmentKind::consonant);
    menu.addItem(12, "It is silence", true, segment.kind == core::VocalSegmentKind::silence);
    juce::Component::SafePointer<AudioEditor> safe(this);
    const auto id = segment.id;
    menu.showMenuAsync(juce::PopupMenu::Options(),
                       [safe, id](int choice)
                       {
                           if (safe == nullptr || choice == 0)
                               return;
                           const auto* cl = safe->clip();
                           if (cl == nullptr)
                               return;
                           auto next = cl->vocal;
                           auto* s = next.findSegment(id);
                           if (s == nullptr)
                               return;
                           if (choice == 1)
                               s->muted = !s->muted;
                           else if (choice == 2)
                           {
                               s->muted = false;
                               s->gainDb = -12.0f;
                           }
                           else if (choice == 3)
                           {
                               s->muted = false;
                               s->gainDb = 0.0f;
                           }
                           else
                               s->kind = choice == 10   ? core::VocalSegmentKind::breath
                                         : choice == 11 ? core::VocalSegmentKind::consonant
                                                        : core::VocalSegmentKind::silence;
                           safe->emit(std::move(next), choice >= 10 ? "Set segment kind" : "Set breath gain");
                       });
}

} // namespace northlight::ui
