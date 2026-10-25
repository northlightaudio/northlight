#include "ui/AudioEditor.h"

#include "core/NoteNames.h"
#include "core/TempoMap.h"
#include "engine/Gain.h"
#include "engine/pitch/VocalOps.h"
#include "engine/pitch/VocalRender.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/PaintStats.h"

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

juce::String signedCents(float cents)
{
    const int c = static_cast<int>(std::lround(cents));
    return (c > 0   ? "+"
            : c < 0 ? juce::String::fromUTF8("\xe2\x88\x92")
                    : "") +
           juce::String(std::abs(c)) + " ct";
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
} // namespace

// ----- the correction sheet -------------------------------------------------------------------

class AudioEditor::CorrectSheet final : public juce::Component
{
public:
    CorrectSheet(AudioEditor& owner, const Theme& theme, bool selectionOnly) : owner_(&owner), theme_(theme)
    {
        const auto slider =
            [this](juce::Slider& s, double lo, double hi, double value, const juce::String& suffix)
        {
            s.setSliderStyle(juce::Slider::LinearBar);
            s.setRange(lo, hi, 1.0);
            s.setValue(value, juce::dontSendNotification);
            s.setTextValueSuffix(suffix);
            s.onValueChange = [this] { preview(); };
            addAndMakeVisible(s);
        };
        slider(strength_, 0, 100, 85, "%");
        slider(tolerance_, 0, 50, 10, " ct");
        slider(drift_, 0, 100, 60, "%");
        slider(vibrato_, 0, 200, 100, "%");
        chords_.setButtonText("Prefer chord tones");
        chords_.onClick = [this] { preview(); };
        addAndMakeVisible(chords_);
        apply_.setButtonText("Apply");
        apply_.onClick = [this] { close(false); };
        cancel_.setButtonText("Cancel");
        cancel_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
        cancel_.onClick = [this] { close(true); };
        addAndMakeVisible(apply_);
        addAndMakeVisible(cancel_);
        title_ = selectionOnly ? "Correct the selected notes" : "Correct every note";
        setSize(300, 236);
        if (owner_->onGestureBegin)
            owner_->onGestureBegin("Correct pitch");
        preview();
    }

    ~CorrectSheet() override
    {
        // Dismissed by a click outside: what is heard is kept.
        if (!finished_ && owner_ != nullptr && owner_->onGestureEnd)
            owner_->onGestureEnd();
    }

    void paint(juce::Graphics& g) override
    {
        const auto& c = theme_.colours;
        g.setColour(c.textPrimary);
        g.setFont(theme_.label());
        g.drawText(title_, 12, 8, getWidth() - 24, 20, juce::Justification::centredLeft);
        g.setFont(theme_.caption());
        g.setColour(c.textSecondary);
        const char* labels[] = {"Strength", "Tolerance", "Drift kept", "Vibrato"};
        for (int i = 0; i < 4; ++i)
            g.drawText(labels[i], 12, 36 + i * 30, 80, 24, juce::Justification::centredLeft);
        g.setColour(c.textTertiary);
        g.drawText(keyText_, 12, getHeight() - 72, getWidth() - 24, 20, juce::Justification::centredLeft);
    }

    void resized() override
    {
        juce::Slider* sliders[] = {&strength_, &tolerance_, &drift_, &vibrato_};
        for (int i = 0; i < 4; ++i)
            sliders[i]->setBounds(96, 36 + i * 30, getWidth() - 108, 24);
        chords_.setBounds(10, 156, getWidth() - 20, 22);
        apply_.setBounds(getWidth() - 88, getHeight() - 40, 76, 28);
        cancel_.setBounds(getWidth() - 172, getHeight() - 40, 76, 28);
    }

private:
    pitch::CorrectionOptions options() const
    {
        pitch::CorrectionOptions o;
        o.strength = static_cast<float>(strength_.getValue() / 100.0);
        o.toleranceCents = static_cast<float>(tolerance_.getValue());
        o.drift = static_cast<float>(drift_.getValue() / 100.0);
        o.vibrato = static_cast<float>(vibrato_.getValue() / 100.0);
        o.chordAware = chords_.getToggleState();
        return o;
    }
    void preview()
    {
        if (owner_ == nullptr)
            return;
        owner_->correctToKey(options());
        if (owner_->project_ != nullptr && owner_->project_->key.isSet())
            keyText_ = juce::String("Key: ") + core::pitchClassName(owner_->project_->key.root) + " " +
                       core::scaleName(owner_->project_->key.scale);
        if (!owner_->flagged_.empty())
            keyText_ +=
                "  \xc2\xb7  " + juce::String(static_cast<int>(owner_->flagged_.size())) + " left alone";
        repaint();
    }
    void close(bool undo)
    {
        finished_ = true;
        if (owner_ != nullptr)
        {
            if (owner_->onGestureEnd)
                owner_->onGestureEnd();
            if (undo && owner_->onUndo)
                owner_->onUndo();
        }
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    juce::Component::SafePointer<AudioEditor> owner_;
    const Theme& theme_;
    juce::Slider strength_, tolerance_, drift_, vibrato_;
    juce::ToggleButton chords_;
    juce::TextButton apply_, cancel_;
    juce::String title_, keyText_;
    bool finished_ = false;
};

// ----- setup ----------------------------------------------------------------------------------

AudioEditor::AudioEditor(audio::PeakStore& peaks, engine::VocalService& vocal)
    : peaks_(peaks), vocal_(vocal),
      pitchTool_(theme_, Icon::pitchTool, "Pitch: drag a note to a semitone, Alt in cents  1"),
      splitTool_(theme_, Icon::scissors, "Split: click a note to split it, a join to merge two  2"),
      driftTool_(theme_, Icon::driftTool, "Drift: drag a note down to steady its slow drift  3"),
      vibratoTool_(theme_, Icon::vibratoTool, "Vibrato: drag a note down to flatten its vibrato  4"),
      breathTool_(theme_, Icon::breathTool, "Breath: click a breath to mute or unmute it  5"),
      link_(theme_, Icon::link, "Link zoom to the arrange view"),
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
    auditionButton_.setClickingTogglesState(true);
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
    std::erase_if(selection_, [c](core::Id id) { return c->vocal.findNote(id) == nullptr; });
    if (!same)
    {
        selection_.clear();
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
    if (std::abs(sourcePerPixel - samplesPerPixel_) < 1.0e-9 && std::abs(source - scroll_) < 0.5)
        return;
    samplesPerPixel_ = sourcePerPixel;
    scroll_ = source;
    updateScrollbars();
    repaint();
}

void AudioEditor::zoomTo(double samplesPerPixel, double secondsIntoClip)
{
    const auto* c = clip();
    if (c == nullptr || project_ == nullptr)
        return;
    samplesPerPixel_ = std::clamp(samplesPerPixel, 1.0, 100000.0);
    scroll_ = static_cast<double>(c->sourceOffset) + toSourceSpan(*c, secondsIntoClip * project_->sampleRate);
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
    return theme_.track.forTrack(t->colour, project_->trackIndex(t->id));
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

float AudioEditor::xForSource(double source) const noexcept
{
    return static_cast<float>(leftEdge() + (source - scroll_) / samplesPerPixel_);
}

core::SamplePosition AudioEditor::sourceForX(float x) const noexcept
{
    return static_cast<core::SamplePosition>(std::llround(scroll_ + (x - leftEdge()) * samplesPerPixel_));
}

float AudioEditor::yForCents(float cents) const noexcept
{
    return static_cast<float>(gridArea().getY()) + (topCents_ - cents) / 100.0f * rowHeight;
}

float AudioEditor::centsForY(float y) const noexcept
{
    return topCents_ - (y - static_cast<float>(gridArea().getY())) / rowHeight * 100.0f;
}

void AudioEditor::fitToClip()
{
    const auto* c = clip();
    const int width = contentArea().getWidth();
    if (c == nullptr || width < 32)
        return;
    samplesPerPixel_ = std::max(
        1.0, static_cast<double>(std::max<core::SamplePosition>(1, c->sourceLength())) / (width - 16));
    scroll_ = static_cast<double>(c->sourceOffset);
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
    const float visible = static_cast<float>(gridArea().getHeight()) / rowHeight * 100.0f;
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
        horizontal_.setRangeLimits(std::min(start, scroll_), std::max(start + length, scroll_ + visible));
        horizontal_.setCurrentRange(scroll_, visible, juce::dontSendNotification);
    }
    const double rows = static_cast<double>(area.getHeight()) / rowHeight * 100.0;
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
        scroll_ = newRangeStart;
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
    const double first = toTimelineSpan(*c, scroll_ - static_cast<double>(c->sourceOffset));
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
    const auto s = sourceForX(static_cast<float>(p.x));
    const auto& edit = shownEdit();
    const core::VocalNote* best = nullptr;
    float bestDistance = 1.0e9f;
    for (const auto& n : edit.notes)
    {
        if (s < n.start || s >= n.end)
            continue;
        const float d = std::abs(yForCents(n.target) - static_cast<float>(p.y));
        if (d < static_cast<float>(rowHeight) * 1.2f && d < bestDistance)
        {
            best = &n;
            bestDistance = d;
        }
    }
    return best;
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
    const auto& notes = shownEdit().notes;
    for (size_t i = 0; i + 1 < notes.size(); ++i)
    {
        if (notes[i].end != notes[i + 1].start)
            continue;
        const float x = xForSource(static_cast<double>(notes[i].end));
        const float y0 = yForCents(notes[i].target), y1 = yForCents(notes[i + 1].target);
        if (std::abs(static_cast<float>(p.x) - x) <= 4.0f &&
            static_cast<float>(p.y) >= std::min(y0, y1) - rowHeight &&
            static_cast<float>(p.y) <= std::max(y0, y1) + rowHeight)
            return &notes[i];
    }
    return nullptr;
}

// ----- painting -------------------------------------------------------------------------------

juce::String AudioEditor::noteName(float cents)
{
    const int note = static_cast<int>(std::lround(cents / 100.0f));
    return juce::String(core::noteName(note));
}

void AudioEditor::drawTooltip(juce::Graphics& g, const juce::String& text, juce::Point<int> at)
{
    const auto& c = theme_.colours;
    g.setFont(theme_.caption());
    const int w = juce::GlyphArrangement::getStringWidthInt(theme_.caption(), text) + 2 * theme_.spacing.sm;
    const int h = theme_.sizes.controlCompact;
    auto box = juce::Rectangle<int>(at.x + 14, at.y + 18, w, h);
    if (box.getRight() > getWidth() - theme_.sizes.scrollbar)
        box.setX(at.x - w - 6);
    g.setColour(c.surface2);
    g.fillRoundedRectangle(box.toFloat(), theme_.radius.sm);
    g.setColour(c.lineStrong);
    g.drawRoundedRectangle(box.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
    g.setColour(c.textPrimary);
    g.drawText(text, box, juce::Justification::centred);
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
    right.setLeft(auditionButton_.getRight() + theme_.spacing.sm);
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
    const auto& map = tempoMap_;
    const auto toTimeline = [cl](double source)
    {
        return cl->start + static_cast<core::SamplePosition>(
                               std::llround((source - static_cast<double>(cl->sourceOffset)) * cl->stretch));
    };
    const auto toSource = [cl](core::SamplePosition t)
    { return static_cast<double>(cl->sourceOffset) + static_cast<double>(t - cl->start) / cl->stretch; };
    const auto first = std::max<core::SamplePosition>(0, toTimeline(scroll_));
    const auto last = toTimeline(static_cast<double>(sourceForX(static_cast<float>(ruler.getRight()))));
    const auto step = map.adaptiveStep(samplesPerPixel_ * cl->stretch, 48, first);
    g.setFont(theme_.caption());
    auto next = map.snap(first, step);
    for (int guard = 0; next <= last && guard < 100000; ++guard)
    {
        const auto t = next;
        next = std::max(t + 1, map.snap(map.move(t, step, 1), step));
        const float x = xForSource(toSource(t));
        if (x < ruler.getX())
            continue;
        const double q = map.quartersAt(static_cast<double>(t));
        const int barIndex = map.barAtQuarter(q + 1.0e-6);
        const double beats =
            (q - map.barStartQuarter(barIndex)) / core::TempoMap::quartersPerBeat(map.signatureAt(t));
        const bool onBeat = std::abs(beats - std::round(beats)) < 1.0e-4;
        const bool onBar = onBeat && std::llround(beats) == 0;
        g.setColour(onBar ? c.gridBar : c.gridBeat);
        g.fillRect(static_cast<int>(x),
                   onBar ? ruler.getY() + 4 : ruler.getBottom() - 6,
                   1,
                   onBar ? ruler.getHeight() - 4 : 6);
        if (onBar)
        {
            g.setColour(c.rulerText);
            g.drawText(juce::String(barIndex + 1),
                       static_cast<int>(x) + 3,
                       ruler.getY(),
                       40,
                       ruler.getHeight(),
                       juce::Justification::centredLeft);
        }
    }
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
    const int channels = std::clamp(peaks.numChannels(), 1, 2);
    const float channelHeight = static_cast<float>(area.getHeight()) / static_cast<float>(channels);
    const auto envelope = [cl](double source)
    {
        const double rel = toTimelineSpan(*cl, source - static_cast<double>(cl->sourceOffset));
        double e = cl->gain;
        if (cl->fadeIn > 0 && rel < static_cast<double>(cl->fadeIn))
            e *= core::fadeShape(static_cast<float>(rel / static_cast<double>(cl->fadeIn)),
                                 core::fadeShapeK(cl->fadeInCurve));
        if (cl->fadeOut > 0 && rel > static_cast<double>(cl->length - cl->fadeOut))
            e *= core::fadeShape(static_cast<float>((static_cast<double>(cl->length) - rel) /
                                                    static_cast<double>(cl->fadeOut)),
                                 core::fadeShapeK(cl->fadeOutCurve));
        return static_cast<float>(e);
    };
    for (int ch = 0; ch < channels; ++ch)
    {
        const float mid = static_cast<float>(area.getY()) + channelHeight * (static_cast<float>(ch) + 0.5f);
        g.setColour(c.lineSubtle);
        g.fillRect(static_cast<float>(area.getX()), mid, static_cast<float>(area.getWidth()), 1.0f);
        const int from = std::max(area.getX(), painted.getX());
        const int to = std::min(area.getRight(), painted.getRight());
        for (int x = from; x < to; ++x)
        {
            paintstats::counters().waveColumns++;
            const auto s0 = sourceForX(static_cast<float>(x));
            const auto s1 = std::max(s0 + 1, sourceForX(static_cast<float>(x + 1)));
            const bool inside = static_cast<double>(s0) >= clipStart && static_cast<double>(s0) < clipEnd;
            const auto r = peaks.lookup(ch, s0, s1);
            const float e = inside ? envelope(static_cast<double>(s0)) : 1.0f;
            const float top = mid - std::clamp(r.maximum * e, -1.0f, 1.0f) * channelHeight * 0.45f;
            const float bottom = mid - std::clamp(r.minimum * e, -1.0f, 1.0f) * channelHeight * 0.45f;
            g.setColour(inside ? colour.withAlpha(theme_.track.waveformAlpha) : colour.withAlpha(0.25f));
            g.fillRect(static_cast<float>(x), top, 1.0f, std::max(1.0f, bottom - top));
        }
    }
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
    if (fadeIn > 0)
        curve(xForSource(clipStart), fi, core::fadeShapeK(cl->fadeInCurve));
    if (fadeOut > 0)
        curve(xForSource(clipEnd), fo, core::fadeShapeK(cl->fadeOutCurve));
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
    if (yc < grid.getY() - rowHeight * 2 || yc > grid.getBottom() + rowHeight * 2)
        return;
    // The blob: as thick as the note is loud.
    juce::Path blob;
    const float step = 2.0f;
    std::vector<juce::Point<float>> top;
    for (float x = x0; x <= x1; x += step)
    {
        const float level = levelAt(sourceForX(std::min(x, x1)));
        top.emplace_back(x, yc - (1.5f + level * rowHeight * 0.75f));
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
        const float nearest = std::round(note.target / 100.0f) * 100.0f;
        juce::String label = noteName(note.target);
        if (std::abs(note.target - nearest) >= 1.0f)
            label += " " + signedCents(note.target - nearest);
        if (note.muted)
            label += "  muted";
        g.setFont(theme_.caption());
        g.setColour(selected ? c.textPrimary : c.textSecondary);
        g.drawText(label,
                   static_cast<int>(x0) + 2,
                   static_cast<int>(yc - rowHeight * 1.9f),
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
            const float nearest = std::round(n->target / 100.0f) * 100.0f;
            text << noteName(n->target) << "  " << signedCents(n->target - nearest) << "   \xc2\xb7   sung "
                 << noteName(n->centre) << " "
                 << signedCents(n->centre - std::round(n->centre / 100.0f) * 100.0f) << "   \xc2\xb7   drift "
                 << percent(n->drift) << "   \xc2\xb7   vibrato " << percent(n->vibrato) << "   \xc2\xb7   "
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
                       "snaps.";
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

    // Rows: one per semitone; black keys darker, out-of-key rows darker still.
    g.saveState();
    g.reduceClipRegion(grid.getUnion(keys));
    const int topNote = static_cast<int>(std::ceil(topCents_ / 100.0f)) + 1;
    const int bottomNote =
        static_cast<int>(std::floor(centsForY(static_cast<float>(grid.getBottom())) / 100.0f)) - 1;
    for (int note = std::min(127, topNote); note >= std::max(0, bottomNote); --note)
    {
        const float y = yForCents(static_cast<float>(note) * 100.0f + 50.0f);
        const auto row = juce::Rectangle<float>(
            static_cast<float>(grid.getX()), y, static_cast<float>(grid.getWidth()), rowHeight);
        if (isBlackKey(note))
        {
            g.setColour(c.laneAlt);
            g.fillRect(row);
        }
        if (key.isSet() && !key.contains(note))
        {
            g.setColour(c.canvas.withAlpha(0.45f));
            g.fillRect(row);
        }
        if (note % 12 == 0)
        {
            g.setColour(c.gridBar);
            g.fillRect(row.getX(), row.getBottom() - 1.0f, row.getWidth(), 1.0f);
        }
        // The keyboard.
        auto k = juce::Rectangle<float>(0.0f, y, static_cast<float>(keyboardWidth), rowHeight);
        g.setColour(isBlackKey(note) ? c.surface0 : c.surface2);
        g.fillRect(k.reduced(0.0f, 0.5f));
        if (key.isSet() && key.root == note % 12)
        {
            g.setColour(c.accent.withAlpha(0.6f));
            g.fillRect(k.withWidth(3.0f));
        }
        if (note % 12 == 0)
        {
            g.setFont(theme_.caption());
            g.setColour(c.textTertiary);
            g.drawText(noteName(static_cast<float>(note) * 100.0f),
                       k.toNearestInt().reduced(6, 0),
                       juce::Justification::centredRight);
        }
    }
    g.restoreState();

    // Beat lines.
    if (cl != nullptr && project_ != nullptr)
    {
        const auto& map = tempoMap_;
        const auto toTimeline = [cl](double source)
        {
            return cl->start + static_cast<core::SamplePosition>(std::llround(
                                   (source - static_cast<double>(cl->sourceOffset)) * cl->stretch));
        };
        const auto first = std::max<core::SamplePosition>(0, toTimeline(scroll_));
        const auto last = toTimeline(static_cast<double>(sourceForX(static_cast<float>(grid.getRight()))));
        const auto step = map.adaptiveStep(samplesPerPixel_ * cl->stretch, 48, first);
        g.setColour(c.gridSub);
        auto next = map.snap(first, step);
        for (int guard = 0; next <= last && guard < 100000; ++guard)
        {
            const auto t = next;
            next = std::max(t + 1, map.snap(map.move(t, step, 1), step));
            const float x = xForSource(static_cast<double>(cl->sourceOffset) +
                                       static_cast<double>(t - cl->start) / cl->stretch);
            if (x >= grid.getX())
                g.fillRect(x, static_cast<float>(grid.getY()), 1.0f, static_cast<float>(grid.getHeight()));
        }
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
    else if (!pitchEditable())
        paintNotPitchEditable(g);
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
        }
        else if (e.y < area.getY() + 14 && std::abs(static_cast<float>(e.x) - fo) < 7.0f)
        {
            drag_ = Drag::fadeOut;
            dragFade_ = cl->fadeOut;
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
                selection_ = {note->id};
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
            scroll_ = panScroll_ - dx * samplesPerPixel_;
            topCents_ = panTop_ - static_cast<float>(dy) / rowHeight * 100.0f;
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
            dragTip_ =
                juce::String(static_cast<double>(dragFade_) * 1000.0 / project_->sampleRate, 0) + " ms";
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
            float delta = -static_cast<float>(dy) / rowHeight * 100.0f;
            if (!e.mods.isAltDown())
                delta = std::round((dragged->target + delta) / 100.0f) * 100.0f - dragged->target;
            preview_ = dragBase_;
            for (auto& n : preview_.notes)
                if (isSelected(n.id))
                    n.target = std::clamp(n.target + delta, 2400.0f, 10800.0f);
            previewing_ = true;
            const auto* moved = preview_.findNote(dragNote_);
            const float nearest = std::round(moved->target / 100.0f) * 100.0f;
            dragTip_ = noteName(moved->target) + (std::abs(moved->target - nearest) >= 1.0f
                                                      ? " " + signedCents(moved->target - nearest)
                                                      : juce::String());
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
                        finished == Drag::fadeOut ? dragFade_ : cl->fadeOut));
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
    if (mode_ == Mode::wave && contentArea().contains(e.getPosition()))
    {
        setMode(Mode::pitch);
        return;
    }
    if (mode_ != Mode::pitch || analysis_ == nullptr || tool_ != Tool::pitch)
        return;
    if (const auto* note = noteAt(e.getPosition()))
    {
        auto next = cl->vocal;
        std::vector<core::Id> ids = isSelected(note->id) ? selection_ : std::vector<core::Id>{note->id};
        pitch::snapToSemitones(next, ids);
        emit(std::move(next), "Snap pitch");
    }
}

void AudioEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        const auto anchor = static_cast<double>(sourceForX(static_cast<float>(e.x)));
        const double factor = wheel.deltaY > 0 ? 0.8 : 1.25;
        samplesPerPixel_ = std::clamp(samplesPerPixel_ * factor, 1.0, 100000.0);
        scroll_ = anchor - (e.x - leftEdge()) * samplesPerPixel_;
        notifyTimeView();
    }
    else if (e.mods.isShiftDown() || mode_ == Mode::wave)
    {
        scroll_ -= wheel.deltaY * 200.0 * samplesPerPixel_;
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
    if (code >= '1' && code <= '5' && !mods.isAnyModifierKeyDown())
    {
        setTool(static_cast<Tool>(code - '1'));
        return true;
    }
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        const float step = mods.isShiftDown() ? 10.0f : 100.0f;
        nudgeSelection(code == juce::KeyPress::upKey ? step : -step);
        return true;
    }
    if ((code == 'M' || code == 'm') && !mods.isAnyModifierKeyDown())
    {
        toggleMuteSelection();
        return true;
    }
    if ((code == 'A' || code == 'a') && mods.isCommandDown())
    {
        selectAll();
        return true;
    }
    if (code == juce::KeyPress::escapeKey)
    {
        selection_.clear();
        repaint();
        return true;
    }
    return false;
}

void AudioEditor::correctToKey(const pitch::CorrectionOptions& options, const std::string& name)
{
    const auto* cl = clip();
    if (cl == nullptr || project_ == nullptr || !cl->vocal.active() || !onEdit || !pitchEditable())
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    auto key = project_->key;
    if (!key.isSet())
    {
        key = pitch::detectKey(cl->vocal.notes);
        commands.push_back(std::make_unique<core::SetProjectKey>(key));
    }
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
            subset.push_back(n);
            chords.push_back(i < allChords.size() ? allChords[i] : 0);
        }
    }
    const auto result = pitch::correctToKey(subset, key, project_->sampleRate, options, chords);
    auto next = cl->vocal;
    for (const auto& corrected : result.notes)
        if (auto* n = next.findNote(corrected.id))
            *n = corrected;
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
    const auto* cl = clip();
    return cl != nullptr && !cl->isStretched();
}

void AudioEditor::paintNotPitchEditable(juce::Graphics& g)
{
    const auto area = contentArea();
    g.setColour(theme_.colours.textSecondary);
    g.setFont(theme_.body());
    g.drawFittedText("Pitch edits play on clips that are not stretched or transposed.\n"
                     "Set the clip's stretch and transpose back to edit its pitch.",
                     area.reduced(theme_.spacing.lg),
                     juce::Justification::centred,
                     3);
}

void AudioEditor::refreshToolbar()
{
    const bool editable = pitchEditable();
    for (auto* b : toolButtons())
        b->setEnabled(editable);
    correctButton_.setEnabled(editable);
    breathsButton_.setEnabled(editable);
    juce::String key = "No key";
    if (project_ != nullptr && project_->key.isSet())
        key = juce::String(core::pitchClassName(project_->key.root)) + " " +
              core::scaleName(project_->key.scale);
    keyButton_.setButtonText(key);
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
    juce::PopupMenu menu;
    const core::ScaleKind scales[] = {core::ScaleKind::major,
                                      core::ScaleKind::minor,
                                      core::ScaleKind::harmonicMinor,
                                      core::ScaleKind::dorian,
                                      core::ScaleKind::mixolydian,
                                      core::ScaleKind::majorPentatonic,
                                      core::ScaleKind::minorPentatonic};
    const auto current = project_ != nullptr ? project_->key : core::MusicalKey{};
    menu.addItem(1, "Detect from this vocal");
    menu.addItem(2, "No key", true, !current.isSet());
    menu.addSeparator();
    for (int s = 0; s < 7; ++s)
    {
        juce::PopupMenu roots;
        for (int r = 0; r < 12; ++r)
            roots.addItem(100 + s * 12 + r,
                          juce::String(core::pitchClassName(r)) + " " + core::scaleName(scales[s]),
                          true,
                          current.isSet() && current.root == r && current.scale == scales[s]);
        juce::String title = core::scaleName(scales[s]);
        menu.addSubMenu(title.substring(0, 1).toUpperCase() + title.substring(1), roots);
    }
    juce::Component::SafePointer<AudioEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(keyButton_),
                       [safe, scales](int choice)
                       {
                           if (safe == nullptr || choice == 0 || !safe->onEdit)
                               return;
                           core::MusicalKey key;
                           if (choice == 1)
                           {
                               const auto* cl = safe->clip();
                               if (cl == nullptr || !cl->vocal.active())
                                   return;
                               key = engine::pitch::detectKey(cl->vocal.notes);
                           }
                           else if (choice >= 100)
                               key = core::MusicalKey{(choice - 100) % 12, scales[(choice - 100) / 12]};
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
