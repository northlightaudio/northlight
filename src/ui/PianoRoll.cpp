#include "ui/PianoRoll.h"

#include "core/Command.h"
#include "core/NoteNames.h"
#include "plugins/BuiltIns.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/PaintStats.h"
#include "ui/Shortcuts.h"
#include "ui/components/Accessibility.h"
#include "ui/components/DragTip.h"
#include "ui/components/KeyMenu.h"
#include "ui/timeline/GridLines.h"
#include "ui/timeline/TimeGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace northlight::ui
{

namespace
{
constexpr int notes = 128;
constexpr core::SamplePosition minNoteLength = 1;

using core::isBlackKey;
} // namespace

PianoRoll::PianoRoll(const Theme& theme)
    : theme_(theme), selectTool_(theme_, Icon::pointer, "Select tool: move, resize and select notes  1"),
      pencilTool_(theme_, Icon::pencil, "Pencil: click to add a note, drag to set its length  3"),
      eraseTool_(theme_, Icon::eraser, "Erase: click or sweep over notes to delete them  4"),
      splitTool_(theme_, Icon::scissors, "Split: click a note to cut it there  5"),
      glueTool_(theme_, Icon::glue, "Glue: click a note to join it with the next of its pitch  6"),
      muteTool_(theme_, Icon::muteClip, "Mute: click or sweep over notes to mute or unmute them  7"),
      link_(theme_, Icon::link, "Link zoom to the arrange view"),
      followButton_(theme_, Icon::followPlayhead, "Follow the playhead: turn the page as it plays"),
      auditionButton_(theme_, Icon::monitor, "Hear notes while drawing and moving them"),
      ghostButton_(theme_, Icon::ghostNotes, "Show the other MIDI tracks' notes as ghosts"),
      namedButton_(theme_, Icon::filter, "Show only the rows the instrument names (and any with notes)")
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    rulerHeight_ = theme_.sizes.ruler;
    scrollbarSize_ = theme_.sizes.scrollbar;
    for (auto* bar : {&horizontal_, &vertical_})
    {
        bar->setAutoHide(false);
        bar->addListener(this);
        addAndMakeVisible(*bar);
    }
    rebuildRows();
    verticalScroll_ = (notes - 84) * rowHeight_; // start around C3..C6

    selectTool_.onClick = [this] { setTool(Tool::select); };
    pencilTool_.onClick = [this] { setTool(Tool::draw); };
    eraseTool_.onClick = [this] { setTool(Tool::erase); };
    splitTool_.onClick = [this] { setTool(Tool::split); };
    glueTool_.onClick = [this] { setTool(Tool::glue); };
    muteTool_.onClick = [this] { setTool(Tool::mute); };
    for (auto* b : toolButtons())
    {
        b->setIconSize(theme_.sizes.iconSmall);
        addAndMakeVisible(*b);
    }
    refreshToolButtons();
    snapButton_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    snapButton_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    Shortcuts::tag(snapButton_, "Snap grid: straight, triplet and dotted values", Command::pianoRollSnap);
    snapButton_.onClick = [this] { showSnapMenu(); };
    addAndMakeVisible(snapButton_);
    quantiseButton_.setButtonText("Quantise");
    quantiseButton_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    quantiseButton_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    Shortcuts::tag(quantiseButton_,
                   "Quantise the selected notes; strength, swing and humanise are in the menu",
                   Command::pianoRollQuantise);
    quantiseButton_.onClick = [this] { showQuantiseMenu(); };
    addAndMakeVisible(quantiseButton_);
    velocityButton_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    velocityButton_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    velocityButton_.setTooltip("The velocity new notes take: the last one edited, or one picked here");
    velocityButton_.onClick = [this] { showVelocityMenu(); };
    addAndMakeVisible(velocityButton_);
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
    showToggle(auditionButton_, audition_);
    auditionButton_.onClick = [this] { setAudition(auditionButton_.getToggleState()); };
    addAndMakeVisible(auditionButton_);
    ghostButton_.setClickingTogglesState(true);
    ghostButton_.onClick = [this] { setGhostTracks(ghostButton_.getToggleState()); };
    addAndMakeVisible(ghostButton_);
    namedButton_.setClickingTogglesState(true);
    namedButton_.onClick = [this] { setNamedRowsOnly(namedButton_.getToggleState()); };
    addChildComponent(namedButton_);
    scaleButton_.setButtonText("Scale");
    scaleButton_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    scaleButton_.setTooltip("Shade the notes outside the project key's scale and mark its notes on the keys");
    scaleButton_.setClickingTogglesState(true);
    showToggle(scaleButton_, scaleHighlight_);
    scaleButton_.onClick = [this] { setScaleHighlight(scaleButton_.getToggleState()); };
    addAndMakeVisible(scaleButton_);
    keyButton_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    keyButton_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    keyButton_.setTooltip(
        "The project's key: the scale the rows shade (and what the audio editor's Correct aims at)");
    keyButton_.onClick = [this] { showKeyMenu(); };
    addAndMakeVisible(keyButton_);
    lanes_.push_back(LaneRow{Lane::velocities(), defaultLaneHeight, nullptr});
    refreshToolbar();
}

PianoRoll::~PianoRoll()
{
    horizontal_.removeListener(this);
    vertical_.removeListener(this);
}

std::array<IconButton*, 6> PianoRoll::toolButtons() noexcept
{
    return {&selectTool_, &pencilTool_, &eraseTool_, &splitTool_, &glueTool_, &muteTool_};
}

std::unique_ptr<ControllerLane> PianoRoll::makeControllerLane()
{
    auto lane = std::make_unique<ControllerLane>(theme_);
    auto& ref = *lane;
    ref.view = [this, &ref]
    {
        auto axis = view(); // in the lane's own coordinates
        axis.originX -= ref.getX();
        return axis;
    };
    ref.snap = [this](core::SamplePosition s) { return snapped(s); };
    ref.gridSamples = [this] { return gridSamples(); };
    ref.snapEnabled = [this] { return snap_; };
    ref.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
    { emit(std::move(commands), name); };
    ref.onPressed = [this, &ref]
    {
        for (size_t i = 0; i < lanes_.size(); ++i)
            if (lanes_[i].controller.get() == &ref)
                focusLane_ = static_cast<int>(i);
        grabKeyboardFocus();
    };
    ref.setClip(project_, trackId_, clipId_);
    ref.setColour(trackColour());
    ref.setTool(tool_ == Tool::draw    ? ControllerLane::Tool::draw
                : tool_ == Tool::erase ? ControllerLane::Tool::erase
                                       : ControllerLane::Tool::select);
    addAndMakeVisible(ref);
    return lane;
}

void PianoRoll::setClip(const core::Project* project, core::Id trackId, core::Id clipId)
{
    const bool same = project == project_ && trackId == trackId_ && clipId == clipId_;
    project_ = project;
    tempoMap_ = project != nullptr ? core::tempoMapOf(*project) : core::TempoMap();
    trackId_ = trackId;
    clipId_ = clipId;
    for (auto& l : lanes_)
        if (l.controller != nullptr)
        {
            l.controller->setClip(project, trackId, clipId);
            l.controller->setColour(trackColour());
        }
    refreshNoteNames();
    refreshToolbar();
    const auto* c = clip();
    if (c == nullptr)
    {
        if (!selection_.empty())
        {
            selection_.clear();
            selectionChanged();
        }
        repaint();
        return;
    }
    const auto before = selection_.size();
    std::erase_if(selection_, [c](core::Id id) { return c->findNote(id) == nullptr; });
    if (before != selection_.size())
        selectionChanged();
    rebuildRows();
    if (!same)
        fitToClip();
    updateScrollbars();
    repaint();
}

void PianoRoll::setPlayhead(core::SamplePosition position)
{
    const auto* c = clip();
    const auto relative =
        c != nullptr && position >= c->start && position < c->end() ? position - c->start : -1;
    if (relative == playhead_)
        return;
    // Following: a playhead past either edge turns the page (it lands an eighth in).
    if (follow_ && relative >= 0 && gridArea().getWidth() > 16)
    {
        const int x = xForSample(relative);
        const auto grid = gridArea();
        if (x < grid.getX() || x > grid.getRight() - 8)
        {
            playhead_ = relative;
            scroll_ =
                std::max<core::SamplePosition>(0,
                                               relative - static_cast<core::SamplePosition>(std::llround(
                                                              grid.getWidth() / 8.0 * samplesPerPixel_)));
            lit_ = soundingKeys();
            updateScrollbars();
            notifyTimeView();
            repaint();
            return;
        }
    }
    const int oldX = playhead_ >= 0 ? xForSample(playhead_) : -1;
    playhead_ = relative;
    const int x = playhead_ >= 0 ? xForSample(playhead_) : -1;
    // The playhead's columns and the keys that lit or went out repaint; nothing else.
    if (oldX >= 0)
        repaint(oldX - 2, 0, 5, getHeight());
    if (x >= 0)
        repaint(x - 2, 0, 5, getHeight());
    const auto lit = soundingKeys();
    const auto changed = lit ^ lit_;
    lit_ = lit;
    if (changed.any())
    {
        const auto keys = keysArea();
        for (int note = 0; note < notes; ++note)
            if (changed[static_cast<size_t>(note)] && rowShown(note))
                repaint(keys.getX(), yForNote(note), keys.getWidth(), rowHeight_);
    }
}

void PianoRoll::setLiveRecording(const engine::LiveRecording* live)
{
    live_ = live;
}

void PianoRoll::liveRecordingChanged()
{
    bool any = false;
    if (live_ != nullptr)
        for (const auto& take : live_->midi)
            any = any || (take.trackId == trackId_ && !take.clip.notes.empty());
    // While notes come in, and once more when they stop (they land in the clip).
    if (any || liveShown_)
        repaint(gridArea());
    liveShown_ = any;
}

std::bitset<128> PianoRoll::soundingKeys() const
{
    std::bitset<128> lit;
    if (const auto* c = clip(); c != nullptr && playhead_ >= 0)
        for (const auto& n : c->notes)
            if (!n.muted && playhead_ >= n.start && playhead_ < n.end() && n.note >= 0 && n.note < notes)
                lit.set(static_cast<size_t>(n.note));
    return lit;
}

void PianoRoll::setTimeView(double samplesPerPixel, core::SamplePosition absoluteFirstVisible)
{
    const auto* c = clip();
    if (!linked_ || c == nullptr)
        return;
    const auto relative = absoluteFirstVisible - c->start;
    if (std::abs(samplesPerPixel - samplesPerPixel_) < 1.0e-9 && relative == scroll_)
        return;
    samplesPerPixel_ = std::max(1.0, samplesPerPixel);
    scroll_ = relative;
    updateScrollbars();
    repaint();
}

void PianoRoll::setFollowPlayhead(bool on)
{
    follow_ = on;
    showToggle(followButton_, on);
}

void PianoRoll::setTool(Tool tool)
{
    tool_ = tool;
    for (auto& l : lanes_)
        if (l.controller != nullptr)
            l.controller->setTool(tool == Tool::draw    ? ControllerLane::Tool::draw
                                  : tool == Tool::erase ? ControllerLane::Tool::erase
                                                        : ControllerLane::Tool::select);
    refreshToolButtons();
    setMouseCursor(toolCursor());
    repaint();
}

void PianoRoll::refreshToolButtons()
{
    showToggle(selectTool_, tool_ == Tool::select);
    showToggle(pencilTool_, tool_ == Tool::draw);
    showToggle(eraseTool_, tool_ == Tool::erase);
    showToggle(splitTool_, tool_ == Tool::split);
    showToggle(glueTool_, tool_ == Tool::glue);
    showToggle(muteTool_, tool_ == Tool::mute);
}

juce::MouseCursor PianoRoll::toolCursor() const
{
    switch (tool_)
    {
        case Tool::draw:
            return Icons::toolCursor(theme_, Icon::pencil);
        case Tool::erase:
            return Icons::toolCursor(theme_, Icon::eraser);
        case Tool::split:
            return Icons::toolCursor(theme_, Icon::scissors);
        case Tool::glue:
            return Icons::toolCursor(theme_, Icon::glue);
        case Tool::mute:
            return Icons::toolCursor(theme_, Icon::muteClip);
        case Tool::select:
            break;
    }
    return juce::MouseCursor(juce::MouseCursor::NormalCursor);
}

const core::MidiClip* PianoRoll::clip() const noexcept
{
    const auto* t = track();
    return t != nullptr ? t->findMidiClip(clipId_) : nullptr;
}

const core::Track* PianoRoll::track() const noexcept
{
    return project_ != nullptr ? project_->findTrack(trackId_) : nullptr;
}

juce::Colour PianoRoll::trackColour() const
{
    const auto* t = track();
    if (t == nullptr)
        return theme_.colours.midiNote;
    return colourOf(*t);
}

juce::Colour PianoRoll::colourOf(const core::Track& t) const
{
    if (project_ == nullptr)
        return theme_.colours.midiNote;
    return theme_.track.forChannel(*project_, t.id);
}

void PianoRoll::selectionChanged()
{
    if (onSelectionChanged)
        onSelectionChanged();
}

// ----- rows and names ------------------------------------------------------------------------

void PianoRoll::refreshNoteNames()
{
    // The instrument the track plays (a MIDI track aims at an instrument channel).
    std::string type;
    if (const auto* t = track(); t != nullptr && project_ != nullptr)
    {
        const core::Track* instrument = t->isMidi() ? project_->findTrack(t->midiTarget) : t;
        if (instrument != nullptr && instrument->hasInstrument())
            type = instrument->instrument.type;
    }
    if (type == namesFor_)
        return;
    namesFor_ = type;
    namedNotes_.reset();
    for (int n = 0; n < notes; ++n)
    {
        const auto name = plugins::builtInNoteName(type, n);
        noteNames_[static_cast<size_t>(n)] = juce::String::fromUTF8(name.c_str());
        namedNotes_.set(static_cast<size_t>(n), !name.empty());
    }
    if (!namedNotes_.any())
        namedOnly_ = false;
    showToggle(namedButton_, namedOnly_);
    rebuildRows();
}

juce::String PianoRoll::rowName(int note) const
{
    return note >= 0 && note < notes ? noteNames_[static_cast<size_t>(note)] : juce::String();
}

juce::String PianoRoll::labelFor(int note) const
{
    const auto name = rowName(note);
    return name.isNotEmpty() ? name : noteName(note);
}

void PianoRoll::rebuildRows()
{
    rows_.clear();
    rowOf_.fill(-1);
    std::bitset<128> used;
    if (const auto* c = clip())
        for (const auto& n : c->notes)
            used.set(static_cast<size_t>(std::clamp(n.note, 0, 127)));
    // A name several notes share (a kit's alternative notes for a pad) shows once:
    // on the note the clip plays, else the lowest.
    std::bitset<128> named;
    for (int note = 0; note < notes && namedOnly_; ++note)
    {
        if (!namedNotes_[static_cast<size_t>(note)])
            continue;
        int keep = -1;
        for (int other = 0; other < notes; ++other)
            if (namedNotes_[static_cast<size_t>(other)] &&
                noteNames_[static_cast<size_t>(other)] == noteNames_[static_cast<size_t>(note)])
            {
                if (keep < 0 || (used[static_cast<size_t>(other)] && !used[static_cast<size_t>(keep)]))
                    keep = other;
            }
        named.set(static_cast<size_t>(note), keep == note);
    }
    for (int note = notes - 1; note >= 0; --note)
    {
        const bool shown = !namedOnly_ || named[static_cast<size_t>(note)] || used[static_cast<size_t>(note)];
        if (!shown)
            continue;
        rowOf_[static_cast<size_t>(note)] = static_cast<int>(rows_.size());
        rows_.push_back(note);
    }
    verticalScroll_ = std::clamp(verticalScroll_, 0, maxVerticalScroll());
}

void PianoRoll::setNamedRowsOnly(bool on)
{
    namedOnly_ = on && hasNoteNames();
    showToggle(namedButton_, namedOnly_);
    rebuildRows();
    if (namedOnly_)
        verticalScroll_ = 0;
    else
        fitToClip();
    updateScrollbars();
    repaint();
}

void PianoRoll::setGhostTracks(bool on)
{
    ghostTracks_ = on;
    showToggle(ghostButton_, on);
    repaint();
}

void PianoRoll::setRowHeight(int pixels)
{
    const int height = std::clamp(pixels, minRowHeight, maxRowHeight);
    if (height == rowHeight_)
        return;
    // The row in the middle of the grid stays there.
    const int middle = gridArea().getHeight() / 2;
    const double row = static_cast<double>(verticalScroll_ + middle) / rowHeight_;
    rowHeight_ = height;
    verticalScroll_ =
        std::clamp(static_cast<int>(std::lround(row * rowHeight_)) - middle, 0, maxVerticalScroll());
    updateScrollbars();
    repaint();
}

int PianoRoll::maxVerticalScroll() const noexcept
{
    return std::max(0, rowsShown() * rowHeight_ - gridArea().getHeight());
}

// ----- geometry -------------------------------------------------------------------------------

juce::Rectangle<int> PianoRoll::rulerArea() const noexcept
{
    return {
        keyboardWidth, toolbarHeight, std::max(0, getWidth() - keyboardWidth - scrollbarSize_), rulerHeight_};
}

juce::Rectangle<int> PianoRoll::keysArea() const noexcept
{
    return {0, toolbarHeight + rulerHeight_, keyboardWidth, gridArea().getHeight()};
}

int PianoRoll::lanesHeight() const noexcept
{
    int h = 0;
    for (const auto& l : lanes_)
        h += l.height;
    return h;
}

juce::Rectangle<int> PianoRoll::gridArea() const noexcept
{
    return {keyboardWidth,
            toolbarHeight + rulerHeight_,
            std::max(0, getWidth() - keyboardWidth - scrollbarSize_),
            std::max(0, getHeight() - toolbarHeight - rulerHeight_ - lanesHeight() - scrollbarSize_)};
}

juce::Rectangle<int> PianoRoll::lanesArea() const noexcept
{
    const auto grid = gridArea();
    return {keyboardWidth, grid.getBottom(), grid.getWidth(), lanesHeight()};
}

juce::Rectangle<int> PianoRoll::laneArea(std::size_t index) const noexcept
{
    int y = gridArea().getBottom();
    for (std::size_t i = 0; i < index && i < lanes_.size(); ++i)
        y += lanes_[i].height;
    const int h = index < lanes_.size() ? lanes_[index].height : 0;
    return {keyboardWidth, y, std::max(0, getWidth() - keyboardWidth - scrollbarSize_), h};
}

juce::Rectangle<int> PianoRoll::laneLabelArea(std::size_t index) const noexcept
{
    return {0, laneArea(index).getY() + 1, keyboardWidth, theme_.sizes.controlCompact};
}

int PianoRoll::laneEdgeAt(juce::Point<int> p) const noexcept
{
    for (std::size_t i = 0; i < lanes_.size(); ++i)
        if (std::abs(p.y - laneArea(i).getY()) <= laneResizeZone && p.x < getWidth() - scrollbarSize_)
            return static_cast<int>(i);
    return -1;
}

int PianoRoll::laneIndexAt(juce::Point<int> p) const noexcept
{
    for (std::size_t i = 0; i < lanes_.size(); ++i)
        if (laneArea(i).withX(0).withWidth(getWidth()).contains(p))
            return static_cast<int>(i);
    return -1;
}

int PianoRoll::velocityLaneAt(juce::Point<int> p) const noexcept
{
    const int lane = laneIndexAt(p);
    return lane >= 0 && lanes_[static_cast<size_t>(lane)].lane.velocity &&
                   laneArea(static_cast<size_t>(lane)).contains(p)
               ? lane
               : -1;
}

int PianoRoll::xForSample(core::SamplePosition s) const noexcept
{
    return view().xOf(s);
}

core::SamplePosition PianoRoll::sampleForX(int x) const noexcept
{
    return view().sampleAt(x);
}

int PianoRoll::yForNote(int note) const noexcept
{
    const int row = rowOf_[static_cast<size_t>(std::clamp(note, 0, 127))];
    if (row < 0)
        return -1000000; // a hidden row: nowhere on the grid
    return toolbarHeight + rulerHeight_ + row * rowHeight_ - verticalScroll_;
}

int PianoRoll::noteForY(int y) const noexcept
{
    if (rows_.empty())
        return 60;
    const int row = std::clamp(
        (y - toolbarHeight - rulerHeight_ + verticalScroll_) / std::max(1, rowHeight_), 0, rowsShown() - 1);
    return rows_[static_cast<size_t>(row)];
}

core::GridStep PianoRoll::gridStep(core::SamplePosition at) const noexcept
{
    const auto* c = clip();
    const auto absolute = (c != nullptr ? c->start : 0) + at;
    return noteGridStep(noteGrids[grid_], tempoMap_, absolute, samplesPerPixel_);
}

core::SamplePosition PianoRoll::gridSamples(core::SamplePosition at) const noexcept
{
    const auto* c = clip();
    const auto absolute = (c != nullptr ? c->start : 0) + at;
    return std::max<core::SamplePosition>(1, std::llround(tempoMap_.stepSamples(gridStep(at), absolute)));
}

core::SamplePosition PianoRoll::snapped(core::SamplePosition s, bool force) const noexcept
{
    s = std::max<core::SamplePosition>(0, s);
    const bool alt = juce::ModifierKeys::getCurrentModifiers().isAltDown();
    if (!(snap_ || force) || alt)
        return s;
    // Clip-relative positions snap to the song's grid.
    const auto* c = clip();
    const auto start = c != nullptr ? c->start : 0;
    return std::max<core::SamplePosition>(0, tempoMap_.snap(start + s, gridStep(s)) - start);
}

juce::Rectangle<int> PianoRoll::noteBounds(const core::MidiNote& note) const noexcept
{
    const int x0 = xForSample(note.start);
    const int x1 = std::max(x0 + 2, xForSample(note.end()));
    return {x0, yForNote(note.note), x1 - x0, rowHeight_};
}

void PianoRoll::setAudition(bool on)
{
    if (!on)
        stopAudition();
    audition_ = on;
    showToggle(auditionButton_, on);
}

void PianoRoll::auditionNote(int note)
{
    if (!audition_ || note == auditioned_)
        return;
    stopAudition();
    auditioned_ = std::clamp(note, 0, 127);
    if (onPreviewNote)
        onPreviewNote(trackId_, auditioned_, true);
}

void PianoRoll::stopAudition()
{
    if (auditioned_ >= 0 && onPreviewNote)
        onPreviewNote(trackId_, auditioned_, false);
    auditioned_ = -1;
}

core::MidiNote PianoRoll::previewOf(const core::MidiNote& note) const noexcept
{
    core::MidiNote p = note;
    if (drag_ == Drag::move && draggedFar_ && isSelected(note.id))
    {
        p.start = std::max<core::SamplePosition>(0, note.start + moveDelta_);
        p.note = std::clamp(note.note + pitchDelta_, 0, 127);
        if (!rowShown(p.note))
            p.note = note.note; // a hidden row: keep the row it has
    }
    else if (drag_ == Drag::resize && draggedFar_ &&
             (note.id == dragNote_ || (isSelected(dragNote_) && isSelected(note.id))))
        p.length = lengthPreview_;
    else if (drag_ == Drag::velocity && draggedFar_)
    {
        if (const auto it = velocityRamp_.find(note.id); it != velocityRamp_.end())
            p.velocity = it->second;
        else if (note.id == dragNote_ || (isSelected(dragNote_) && isSelected(note.id)))
            p.velocity = velocityAbsolute_
                             ? velocityPreview_
                             : std::clamp(note.velocity + velocityPreview_ - dragOriginal_.velocity, 1, 127);
    }
    else if (drag_ == Drag::mute && std::find(swept_.begin(), swept_.end(), note.id) != swept_.end())
        p.muted = muteTo_;
    return p;
}

const core::MidiNote* PianoRoll::noteAt(juce::Point<int> p, bool& onRightEdge) const noexcept
{
    onRightEdge = false;
    const auto* c = clip();
    if (c == nullptr || !gridArea().contains(p))
        return nullptr;
    for (auto it = c->notes.rbegin(); it != c->notes.rend(); ++it)
    {
        if (!rowShown(it->note))
            continue;
        const auto bounds = noteBounds(*it);
        if (!bounds.contains(p))
            continue;
        onRightEdge = bounds.getWidth() > 2 * edgeZone && p.x >= bounds.getRight() - edgeZone;
        return &*it;
    }
    return nullptr;
}

const core::MidiNote* PianoRoll::noteAtVelocityBar(juce::Point<int> p, int lane) const noexcept
{
    const auto* c = clip();
    if (c == nullptr || lane < 0 || !laneArea(static_cast<size_t>(lane)).contains(p))
        return nullptr;
    // The bar nearest to the pointer's x among those within a few pixels.
    const core::MidiNote* best = nullptr;
    int bestDistance = 6;
    for (const auto& n : c->notes)
    {
        const int x = xForSample(n.start);
        const int distance = std::abs(x + 3 - p.x);
        if (distance <= bestDistance)
        {
            best = &n;
            bestDistance = distance;
        }
    }
    return best;
}

bool PianoRoll::isSelected(core::Id note) const noexcept
{
    return std::find(selection_.begin(), selection_.end(), note) != selection_.end();
}

// ----- scrolling and zoom ---------------------------------------------------------------------

void PianoRoll::fitToClip()
{
    const auto* c = clip();
    const int width = std::max(1, gridArea().getWidth());
    if (c == nullptr || c->length <= 0)
        return;
    // Not laid out yet (a panel shown for the first time): fit once it is.
    fitPending_ = width <= 16;
    if (fitPending_)
        return;
    samplesPerPixel_ = std::max(1.0, static_cast<double>(c->length) / static_cast<double>(width - 16));
    scroll_ = 0;
    // Scroll vertically to the notes' middle.
    if (!c->notes.empty())
    {
        int top = rowsShown(), bottom = 0;
        for (const auto& n : c->notes)
            if (rowShown(n.note))
            {
                top = std::min(top, rowOf_[static_cast<size_t>(n.note)]);
                bottom = std::max(bottom, rowOf_[static_cast<size_t>(n.note)]);
            }
        if (top <= bottom)
        {
            const int middle = (top + bottom + 1) * rowHeight_ / 2;
            verticalScroll_ = std::clamp(middle - gridArea().getHeight() / 2, 0, maxVerticalScroll());
        }
    }
}

void PianoRoll::zoomToFitNotes()
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    const auto grid = gridArea();
    if (c->notes.empty() || grid.getWidth() < 32 || grid.getHeight() < 32)
    {
        fitToClip();
        updateScrollbars();
        repaint();
        return;
    }
    core::SamplePosition first = c->notes.front().start, last = 0;
    int top = rowsShown(), bottom = 0;
    for (const auto& n : c->notes)
    {
        first = std::min(first, n.start);
        last = std::max(last, n.end());
        if (rowShown(n.note))
        {
            top = std::min(top, rowOf_[static_cast<size_t>(n.note)]);
            bottom = std::max(bottom, rowOf_[static_cast<size_t>(n.note)]);
        }
    }
    // Time: the notes with a grid step either side.
    const auto margin = gridSamples(first);
    const auto from = std::max<core::SamplePosition>(0, first - margin);
    samplesPerPixel_ =
        std::clamp(static_cast<double>(last + margin - from) / std::max(1, grid.getWidth()), 1.0, 100000.0);
    scroll_ = from;
    // Rows: as tall as fits the range (with a row either side), within the limits.
    if (top <= bottom)
    {
        const int span = bottom - top + 3;
        rowHeight_ = std::clamp(grid.getHeight() / std::max(1, span), minRowHeight, maxRowHeight);
        const int middle = (top + bottom + 1) * rowHeight_ / 2;
        verticalScroll_ = std::clamp(middle - grid.getHeight() / 2, 0, maxVerticalScroll());
    }
    updateScrollbars();
    notifyTimeView();
    repaint();
}

void PianoRoll::updateScrollbars()
{
    const juce::ScopedValueSetter<bool> guard(updatingScrollbars_, true);
    const auto* c = clip();
    const double length =
        c != nullptr ? static_cast<double>(std::max<core::SamplePosition>(1, c->length)) : 1.0;
    const double visible = gridArea().getWidth() * samplesPerPixel_;
    horizontal_.setRangeLimits(std::min(0.0, static_cast<double>(scroll_)),
                               std::max(length, static_cast<double>(scroll_) + visible));
    horizontal_.setCurrentRange(static_cast<double>(scroll_), visible);
    vertical_.setRangeLimits(0.0, std::max(1, rowsShown() * rowHeight_));
    vertical_.setCurrentRange(verticalScroll_, gridArea().getHeight());
}

void PianoRoll::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (updatingScrollbars_)
        return;
    if (bar == &horizontal_)
    {
        scroll_ = static_cast<core::SamplePosition>(newRangeStart);
        notifyTimeView();
    }
    else
        verticalScroll_ = static_cast<int>(newRangeStart);
    repaint();
}

void PianoRoll::notifyTimeView()
{
    const auto* c = clip();
    if (linked_ && c != nullptr && onTimeViewChanged)
        onTimeViewChanged(samplesPerPixel_, c->start + scroll_);
}

void PianoRoll::layoutLanes()
{
    for (size_t i = 0; i < lanes_.size(); ++i)
        if (lanes_[i].controller != nullptr)
            // Below the lane's top edge, which drags its height.
            lanes_[i].controller->setBounds(laneArea(i).withTrimmedTop(laneResizeZone + 1));
}

void PianoRoll::resized()
{
    if (fitPending_ && gridArea().getWidth() > 16)
        fitToClip();
    auto bar = getLocalBounds()
                   .removeFromTop(toolbarHeight)
                   .reduced(theme_.spacing.sm, (toolbarHeight - theme_.sizes.controlCompact) / 2);
    // The key at the right; the tools and toggles from the left.
    keyButton_.setBounds(bar.removeFromRight(std::min(bar.getWidth(), 120)));
    bar.removeFromRight(theme_.spacing.sm);
    for (auto* b : toolButtons())
        b->setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
    bar.removeFromLeft(theme_.spacing.sm);
    snapButton_.setBounds(bar.removeFromLeft(72));
    bar.removeFromLeft(theme_.spacing.xs);
    quantiseButton_.setBounds(bar.removeFromLeft(104));
    bar.removeFromLeft(theme_.spacing.xs);
    velocityButton_.setBounds(bar.removeFromLeft(92));
    bar.removeFromLeft(theme_.spacing.sm);
    for (auto* b : {&link_, &followButton_, &auditionButton_, &ghostButton_})
    {
        b->setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
        bar.removeFromLeft(theme_.spacing.xs);
    }
    namedButton_.setVisible(hasNoteNames());
    if (hasNoteNames())
    {
        namedButton_.setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
        bar.removeFromLeft(theme_.spacing.xs);
    }
    scaleButton_.setBounds(bar.removeFromLeft(56));
    // A narrow panel drops the least used controls rather than overlap the key.
    velocityButton_.setVisible(scaleButton_.getRight() <= keyButton_.getX());
    scaleButton_.setVisible(scaleButton_.getRight() <= keyButton_.getX());
    horizontal_.setBounds(keyboardWidth,
                          getHeight() - scrollbarSize_,
                          std::max(0, getWidth() - keyboardWidth - scrollbarSize_),
                          scrollbarSize_);
    vertical_.setBounds(
        getWidth() - scrollbarSize_, gridArea().getY(), scrollbarSize_, gridArea().getHeight());
    layoutLanes();
    verticalScroll_ = std::clamp(verticalScroll_, 0, maxVerticalScroll());
    updateScrollbars();
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // interaction.md 10.2: the wheel scrolls up and down, Shift+wheel scrolls time,
    // Ctrl+wheel zooms time at the pointer; Ctrl+Shift+wheel (or Alt+wheel) zooms the rows.
    const float delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    if ((e.mods.isCtrlDown() && e.mods.isShiftDown()) || e.mods.isAltDown())
    {
        const double factor = 1.0 / TimelineViewport::wheelZoomFactor(delta);
        // The row under the pointer stays under it.
        const int at = e.y - gridArea().getY();
        const double row = static_cast<double>(verticalScroll_ + at) / rowHeight_;
        rowHeight_ = std::clamp(static_cast<int>(std::lround(rowHeight_ * factor)) +
                                    (factor > 1.0 && std::lround(rowHeight_ * factor) == rowHeight_   ? 1
                                     : factor < 1.0 && std::lround(rowHeight_ * factor) == rowHeight_ ? -1
                                                                                                      : 0),
                                minRowHeight,
                                maxRowHeight);
        verticalScroll_ =
            std::clamp(static_cast<int>(std::lround(row * rowHeight_)) - at, 0, maxVerticalScroll());
    }
    else if (e.mods.isCtrlDown())
    {
        const auto zoomed = view().wheelZoomed(wheel.deltaY, e.x, 1.0, 100000.0);
        samplesPerPixel_ = zoomed.samplesPerPixel;
        scroll_ = std::max<core::SamplePosition>(0, zoomed.first);
        notifyTimeView();
    }
    else if (e.mods.isShiftDown() || (wheel.deltaY == 0.0f && wheel.deltaX != 0.0f))
    {
        scroll_ = std::max<core::SamplePosition>(
            0, scroll_ - static_cast<core::SamplePosition>(delta * 200.0 * samplesPerPixel_));
        notifyTimeView();
    }
    else
    {
        verticalScroll_ =
            std::clamp(verticalScroll_ - static_cast<int>(wheel.deltaY * 120.0), 0, maxVerticalScroll());
    }
    updateScrollbars();
    repaint();
}

// ----- the toolbar ----------------------------------------------------------------------------

void PianoRoll::refreshToolbar()
{
    const auto label = juce::String(std::string(noteGrids[grid_].label));
    snapButton_.setButtonText(snap_ ? label : "Off " + label);
    velocityButton_.setButtonText("Vel " + juce::String(velocity_));
    keyButton_.setButtonText(keymenu::name(project_ != nullptr ? project_->key : core::MusicalKey{}));
    if (getWidth() > 0)
        resized();
}

void PianoRoll::showSnapMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Snap", true, snap_);
    menu.addSeparator();
    juce::PopupMenu triplets, dotted;
    for (size_t i = 0; i < noteGrids.size(); ++i)
    {
        const auto& g = noteGrids[i];
        const auto text = juce::String(std::string(g.label));
        const int id = 10 + static_cast<int>(i);
        switch (g.family)
        {
            case NoteGrid::Family::straight:
                menu.addItem(id, text, true, grid_ == i);
                break;
            case NoteGrid::Family::triplet:
                triplets.addItem(id, text, true, grid_ == i);
                break;
            case NoteGrid::Family::dotted:
                dotted.addItem(id, text, true, grid_ == i);
                break;
        }
    }
    menu.addSubMenu(
        "Triplets", triplets, true, nullptr, noteGrids[grid_].family == NoteGrid::Family::triplet);
    menu.addSubMenu("Dotted", dotted, true, nullptr, noteGrids[grid_].family == NoteGrid::Family::dotted);
    juce::Component::SafePointer<PianoRoll> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(snapButton_),
                       [safe](int choice)
                       {
                           if (safe == nullptr || choice == 0)
                               return;
                           if (choice == 1)
                               safe->setSnapEnabled(!safe->snap_);
                           else
                               safe->setGrid(static_cast<size_t>(choice - 10));
                       });
}

void PianoRoll::showQuantiseMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Quantise starts (Q)");
    menu.addItem(2, "Quantise starts and ends (Shift+Q)");
    menu.addSeparator();
    juce::PopupMenu strength, swing, humanise;
    for (const int percent : {100, 75, 50, 25})
        strength.addItem(100 + percent,
                         juce::String(percent) + "%",
                         true,
                         std::abs(quantiseStrength_ - percent / 100.0f) < 0.01f);
    swing.addItem(300, "Off (50%)", true, swing_ <= 0.0f);
    for (const int percent : {54, 58, 62, 66, 71, 75})
        swing.addItem(300 + percent,
                      juce::String(percent) + "%" + (percent == 66 ? "  triplet feel" : ""),
                      true,
                      std::abs(swing_ - noteops::swingFromPercent(static_cast<float>(percent))) < 0.005f);
    humanise.addItem(401,
                     juce::String::fromUTF8("Timing \xc2\xb1"
                                            "5 ms"));
    humanise.addItem(402,
                     juce::String::fromUTF8("Timing \xc2\xb1"
                                            "10 ms"));
    humanise.addItem(403,
                     juce::String::fromUTF8("Timing \xc2\xb1"
                                            "20 ms"));
    humanise.addSeparator();
    humanise.addItem(411,
                     juce::String::fromUTF8("Velocity \xc2\xb1"
                                            "5"));
    humanise.addItem(412,
                     juce::String::fromUTF8("Velocity \xc2\xb1"
                                            "10"));
    humanise.addSeparator();
    humanise.addItem(421,
                     juce::String::fromUTF8("Both: \xc2\xb1"
                                            "10 ms, \xc2\xb1"
                                            "8"));
    menu.addSubMenu("Strength", strength);
    menu.addSubMenu("Swing", swing, true, nullptr, swing_ > 0.0f);
    menu.addSubMenu("Humanise", humanise);
    juce::Component::SafePointer<PianoRoll> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(quantiseButton_),
        [safe](int choice)
        {
            if (safe == nullptr || choice == 0)
                return;
            const double rate = safe->project_ != nullptr ? safe->project_->sampleRate : 48000.0;
            const auto ms = [rate](double v)
            { return static_cast<core::SamplePosition>(std::llround(v * rate / 1000.0)); };
            if (choice == 1 || choice == 2)
                safe->quantiseSelection(
                    noteops::QuantiseOptions{safe->quantiseStrength_, safe->swing_, choice == 2});
            else if (choice >= 100 && choice < 300)
                safe->quantiseStrength_ = static_cast<float>(choice - 100) / 100.0f;
            else if (choice >= 300 && choice < 400)
                safe->swing_ =
                    choice == 300 ? 0.0f : noteops::swingFromPercent(static_cast<float>(choice - 300));
            else if (choice == 401 || choice == 402 || choice == 403)
                safe->humaniseSelection(ms(choice == 401 ? 5.0 : choice == 402 ? 10.0 : 20.0), 0);
            else if (choice == 411 || choice == 412)
                safe->humaniseSelection(0, choice == 411 ? 5 : 10);
            else if (choice == 421)
                safe->humaniseSelection(ms(10.0), 8);
        });
}

void PianoRoll::showVelocityMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader("New notes' velocity");
    for (const int v : {127, 110, 100, 80, 64, 48, 32})
        menu.addItem(v, juce::String(v), true, velocity_ == v);
    if (const auto* c = clip(); c != nullptr && selection_.size() == 1)
        if (const auto* n = c->findNote(selection_.front()))
        {
            menu.addSeparator();
            menu.addItem(1000 + n->velocity, "The selected note's (" + juce::String(n->velocity) + ")");
        }
    juce::Component::SafePointer<PianoRoll> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(velocityButton_),
                       [safe](int choice)
                       {
                           if (safe != nullptr && choice > 0)
                               safe->setDefaultVelocity(choice >= 1000 ? choice - 1000 : choice);
                       });
}

void PianoRoll::showKeyMenu()
{
    const auto current = project_ != nullptr ? project_->key : core::MusicalKey{};
    auto menu = keymenu::build(current, false);
    juce::Component::SafePointer<PianoRoll> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(keyButton_),
                       [safe](int choice)
                       {
                           if (safe == nullptr)
                               return;
                           const auto key = keymenu::keyFor(choice);
                           if (!key || safe->project_ == nullptr || *key == safe->project_->key)
                               return;
                           std::vector<std::unique_ptr<core::Command>> commands;
                           commands.push_back(std::make_unique<core::SetProjectKey>(*key));
                           safe->emit(std::move(commands), "Set key");
                           safe->refreshToolbar();
                           safe->repaint();
                       });
}

void PianoRoll::setDefaultVelocity(int velocity)
{
    velocity_ = std::clamp(velocity, 1, 127);
    velocityButton_.setButtonText("Vel " + juce::String(velocity_));
}

// ----- painting -------------------------------------------------------------------------------

juce::String PianoRoll::noteName(int note)
{
    return juce::String(core::noteName(note));
}

void PianoRoll::drawTooltip(juce::Graphics& g, const juce::String& text, juce::Point<int> at)
{
    paintDragTip(g,
                 theme_,
                 text,
                 at,
                 getLocalBounds().withTrimmedRight(scrollbarSize_).withTrimmedBottom(scrollbarSize_));
}

// The toolbar's background.
void PianoRoll::paintToolbar(juce::Graphics& g, const PaintFrame&)
{
    const auto& c = theme_.colours;
    g.setColour(c.surface0);
    g.fillRect(0, 0, getWidth(), toolbarHeight);
    g.setColour(c.lineSubtle);
    g.fillRect(0, toolbarHeight - 1, getWidth(), 1);
}

// The note rows (black keys, the scale, octave lines) and, over them, the timeline grid.
void PianoRoll::paintRows(juce::Graphics& g, const PaintFrame& f)
{
    const auto& c = theme_.colours;
    const auto& grid = f.grid;
    const auto& paintedGrid = f.paintedGrid;
    const auto& key = f.key;
    const auto* clipModel = f.clip;
    const bool named = namedOnly_;
    // Rows: laneAlt on the black keys (every other row when only named rows
    // show), an octave line under each C.
    for (int row = 0; row < rowsShown(); ++row)
    {
        const int note = rows_[static_cast<size_t>(row)];
        const int y = yForNote(note);
        if (y + rowHeight_ < paintedGrid.getY() || y > paintedGrid.getBottom())
            continue;
        if (named ? row % 2 == 1 : isBlackKey(note))
        {
            g.setColour(c.laneAlt);
            g.fillRect(grid.getX(), y, grid.getWidth(), rowHeight_);
        }
        if (!named && scaleHighlight_ && key.isSet() && !key.contains(note))
        {
            g.setColour(c.canvas.withAlpha(0.45f));
            g.fillRect(grid.getX(), y, grid.getWidth(), rowHeight_);
        }
        else if (!named && scaleHighlight_ && key.isSet() && note % 12 == key.root)
        {
            g.setColour(c.accentSubtle);
            g.fillRect(grid.getX(), y, grid.getWidth(), rowHeight_);
        }
        if (named || note % 12 == 0)
        {
            g.setColour(named ? c.lineSubtle : c.gridBar);
            g.fillRect(grid.getX(), y + rowHeight_ - 1, grid.getWidth(), 1);
        }
    }
    // Grid lines: bars, beats and sub-beats on the timeline, as the arrange view's.
    if (project_ != nullptr && clipModel != nullptr)
    {
        if (!paintedGrid.isEmpty())
            paintGridLines(g,
                           theme_,
                           tempoMap_,
                           timelineView(*clipModel),
                           grid.withX(paintedGrid.getX()).withWidth(paintedGrid.getWidth()));
        // The clip's end: beyond it the grid dims.
        g.setColour(c.canvas.withAlpha(0.5f));
        g.fillRect(xForSample(clipModel->length) + 1, grid.getY(), grid.getRight(), grid.getHeight());
        g.setColour(c.lineStrong);
        g.fillRect(xForSample(clipModel->length), grid.getY(), 1, grid.getHeight());
    }
}

// The track's other clips' notes at 30% of the track colour; with the ghosts
// toggle, the other MIDI tracks' notes too, each in its own track's colour,
// fainter, with a hairline in that colour so they read as another part.
void PianoRoll::paintGhosts(juce::Graphics& g, const PaintFrame& f)
{
    const auto& paintedGrid = f.paintedGrid;
    const auto* clipModel = f.clip;
    if (project_ == nullptr || clipModel == nullptr || paintedGrid.isEmpty())
        return;
    const auto paintTrack = [&](const core::Track& t, bool own)
    {
        const auto colour = own ? f.colour : colourOf(t);
        for (const auto& other : t.midiClips)
        {
            if (own && other.id == clipModel->id)
                continue;
            // A clip wholly outside what is painted has no ghost to draw.
            const int otherLeft = xForSample(other.start - clipModel->start);
            const int otherRight = xForSample(other.end() - clipModel->start);
            if (otherRight < paintedGrid.getX() || otherLeft > paintedGrid.getRight())
                continue;
            for (const auto& n : other.notes)
            {
                if (!rowShown(n.note))
                    continue;
                core::MidiNote shifted = n;
                shifted.start = other.start + n.start - clipModel->start;
                const auto bounds = noteBounds(shifted).reduced(0, 1);
                if (!bounds.intersects(paintedGrid))
                    continue;
                g.setColour(colour.withAlpha((own ? 0.3f : 0.2f) * theme_.track.waveformAlpha));
                g.fillRoundedRectangle(bounds.toFloat(), theme_.radius.sm);
                if (!own)
                {
                    g.setColour(colour.withAlpha(0.45f));
                    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
                }
            }
        }
    };
    if (ghostTracks_)
        for (const auto& t : project_->tracks)
            if (t.isMidi() && t.id != trackId_)
                paintTrack(t, false);
    if (f.track != nullptr)
        paintTrack(*f.track, true);
}

void PianoRoll::paintNote(juce::Graphics& g, const core::MidiNote& note, juce::Colour colour, bool selected)
{
    const auto& c = theme_.colours;
    const auto bounds = noteBounds(note).reduced(0, 1);
    const float t = static_cast<float>(std::clamp(note.velocity, 1, 127) - 1) / 126.0f;
    const auto fill = colour.withAlpha(theme_.track.waveformAlpha);
    if (note.muted)
    {
        // Muted: hollow, the outline in the track colour, a hairline through it.
        g.setColour(colour.withAlpha(0.12f));
        g.fillRoundedRectangle(bounds.toFloat(), theme_.radius.sm);
        g.setColour(selected ? c.textPrimary : colour.withAlpha(0.7f));
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
        g.setColour(colour.withAlpha(0.5f));
        g.fillRect(bounds.getX() + 2, bounds.getCentreY(), std::max(0, bounds.getWidth() - 4), 1);
        return;
    }
    g.setColour(fill.withAlpha(juce::jmap(
        t, c.midiNoteVelocityLow.getFloatAlpha() * theme_.track.waveformAlpha, theme_.track.waveformAlpha)));
    g.fillRoundedRectangle(bounds.toFloat(), theme_.radius.sm);
    if (selected)
    {
        g.setColour(c.selectionFill);
        g.fillRoundedRectangle(bounds.toFloat(), theme_.radius.sm);
    }
    g.setColour(selected ? c.textPrimary : c.canvas);
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
    if (bounds.getHeight() > 14 && bounds.getWidth() > 24)
    {
        g.setFont(theme_.caption());
        g.setColour(c.textOnColour);
        g.drawText(labelFor(note.note), bounds.reduced(4, 0), juce::Justification::centredLeft, true);
    }
}

// This clip's notes and the note being drawn.
void PianoRoll::paintNotes(juce::Graphics& g, const PaintFrame& f)
{
    const auto& c = theme_.colours;
    const auto& paintedGrid = f.paintedGrid;
    const auto* clipModel = f.clip;
    // Notes: the track colour, lighter with velocity, a canvas inset so neighbours
    // separate; selected notes outlined in textPrimary over a selection tint.
    if (clipModel == nullptr)
        return;
    for (const auto& model : clipModel->notes)
    {
        const auto note = previewOf(model);
        if (!rowShown(note.note))
            continue;
        if (!noteBounds(note).reduced(0, 1).intersects(paintedGrid))
            continue;
        paintstats::counters().notesDrawn++;
        paintNote(g, note, f.colour, isSelected(model.id));
    }
    if (drag_ == Drag::draw)
    {
        const auto bounds = noteBounds(drawNote_).reduced(0, 1);
        g.setColour(f.colour.withAlpha(theme_.track.waveformAlpha));
        g.fillRoundedRectangle(bounds.toFloat(), theme_.radius.sm);
        g.setColour(c.textPrimary);
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
    }
}

// Notes coming in while recording on this track, in `record` until they land in
// a clip on stop (screens/piano-roll.md "Recording").
void PianoRoll::paintLiveNotes(juce::Graphics& g, const PaintFrame& f)
{
    if (live_ == nullptr || f.clip == nullptr)
        return;
    const auto& c = theme_.colours;
    for (const auto& take : live_->midi)
    {
        if (take.trackId != trackId_)
            continue;
        for (const auto& n : take.clip.notes)
        {
            if (!rowShown(n.note))
                continue;
            core::MidiNote shifted = n;
            shifted.start = take.clip.start + n.start - f.clip->start;
            const auto bounds = noteBounds(shifted).reduced(0, 1);
            if (!bounds.intersects(f.paintedGrid))
                continue;
            g.setColour(c.record.withAlpha(0.85f));
            g.fillRoundedRectangle(bounds.toFloat(), theme_.radius.sm);
            g.setColour(c.canvas);
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
        }
    }
}

// What a drag shows over the grid (the lasso, the move's pitch tag) and the playhead.
void PianoRoll::paintGridOverlay(juce::Graphics& g, const PaintFrame& f)
{
    const auto& c = theme_.colours;
    const auto& grid = f.grid;
    if (drag_ == Drag::lasso && draggedFar_)
    {
        g.setColour(c.selectionFill);
        g.fillRect(lasso_);
        g.setColour(c.selectionOutline);
        g.drawRect(lasso_, 1);
    }
    if (playhead_ >= 0)
    {
        g.setColour(c.playhead);
        g.fillRect(xForSample(playhead_), grid.getY(), 1, grid.getHeight());
    }
    if (drag_ == Drag::move && draggedFar_)
    {
        const int pitch = std::clamp(dragOriginal_.note + pitchDelta_, 0, 127);
        drawTooltip(g,
                    labelFor(pitch) +
                        (pitchDelta_ != 0 ? "  (" + juce::String(pitchDelta_ > 0 ? "+" : "") +
                                                juce::String(pitchDelta_) + ")"
                                          : juce::String()) +
                        (duplicating_ ? "  copy" : ""),
                    dragPoint_);
    }
    if (drag_ == Drag::velocity && draggedFar_ && velocityRamp_.empty())
        drawTooltip(g,
                    velocityAbsolute_ || !isSelected(dragNote_) || selection_.size() < 2
                        ? "Velocity " + juce::String(velocityPreview_)
                        : "Velocity " + juce::String(velocityPreview_) + "  (" +
                              juce::String(velocityPreview_ - dragOriginal_.velocity >= 0 ? "+" : "") +
                              juce::String(velocityPreview_ - dragOriginal_.velocity) + " each)",
                    dragPoint_);
}

// The keyboard, its sounding and held keys lit; an instrument's named notes
// carry their names (a drum kit's pads), and with only the named rows showing
// each row is a plain key with its name.
void PianoRoll::paintKeys(juce::Graphics& g, const PaintFrame& f)
{
    const auto& c = theme_.colours;
    const auto& keys = f.keys;
    const auto& painted = f.painted;
    const auto& key = f.key;
    g.saveState();
    g.reduceClipRegion(keys);
    const auto paintedKeys = keys.getIntersection(painted);
    auto lit = soundingKeys();
    if (heldKey_ >= 0 && heldKey_ < notes)
        lit.set(static_cast<size_t>(heldKey_));
    for (int row = 0; row < rowsShown() && !paintedKeys.isEmpty(); ++row)
    {
        const int note = rows_[static_cast<size_t>(row)];
        const int y = yForNote(note);
        if (y + rowHeight_ < paintedKeys.getY() || y > paintedKeys.getBottom())
            continue;
        const bool on = lit[static_cast<size_t>(note)];
        const auto name = rowName(note);
        if (namedOnly_)
        {
            g.setColour(on ? c.accent : row % 2 == 0 ? c.surface2 : c.surface1);
            g.fillRect(0, y, keyboardWidth - 1, rowHeight_ - 1);
            if (rowHeight_ >= 9)
            {
                g.setColour(on ? c.textOnAccent : name.isNotEmpty() ? c.textPrimary : c.textTertiary);
                g.setFont(theme_.caption());
                g.drawText(name.isNotEmpty() ? name : noteName(note),
                           4,
                           y,
                           keyboardWidth - 8,
                           rowHeight_,
                           juce::Justification::centredLeft,
                           true);
            }
            continue;
        }
        const bool black = isBlackKey(note);
        g.setColour(on ? c.accent : black ? c.surface0 : c.surface2);
        g.fillRect(0, y, black ? keyboardWidth * 2 / 3 : keyboardWidth - 1, rowHeight_ - 1);
        // The scale: a stripe on the keys in the project key, the root named.
        const bool root = scaleHighlight_ && key.isSet() && note % 12 == key.root;
        if (scaleHighlight_ && key.isSet() && key.contains(note) && !on)
        {
            g.setColour(root ? c.accent : c.accent.withAlpha(0.45f));
            g.fillRect(keyboardWidth - 5, y, 3, rowHeight_ - 1);
        }
        if (rowHeight_ < 9)
            continue; // too short to label
        if (name.isNotEmpty())
        {
            // An instrument's name for the note, over the key.
            g.setColour(on ? c.textOnAccent : c.textPrimary);
            g.setFont(theme_.caption());
            g.drawText(name, 4, y, keyboardWidth - 10, rowHeight_, juce::Justification::centredLeft, true);
        }
        else if (note % 12 == 0 || root)
        {
            g.setColour(on ? c.textOnAccent : root ? c.accent : c.textSecondary);
            g.setFont(theme_.caption());
            g.drawText(
                noteName(note), 4, y, keyboardWidth - 10, rowHeight_, juce::Justification::centredRight);
        }
    }
    g.setColour(c.lineSubtle);
    g.fillRect(keyboardWidth - 1, keys.getY(), 1, keys.getHeight());
    g.restoreState();
}

// The bars ruler over the grid.
void PianoRoll::paintRuler(juce::Graphics& g, const PaintFrame& f)
{
    const auto& c = theme_.colours;
    const auto& ruler = f.ruler;
    const auto& painted = f.painted;
    const auto* clipModel = f.clip;
    // Ruler: bar numbers and beat ticks at timeline positions.
    g.setColour(c.surface0);
    g.fillRect(0, toolbarHeight, getWidth(), rulerHeight_);
    g.setColour(c.lineSubtle);
    g.fillRect(0, toolbarHeight + rulerHeight_ - 1, getWidth(), 1);
    if (project_ != nullptr && clipModel != nullptr && ruler.intersects(painted))
    {
        g.saveState();
        g.reduceClipRegion(ruler);
        paintBarsRuler(g, theme_, tempoMap_, timelineView(*clipModel), ruler.withTrimmedBottom(1));
        g.restoreState();
    }
}

void PianoRoll::paintVelocityLane(juce::Graphics& g, const PaintFrame& f, juce::Rectangle<int> velocity)
{
    const auto& c = theme_.colours;
    const auto* clipModel = f.clip;
    if (clipModel == nullptr || !velocity.intersects(f.painted))
        return;
    g.saveState();
    g.reduceClipRegion(velocity);
    const auto paintedLane = velocity.getIntersection(f.painted);
    for (const auto& model : clipModel->notes)
    {
        const auto note = previewOf(model);
        const int x = xForSample(note.start);
        if (x < paintedLane.getX() - 6 || x > paintedLane.getRight())
            continue;
        const int h = (velocity.getHeight() - 10) * note.velocity / 127;
        const bool selected = isSelected(model.id);
        const auto colour = note.muted ? c.textTertiary : f.colour;
        g.setColour(colour.withAlpha(selected ? 1.0f : 0.7f));
        g.fillRect(x + 2, velocity.getBottom() - 4 - h, 2, h);
        g.fillRect(x, velocity.getBottom() - 4 - h - 3, 6, 6);
        if (selected)
        {
            g.setColour(c.textPrimary);
            g.drawRect(x, velocity.getBottom() - 4 - h - 3, 6, 6, 1);
        }
    }
    g.restoreState();
}

// The lanes: each its label and menu chevron, a rule at its top edge (which
// drags its height, lit while hovered or dragged), and the velocity bars (a
// controller lane is a child).
void PianoRoll::paintLanes(juce::Graphics& g, const PaintFrame& f)
{
    const auto& c = theme_.colours;
    for (size_t i = 0; i < lanes_.size(); ++i)
    {
        const auto area = laneArea(i);
        if (!area.withX(0).withWidth(getWidth()).intersects(f.painted))
            continue;
        const auto& l = lanes_[i];
        g.setColour(c.surface0);
        g.fillRect(area.withX(0).withWidth(getWidth()));
        const bool edgeLit = hoverLaneEdge_ == static_cast<int>(i) || resizingLane_ == static_cast<int>(i);
        g.setColour(edgeLit ? c.accent : c.lineSubtle);
        g.fillRect(0, area.getY() - (edgeLit ? 1 : 0), getWidth(), edgeLit ? 2 : 1);
        auto label = laneLabelArea(i);
        const int chevron = 10;
        g.setColour(focusLane_ == static_cast<int>(i) ? c.textSecondary : c.textTertiary);
        g.setFont(theme_.captionCaps());
        g.drawText(laneName(l.lane, true),
                   label.withTrimmedRight(theme_.spacing.sm + chevron),
                   juce::Justification::centredRight);
        Icons::draw(g,
                    Icon::chevronDown,
                    label.removeFromRight(theme_.spacing.sm + chevron)
                        .withSizeKeepingCentre(chevron, chevron)
                        .toFloat(),
                    c.textTertiary);
        if (!l.lane.velocity)
        {
            g.setColour(c.textTertiary);
            g.setFont(theme_.caption());
            const int low = core::midiControllerMinimum(l.lane.type),
                      high = core::midiControllerMaximum(l.lane.type);
            if (area.getHeight() >= 2 * theme_.sizes.controlCompact)
            {
                g.drawText(juce::String(high > 127 ? "+" : "") + juce::String(high),
                           0,
                           std::max(yForValue(i, high) - 2, laneLabelArea(i).getBottom()),
                           keyboardWidth - theme_.spacing.sm,
                           12,
                           juce::Justification::centredRight);
                g.drawText(juce::String(low),
                           0,
                           yForValue(i, low) - 10,
                           keyboardWidth - theme_.spacing.sm,
                           12,
                           juce::Justification::centredRight);
            }
        }
        else
            paintVelocityLane(g, f, area.withTrimmedTop(1));
    }
}

void PianoRoll::paintOverChildren(juce::Graphics& g)
{
    // Keyboard focus on the editor as a whole: the ring inside its canvas.
    if (auto* lnf = dynamic_cast<NorthlightLookAndFeel*>(&getLookAndFeel()))
        lnf->drawRegionFocusRing(g, *this, gridArea());
}

void PianoRoll::paint(juce::Graphics& g)
{
    g.fillAll(theme_.colours.canvas);
    // Only what the repaint covers is drawn (a playhead frame is a column and a few keys).
    PaintFrame f;
    f.painted = g.getClipBounds();
    f.grid = gridArea();
    f.paintedGrid = f.grid.getIntersection(f.painted);
    f.lanes = lanesArea();
    f.ruler = rulerArea();
    f.keys = keysArea();
    f.clip = clip();
    f.track = track();
    f.colour = trackColour();
    f.key = project_ != nullptr ? project_->key : core::MusicalKey{};
    paintToolbar(g, f);
    g.saveState();
    g.reduceClipRegion(f.grid);
    paintRows(g, f);
    paintGhosts(g, f);
    paintNotes(g, f);
    paintLiveNotes(g, f);
    paintGridOverlay(g, f);
    g.restoreState();
    paintKeys(g, f);
    paintRuler(g, f);
    paintLanes(g, f);
    if (f.clip == nullptr)
    {
        g.setColour(theme_.colours.textTertiary);
        g.setFont(theme_.body());
        g.drawText("Select a MIDI clip to edit its notes", f.grid, juce::Justification::centred);
    }
}

// ----- mouse ----------------------------------------------------------------------------------

void PianoRoll::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    draggedFar_ = false;
    duplicating_ = false;
    dragStart_ = e.getPosition();
    dragPoint_ = e.getPosition();
    const auto* c = clip();
    if (c == nullptr)
        return;

    // The middle button pans.
    if (e.mods.isMiddleButtonDown())
    {
        drag_ = Drag::pan;
        panScroll_ = scroll_;
        panVertical_ = verticalScroll_;
        return;
    }
    // A lane's top edge drags its height.
    if (const int edge = laneEdgeAt(e.getPosition()); edge >= 0 && !e.mods.isPopupMenu())
    {
        drag_ = Drag::laneHeight;
        resizingLane_ = edge;
        resizeStartHeight_ = lanes_[static_cast<size_t>(edge)].height;
        repaint();
        return;
    }
    // A key on the keyboard previews the note.
    if (keysArea().contains(e.getPosition()))
    {
        heldKey_ = noteForY(e.y);
        drag_ = Drag::key;
        if (onPreviewNote)
            onPreviewNote(trackId_, heldKey_, true);
        repaint();
        return;
    }

    const int lane = laneIndexAt(e.getPosition());
    focusLane_ = lane;
    if (lane >= 0 && laneLabelArea(static_cast<size_t>(lane)).contains(e.getPosition()))
    {
        showLaneMenu(static_cast<size_t>(lane));
        return;
    }
    if (const int velocityLane = velocityLaneAt(e.getPosition()); velocityLane >= 0)
    {
        velocityRamp_.clear();
        if (const auto* note = noteAtVelocityBar(e.getPosition(), velocityLane))
        {
            dragNote_ = note->id;
            dragOriginal_ = *note;
            velocityPreview_ = note->velocity;
            velocityLane_ = velocityLane;
            velocityAbsolute_ = e.mods.isAltDown();
            if (!isSelected(note->id))
            {
                selection_.clear();
                selection_.push_back(note->id);
                selectionChanged();
            }
            drag_ = Drag::velocity;
            repaint();
        }
        return;
    }
    if (!gridArea().contains(e.getPosition()))
        return;
    bool onEdge = false;
    const auto* note = noteAt(e.getPosition(), onEdge);
    if (e.mods.isPopupMenu())
    {
        // Right-click: a note not yet selected becomes the selection; then the menu.
        if (note != nullptr && !isSelected(note->id))
        {
            selection_ = {note->id};
            selectionChanged();
        }
        repaint();
        showNoteMenu(e.getPosition());
        return;
    }
    switch (tool_)
    {
        case Tool::erase:
            drag_ = Drag::erase;
            eraseAt(e.getPosition());
            return;
        case Tool::mute:
            drag_ = Drag::mute;
            swept_.clear();
            muteTo_ = note == nullptr || !note->muted;
            muteAt(e.getPosition());
            return;
        case Tool::split:
            splitAt(e.getPosition());
            return;
        case Tool::glue:
            glueAt(e.getPosition());
            return;
        case Tool::draw:
            if (note == nullptr)
            {
                drawNote_ = {};
                drawNote_.note = noteForY(e.y);
                drawNote_.velocity = velocity_;
                drawNote_.start = snapped(sampleForX(e.x));
                drawNote_.length = lastLength_ > 0 ? lastLength_ : gridSamples();
                drag_ = Drag::draw;
                auditionNote(drawNote_.note);
                repaint();
                return;
            }
            break;
        case Tool::select:
            break;
    }
    if (note == nullptr)
    {
        drag_ = Drag::lasso;
        lasso_ = juce::Rectangle<int>(e.x, e.y, 0, 0);
        return;
    }
    dragNote_ = note->id;
    dragOriginal_ = *note;
    const auto before = selection_;
    if (e.mods.isShiftDown())
    {
        if (!isSelected(note->id))
            selection_.push_back(note->id);
    }
    else if (e.mods.isCtrlDown())
    {
        // Ctrl+click toggles on release; Ctrl+drag duplicates.
        if (!isSelected(note->id))
            selection_.push_back(note->id);
        duplicating_ = true;
    }
    else if (!isSelected(note->id))
    {
        selection_.clear();
        selection_.push_back(note->id);
    }
    if (selection_ != before)
        selectionChanged();
    drag_ = onEdge ? Drag::resize : Drag::move;
    moveDelta_ = 0;
    pitchDelta_ = 0;
    lengthPreview_ = note->length;
    if (drag_ == Drag::move)
        auditionNote(note->note);
    repaint();
}

void PianoRoll::mouseDrag(const juce::MouseEvent& e)
{
    if (drag_ == Drag::none)
        return;
    draggedFar_ = draggedFar_ || e.mouseWasDraggedSinceMouseDown();
    dragPoint_ = e.getPosition();
    const auto* c = clip();
    if (c == nullptr)
        return;
    const int x = std::clamp(e.x, keyboardWidth, getWidth() - scrollbarSize_);
    const auto at = sampleForX(x);
    switch (drag_)
    {
        case Drag::pan:
            scroll_ = panScroll_ - static_cast<core::SamplePosition>((e.x - dragStart_.x) * samplesPerPixel_);
            verticalScroll_ = std::clamp(panVertical_ - (e.y - dragStart_.y), 0, maxVerticalScroll());
            updateScrollbars();
            break;
        case Drag::laneHeight:
            // Dragging the edge up makes the lane taller (the grid gives the room).
            setLaneHeight(static_cast<size_t>(resizingLane_), resizeStartHeight_ - (e.y - dragStart_.y));
            break;
        case Drag::key:
        {
            const int key = noteForY(e.y);
            if (key != heldKey_)
            {
                if (onPreviewNote)
                {
                    onPreviewNote(trackId_, heldKey_, false);
                    onPreviewNote(trackId_, key, true);
                }
                heldKey_ = key;
            }
            break;
        }
        case Drag::erase:
            eraseAt(e.getPosition());
            break;
        case Drag::mute:
            muteAt(e.getPosition());
            break;
        case Drag::draw:
            drawNote_.length = std::max(minNoteLength, snapped(at) - drawNote_.start);
            if (drawNote_.length < gridSamples() && snap_)
                drawNote_.length = gridSamples();
            break;
        case Drag::move:
        {
            const auto wanted = dragOriginal_.start + (at - sampleForX(dragStart_.x));
            moveDelta_ = snapped(wanted) - dragOriginal_.start;
            core::SamplePosition earliest = dragOriginal_.start;
            int lowest = dragOriginal_.note, highest = dragOriginal_.note;
            for (const auto id : selection_)
                if (const auto* n = c->findNote(id))
                {
                    earliest = std::min(earliest, n->start);
                    lowest = std::min(lowest, n->note);
                    highest = std::max(highest, n->note);
                }
            moveDelta_ = std::max(moveDelta_, -earliest);
            pitchDelta_ = std::clamp(noteForY(e.y) - dragOriginal_.note, -lowest, 127 - highest);
            if (e.mods.isShiftDown()) // constrain to one axis: whichever moved more
            {
                if (std::abs(e.x - dragStart_.x) > std::abs(e.y - dragStart_.y))
                    pitchDelta_ = 0;
                else
                    moveDelta_ = 0;
            }
            auditionNote(dragOriginal_.note + pitchDelta_); // only sounds when the pitch changes
            break;
        }
        case Drag::resize:
        {
            const auto newEnd = std::max(snapped(at), dragOriginal_.start + minNoteLength);
            lengthPreview_ = newEnd - dragOriginal_.start;
            break;
        }
        case Drag::lasso:
            lasso_ = juce::Rectangle<int>::leftTopRightBottom(std::min(dragStart_.x, x),
                                                              std::min(dragStart_.y, e.y),
                                                              std::max(dragStart_.x, x),
                                                              std::max(dragStart_.y, e.y));
            break;
        case Drag::velocity:
        {
            const auto area = laneArea(static_cast<size_t>(std::max(0, velocityLane_))).withTrimmedTop(1);
            const auto velocityFor = [&](int y)
            {
                return std::clamp(
                    (area.getBottom() - 4 - y) * 127 / std::max(1, area.getHeight() - 10), 1, 127);
            };
            velocityAbsolute_ = e.mods.isAltDown();
            velocityPreview_ = velocityFor(e.y);
            // Dragging across bars ramps the notes between the start and the pointer.
            velocityRamp_.clear();
            const int x0 = std::min(dragStart_.x, e.x), x1 = std::max(dragStart_.x, e.x);
            if (x1 - x0 > 8)
            {
                const int v0 = velocityFor(dragStart_.y), v1 = velocityPreview_;
                for (const auto& n : c->notes)
                {
                    const int nx = xForSample(n.start) + 3;
                    if (nx < x0 || nx > x1)
                        continue;
                    const double t =
                        static_cast<double>(nx - dragStart_.x) / static_cast<double>(e.x - dragStart_.x);
                    velocityRamp_[n.id] = std::clamp(
                        static_cast<int>(std::lround(v0 + (v1 - v0) * std::clamp(t, 0.0, 1.0))), 1, 127);
                }
            }
            break;
        }
        case Drag::none:
            break;
    }
    repaint();
}

void PianoRoll::mouseUp(const juce::MouseEvent& e)
{
    const auto drag = drag_;
    drag_ = Drag::none;
    stopAudition();
    const auto before = selection_;
    switch (drag)
    {
        case Drag::pan:
            break;
        case Drag::laneHeight:
            resizingLane_ = -1;
            break;
        case Drag::key:
            if (onPreviewNote && heldKey_ >= 0)
                onPreviewNote(trackId_, heldKey_, false);
            heldKey_ = -1;
            break;
        case Drag::erase:
            break;
        case Drag::mute:
            finishMuteSweep();
            break;
        case Drag::draw:
            finishDraw();
            break;
        case Drag::move:
            if (draggedFar_)
                finishMove(duplicating_);
            else if (e.mods.isCtrlDown())
                std::erase(selection_, dragNote_);
            else if (!e.mods.isShiftDown())
            {
                selection_.clear();
                selection_.push_back(dragNote_);
            }
            break;
        case Drag::resize:
            if (draggedFar_)
                finishResize();
            break;
        case Drag::lasso:
            if (draggedFar_)
                finishLasso();
            else if (!e.mods.isShiftDown())
                selection_.clear();
            break;
        case Drag::velocity:
            if (draggedFar_)
                finishVelocity(velocityAbsolute_);
            break;
        case Drag::none:
            break;
    }
    duplicating_ = false;
    velocityRamp_.clear();
    velocityLane_ = -1;
    swept_.clear();
    if (selection_ != before)
        selectionChanged();
    repaint();
}

void PianoRoll::mouseMove(const juce::MouseEvent& e)
{
    if (drag_ != Drag::none)
        return;
    if (gridArea().contains(e.getPosition()))
        pointer_ = std::max<core::SamplePosition>(0, sampleForX(e.x));
    const int edge = laneEdgeAt(e.getPosition());
    if (edge != hoverLaneEdge_)
    {
        hoverLaneEdge_ = edge;
        repaint(0, lanesArea().getY() - 4, getWidth(), lanesArea().getHeight() + 8);
    }
    if (edge >= 0)
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        return;
    }
    bool onEdge = false;
    const auto* note = noteAt(e.getPosition(), onEdge);
    // The pencil on a note and the select tool move and resize it; the other
    // tools keep their cursors everywhere.
    if (tool_ == Tool::erase || tool_ == Tool::split || tool_ == Tool::glue || tool_ == Tool::mute ||
        (tool_ == Tool::draw && note == nullptr))
        setMouseCursor(toolCursor());
    else
        setMouseCursor(note == nullptr ? juce::MouseCursor::NormalCursor
                       : onEdge        ? juce::MouseCursor::LeftRightResizeCursor
                                       : juce::MouseCursor::DraggingHandCursor);
    const auto id = note != nullptr ? note->id : core::invalidId;
    if (id != hoverNote_ || onEdge != hoverEdge_)
    {
        hoverNote_ = id;
        hoverEdge_ = onEdge;
        repaint();
    }
}

void PianoRoll::mouseExit(const juce::MouseEvent&)
{
    if (hoverNote_ != core::invalidId || hoverLaneEdge_ >= 0)
    {
        hoverNote_ = core::invalidId;
        hoverLaneEdge_ = -1;
        repaint();
    }
}

void PianoRoll::mouseDoubleClick(const juce::MouseEvent& e)
{
    // In Select mode a double-click on empty grid adds a note (in a controller
    // lane, a point).
    if (tool_ != Tool::select || !gridArea().contains(e.getPosition()))
        return;
    bool onEdge = false;
    if (noteAt(e.getPosition(), onEdge) == nullptr)
        addNoteAt(e.getPosition(), lastLength_ > 0 ? lastLength_ : gridSamples());
}

// ----- edits ----------------------------------------------------------------------------------

void PianoRoll::emit(std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
{
    if (!commands.empty() && onEdit)
        onEdit(std::move(commands), name);
}

void PianoRoll::emitEdit(const noteops::NoteEdit& edit, const std::string& name, bool selectAdded)
{
    if (edit.empty())
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    std::vector<const core::AddNote*> added;
    for (const auto& n : edit.set)
        commands.push_back(std::make_unique<core::SetNote>(trackId_, clipId_, n));
    for (const auto id : edit.remove)
        commands.push_back(std::make_unique<core::RemoveNote>(trackId_, clipId_, id));
    for (const auto& n : edit.add)
    {
        auto add = std::make_unique<core::AddNote>(trackId_, clipId_, n);
        added.push_back(add.get());
        commands.push_back(std::move(add));
    }
    for (const auto id : edit.remove)
        std::erase(selection_, id);
    emit(std::move(commands), name);
    // The commands allocated the ids when they were applied (the owner executed them).
    if (selectAdded && !added.empty())
    {
        selection_.clear();
        for (const auto* a : added)
            if (a->noteId() != core::invalidId)
                selection_.push_back(a->noteId());
    }
    selectionChanged();
    repaint();
}

std::vector<core::Id> PianoRoll::targets() const
{
    if (!selection_.empty())
        return selection_;
    std::vector<core::Id> all;
    if (const auto* c = clip())
        for (const auto& n : c->notes)
            all.push_back(n.id);
    return all;
}

void PianoRoll::eraseAt(juce::Point<int> p)
{
    bool onEdge = false;
    if (const auto* note = noteAt(p, onEdge))
    {
        std::vector<std::unique_ptr<core::Command>> commands;
        commands.push_back(std::make_unique<core::RemoveNote>(trackId_, clipId_, note->id));
        if (isSelected(note->id))
        {
            std::erase(selection_, note->id);
            selectionChanged();
        }
        emit(std::move(commands), "Erase note");
    }
}

void PianoRoll::muteAt(juce::Point<int> p)
{
    bool onEdge = false;
    if (const auto* note = noteAt(p, onEdge);
        note != nullptr && std::find(swept_.begin(), swept_.end(), note->id) == swept_.end())
    {
        swept_.push_back(note->id);
        repaint();
    }
}

void PianoRoll::finishMuteSweep()
{
    const auto* c = clip();
    if (c == nullptr || swept_.empty())
        return;
    noteops::NoteEdit edit;
    for (const auto id : swept_)
        if (const auto* n = c->findNote(id); n != nullptr && n->muted != muteTo_)
        {
            auto changed = *n;
            changed.muted = muteTo_;
            edit.set.push_back(changed);
        }
    emitEdit(edit, muteTo_ ? "Mute notes" : "Unmute notes");
}

void PianoRoll::splitAt(juce::Point<int> p)
{
    const auto* c = clip();
    bool onEdge = false;
    const auto* note = noteAt(p, onEdge);
    if (c == nullptr || note == nullptr)
        return;
    // A selected note splits the selection's notes there too.
    const auto at = snapped(sampleForX(p.x));
    const auto ids = isSelected(note->id) ? selection_ : std::vector<core::Id>{note->id};
    emitEdit(noteops::split(*c, ids, at, minNoteLength), "Split notes");
}

void PianoRoll::glueAt(juce::Point<int> p)
{
    const auto* c = clip();
    bool onEdge = false;
    const auto* note = noteAt(p, onEdge);
    if (c == nullptr || note == nullptr)
        return;
    std::vector<core::Id> ids;
    if (isSelected(note->id) && selection_.size() > 1)
        ids = selection_;
    else
    {
        // The note and the next one of its pitch.
        ids.push_back(note->id);
        const core::MidiNote* next = nullptr;
        for (const auto& n : c->notes)
            if (n.note == note->note && n.id != note->id && n.start >= note->start &&
                (next == nullptr || n.start < next->start))
                next = &n;
        if (next == nullptr)
            return;
        ids.push_back(next->id);
    }
    emitEdit(noteops::join(*c, ids), "Join notes");
}

void PianoRoll::addNoteAt(juce::Point<int> p, core::SamplePosition length)
{
    const auto* c = clip();
    if (c == nullptr || !gridArea().contains(p))
        return;
    core::MidiNote note;
    note.note = noteForY(p.y);
    note.velocity = velocity_;
    note.start = snapped(sampleForX(p.x));
    note.length = std::max(minNoteLength, length);
    if (note.start >= c->length)
        return;
    addNote(note, "Add note");
}

void PianoRoll::addNote(const core::MidiNote& note, const std::string& name)
{
    std::vector<std::unique_ptr<core::Command>> commands;
    auto add = std::make_unique<core::AddNote>(trackId_, clipId_, note);
    const auto* raw = add.get();
    commands.push_back(std::move(add));
    emit(std::move(commands), name);
    // The command allocated the id when it was applied (the owner executed it).
    selection_.clear();
    if (raw != nullptr && raw->noteId() != core::invalidId)
        selection_.push_back(raw->noteId());
    selectionChanged();
}

void PianoRoll::insertNote()
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    const core::MidiNote* after = nullptr;
    for (const auto id : selection_)
        if (const auto* n = c->findNote(id); n != nullptr && (after == nullptr || n->start > after->start))
            after = n;
    core::MidiNote note;
    if (after != nullptr)
    {
        note.note = after->note;
        note.velocity = after->velocity;
        note.start = after->start + after->length;
        note.length = after->length;
    }
    else
    {
        note.note = noteForY(gridArea().getCentreY());
        note.velocity = velocity_;
        note.start = snapped(playhead_ >= 0 ? playhead_ : scroll_);
        note.length = gridSamples(note.start);
    }
    note.length = std::max(minNoteLength, note.length);
    if (note.start >= c->length)
        return;
    addNote(note, "Add note");
    // Keep it in view.
    const auto grid = gridArea();
    const int x = xForSample(note.start);
    if (x < grid.getX() || x > grid.getRight() - 16)
    {
        scroll_ = std::max<core::SamplePosition>(
            0, note.start - static_cast<core::SamplePosition>(grid.getWidth() / 4.0 * samplesPerPixel_));
        notifyTimeView();
        updateScrollbars();
    }
    auditionNote(note.note);
    stopAudition();
    repaint();
}

void PianoRoll::changeVelocity(int delta)
{
    if (const auto* c = clip(); c != nullptr && !selection_.empty() && delta != 0)
        emitEdit(noteops::offsetVelocity(*c, selection_, delta), "Change velocity");
}

bool PianoRoll::showKeyboardMenu()
{
    const auto* c = clip();
    if (c == nullptr)
        return false;
    auto at = gridArea().getCentre();
    if (!selection_.empty())
        if (const auto* n = c->findNote(selection_.front()))
            if (const auto shown = noteBounds(*n).getIntersection(gridArea()); !shown.isEmpty())
                at = shown.getCentre();
    showNoteMenu(at);
    return true;
}

void PianoRoll::finishDraw()
{
    const auto* c = clip();
    if (c == nullptr || drawNote_.start >= c->length)
        return;
    lastLength_ = drawNote_.length;
    std::vector<std::unique_ptr<core::Command>> commands;
    auto add = std::make_unique<core::AddNote>(trackId_, clipId_, drawNote_);
    const auto* raw = add.get();
    commands.push_back(std::move(add));
    emit(std::move(commands), "Draw note");
    selection_.clear();
    if (raw != nullptr && raw->noteId() != core::invalidId)
        selection_.push_back(raw->noteId());
}

void PianoRoll::finishMove(bool duplicate)
{
    const auto* c = clip();
    if (c == nullptr || (moveDelta_ == 0 && pitchDelta_ == 0))
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    for (const auto id : selection_)
        if (const auto* n = c->findNote(id))
        {
            auto moved = *n;
            moved.start = std::max<core::SamplePosition>(0, n->start + moveDelta_);
            moved.note = std::clamp(n->note + pitchDelta_, 0, 127);
            if (!rowShown(moved.note))
                moved.note = n->note;
            if (duplicate)
            {
                moved.id = core::invalidId;
                commands.push_back(std::make_unique<core::AddNote>(trackId_, clipId_, moved));
            }
            else
                commands.push_back(std::make_unique<core::SetNote>(trackId_, clipId_, moved));
        }
    emit(std::move(commands),
         duplicate             ? "Duplicate notes"
         : commands.size() > 1 ? "Move notes"
                               : "Move note");
    rebuildRows(); // a note moved to a row only shown for it
}

void PianoRoll::finishResize()
{
    const auto* c = clip();
    const auto* n = c != nullptr ? c->findNote(dragNote_) : nullptr;
    if (n == nullptr || lengthPreview_ == n->length)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    // Every selected note takes the same new length when the dragged one is selected.
    const bool all = isSelected(dragNote_);
    for (const auto& note : c->notes)
        if (note.id == dragNote_ || (all && isSelected(note.id)))
        {
            auto resized = note;
            resized.length = lengthPreview_;
            commands.push_back(std::make_unique<core::SetNote>(trackId_, clipId_, resized));
        }
    lastLength_ = lengthPreview_;
    emit(std::move(commands), "Resize notes");
}

void PianoRoll::finishVelocity(bool absolute)
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    noteops::NoteEdit edit;
    if (!velocityRamp_.empty())
    {
        for (const auto& note : c->notes)
            if (const auto it = velocityRamp_.find(note.id);
                it != velocityRamp_.end() && it->second != note.velocity)
            {
                auto changed = note;
                changed.velocity = it->second;
                edit.set.push_back(changed);
            }
    }
    else
        // The selection moves with the dragged bar (Alt: all to its value).
        edit = noteops::dragVelocity(*c, selection_, dragNote_, velocityPreview_, absolute);
    velocity_ = velocityPreview_; // new notes take the last velocity edited
    refreshToolbar();
    emitEdit(edit, "Set velocity");
}

void PianoRoll::finishLasso()
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    for (const auto& n : c->notes)
        if (rowShown(n.note) && noteBounds(n).intersects(lasso_) && !isSelected(n.id))
            selection_.push_back(n.id);
}

void PianoRoll::setSelection(std::vector<core::Id> ids)
{
    selection_ = std::move(ids);
    selectionChanged();
    repaint();
}

void PianoRoll::selectAll()
{
    selection_.clear();
    if (const auto* c = clip())
        for (const auto& n : c->notes)
            selection_.push_back(n.id);
    selectionChanged();
    repaint();
}

void PianoRoll::clearSelection()
{
    selection_.clear();
    selectionChanged();
    repaint();
}

void PianoRoll::deleteSelection()
{
    const auto* c = clip();
    if (c == nullptr || selection_.empty())
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    for (const auto id : selection_)
        if (c->findNote(id) != nullptr)
            commands.push_back(std::make_unique<core::RemoveNote>(trackId_, clipId_, id));
    selection_.clear();
    selectionChanged();
    emit(std::move(commands), "Delete notes");
}

void PianoRoll::duplicateSelection()
{
    // Copies land right after the selection's span (whole grid steps), keeping their spacing.
    const auto* c = clip();
    if (c == nullptr || selection_.empty())
        return;
    const auto copied = noteops::copy(*c, selection_, gridSamples());
    if (copied.empty())
        return;
    const auto first = noteops::selected(*c, selection_).front().start;
    emitEdit(noteops::paste(*c, copied, first + copied.span), "Duplicate notes", true);
}

noteops::Clipboard& PianoRoll::clipboard()
{
    // One for every piano roll: copy in one clip, paste in another.
    static noteops::Clipboard notesClipboard;
    return notesClipboard;
}

bool PianoRoll::clipboardEmpty() const noexcept
{
    return clipboard().empty();
}

void PianoRoll::copySelection()
{
    if (const auto* c = clip(); c != nullptr && !selection_.empty())
        clipboard() = noteops::copy(*c, selection_, gridSamples());
}

void PianoRoll::cutSelection()
{
    copySelection();
    if (selection_.empty())
        return;
    const auto* c = clip();
    noteops::NoteEdit edit;
    for (const auto id : selection_)
        if (c != nullptr && c->findNote(id) != nullptr)
            edit.remove.push_back(id);
    emitEdit(edit, "Cut notes");
}

void PianoRoll::paste()
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    const auto at = playhead_ >= 0 && playhead_ < c->length ? playhead_
                    : pointer_.has_value()                  ? *pointer_
                                                            : std::max<core::SamplePosition>(0, scroll_);
    pasteAt(snapped(at));
}

void PianoRoll::pasteAt(core::SamplePosition clipRelative)
{
    const auto* c = clip();
    if (c == nullptr || clipboard().empty())
        return;
    emitEdit(noteops::paste(*c, clipboard(), clipRelative), "Paste notes", true);
}

void PianoRoll::quantiseSelection(float strength, bool ends)
{
    quantiseSelection(noteops::QuantiseOptions{strength, swing_, ends});
}

void PianoRoll::quantiseSelection(const noteops::QuantiseOptions& options)
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    emitEdit(noteops::quantise(*c, selection_, tempoMap_, noteGrids[grid_], samplesPerPixel_, options),
             "Quantise notes");
}

void PianoRoll::humaniseSelection(core::SamplePosition timing, int velocity)
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    emitEdit(noteops::humanise(*c, targets(), timing, velocity, nextSeed()), "Humanise notes");
}

void PianoRoll::nudgeSelection(int timeSteps, int semitones, bool fine)
{
    const auto* c = clip();
    if (c == nullptr || selection_.empty())
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    // A grid step (a quarter of one when fine) on the tempo track from the first
    // selected note.
    core::SamplePosition from = -1;
    for (const auto id : selection_)
        if (const auto* n = c->findNote(id))
            from = from < 0 ? n->start : std::min(from, n->start);
    auto grid = gridStep(std::max<core::SamplePosition>(0, from));
    if (grid.bars > 0) // a bar grid nudges by a beat
        grid = {0,
                core::TempoMap::quartersPerBeat(
                    tempoMap_.signatureAt(c->start + std::max<core::SamplePosition>(0, from)))};
    if (fine)
        grid.quarters /= 4.0;
    const auto origin = c->start + std::max<core::SamplePosition>(0, from);
    const auto step = timeSteps == 0 ? 0 : tempoMap_.move(origin, grid, timeSteps) - origin;
    // The selection keeps its shape inside 0..127.
    int lowest = 127, highest = 0;
    for (const auto id : selection_)
        if (const auto* n = c->findNote(id))
        {
            lowest = std::min(lowest, n->note);
            highest = std::max(highest, n->note);
        }
    const int shift = std::clamp(semitones, -lowest, 127 - highest);
    for (const auto id : selection_)
        if (const auto* n = c->findNote(id))
        {
            auto moved = *n;
            moved.start = std::max<core::SamplePosition>(0, n->start + step);
            moved.note = n->note + shift;
            commands.push_back(std::make_unique<core::SetNote>(trackId_, clipId_, moved));
        }
    emit(std::move(commands), semitones != 0 && timeSteps == 0 ? "Transpose notes" : "Nudge notes");
    rebuildRows();
}

void PianoRoll::transposeSelection(int semitones)
{
    nudgeSelection(0, semitones);
}

void PianoRoll::stepSelection(int direction, bool extend)
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    const auto next = noteops::stepSelection(*c, selection_, direction);
    if (next == core::invalidId)
        return;
    if (!extend)
        selection_.clear();
    if (!isSelected(next))
        selection_.push_back(next);
    // Keep it in view, and sound it.
    if (const auto* n = c->findNote(next))
    {
        const auto grid = gridArea();
        const auto bounds = noteBounds(*n);
        if (bounds.getX() < grid.getX() || bounds.getX() > grid.getRight() - 16)
        {
            scroll_ = std::max<core::SamplePosition>(
                0, n->start - static_cast<core::SamplePosition>(grid.getWidth() / 4.0 * samplesPerPixel_));
            notifyTimeView();
        }
        if (bounds.getY() < grid.getY() || bounds.getBottom() > grid.getBottom())
            verticalScroll_ =
                std::clamp(rowOf_[static_cast<size_t>(n->note)] * rowHeight_ - grid.getHeight() / 2,
                           0,
                           maxVerticalScroll());
        updateScrollbars();
        auditionNote(n->note);
        stopAudition();
    }
    selectionChanged();
    repaint();
}

void PianoRoll::changeLength(int steps, bool fine)
{
    const auto* c = clip();
    if (c == nullptr || selection_.empty() || steps == 0)
        return;
    auto step = gridSamples(noteops::selected(*c, selection_).front().start);
    if (fine)
        step = std::max<core::SamplePosition>(1, step / 4);
    emitEdit(noteops::addLength(*c, selection_, step * steps, std::max<core::SamplePosition>(1, step / 4)),
             steps > 0 ? "Lengthen notes" : "Shorten notes");
}

void PianoRoll::legatoSelection()
{
    if (const auto* c = clip())
        emitEdit(noteops::legato(*c, targets()), "Legato");
}

void PianoRoll::toggleMuteSelection()
{
    if (const auto* c = clip(); c != nullptr && !selection_.empty())
    {
        const auto edit = noteops::toggleMute(*c, selection_);
        emitEdit(edit, !edit.set.empty() && edit.set.front().muted ? "Mute notes" : "Unmute notes");
    }
}

void PianoRoll::splitSelectionAt(core::SamplePosition clipRelative)
{
    if (const auto* c = clip(); c != nullptr)
        emitEdit(noteops::split(*c, targets(), clipRelative, minNoteLength), "Split notes");
}

void PianoRoll::joinSelection()
{
    if (const auto* c = clip(); c != nullptr && selection_.size() > 1)
        emitEdit(noteops::join(*c, selection_), "Join notes");
}

void PianoRoll::selectPitch(int note)
{
    if (const auto* c = clip())
    {
        selection_ = noteops::notesOfPitch(*c, note);
        selectionChanged();
        repaint();
    }
}

void PianoRoll::setGrid(std::size_t index)
{
    grid_ = std::min(index, noteGrids.size() - 1);
    refreshToolbar();
    repaint();
}

void PianoRoll::setSnapEnabled(bool on)
{
    snap_ = on;
    refreshToolbar();
    repaint();
}

// Each key is its own condition, so a keymap can take them one by one.
bool PianoRoll::handleEditKey(const juce::KeyPress& key)
{
    // The keys come from the keyboard map (ui/Shortcuts.h), so a rebinding applies.
    const auto is = [&](Command command) { return Shortcuts::matches(command, key); };
    const auto mods = key.getModifiers();
    const bool left = key.getKeyCode() == juce::KeyPress::leftKey;
    const bool controllerFocus = focusLane_ >= 0 && static_cast<size_t>(focusLane_) < lanes_.size() &&
                                 lanes_[static_cast<size_t>(focusLane_)].controller != nullptr;
    if (is(Command::pianoRollDelete))
    {
        if (controllerFocus && !controllerSelection().empty())
            deleteControllerSelection();
        else
            deleteSelection();
        return true;
    }
    const std::pair<Command, Tool> tools[] = {{Command::pianoRollSelectTool, Tool::select},
                                              {Command::pianoRollDrawTool, Tool::draw},
                                              {Command::pianoRollEraseTool, Tool::erase},
                                              {Command::pianoRollSplitTool, Tool::split},
                                              {Command::pianoRollGlueTool, Tool::glue},
                                              {Command::pianoRollMuteTool, Tool::mute}};
    for (const auto& [command, tool] : tools)
        if (is(command))
        {
            setTool(tool);
            return true;
        }
    if (is(Command::pianoRollSelectAll))
    {
        if (controllerFocus)
            lanes_[static_cast<size_t>(focusLane_)].controller->selectAll();
        else
            selectAll();
        return true;
    }
    if (is(Command::pianoRollCopy))
    {
        copySelection();
        return true;
    }
    if (is(Command::pianoRollCut))
    {
        cutSelection();
        return true;
    }
    if (is(Command::pianoRollPaste))
    {
        paste();
        return true;
    }
    if (is(Command::pianoRollDuplicate))
    {
        duplicateSelection();
        return true;
    }
    if (is(Command::pianoRollMuteNotes))
    {
        toggleMuteSelection();
        return true;
    }
    if (is(Command::pianoRollClearSelection))
    {
        for (auto& l : lanes_)
            if (l.controller != nullptr)
                l.controller->clearSelection();
        clearSelection();
        return true;
    }
    if (is(Command::pianoRollSnap))
    {
        setSnapEnabled(!snap_);
        return true;
    }
    if (is(Command::pianoRollQuantise) || is(Command::pianoRollQuantiseEnds))
    {
        quantiseSelection(quantiseStrength_, is(Command::pianoRollQuantiseEnds));
        return true;
    }
    if (is(Command::pianoRollCoarserGrid) || is(Command::pianoRollFinerGrid))
    {
        setGrid(noteGridStepped(grid_, is(Command::pianoRollCoarserGrid) ? -1 : 1));
        return true;
    }
    if (is(Command::pianoRollSplitAtPlayhead))
    {
        if (playhead_ >= 0)
            splitSelectionAt(playhead_);
        return true;
    }
    if (is(Command::pianoRollJoin))
    {
        joinSelection();
        return true;
    }
    // Zoom takes the window's keys while the piano roll has the focus.
    if (is(Command::zoomToFit))
    {
        zoomToFitNotes();
        return true;
    }
    if (is(Command::zoomIn) || is(Command::zoomOut))
    {
        const double spp = is(Command::zoomIn) ? std::max(1.0, samplesPerPixel_ / 1.5)
                                               : std::min(100000.0, samplesPerPixel_ * 1.5);
        const auto zoomed = view().zoomedAround(spp, gridArea().getCentreX());
        samplesPerPixel_ = zoomed.samplesPerPixel;
        scroll_ = std::max<core::SamplePosition>(0, zoomed.first);
        notifyTimeView();
        updateScrollbars();
        repaint();
        return true;
    }
    if (is(Command::pianoRollTransposeUp) || is(Command::pianoRollTransposeDown))
    {
        transposeSelection(is(Command::pianoRollTransposeDown) ? -1 : 1);
        return true;
    }
    if (is(Command::pianoRollStepSelection))
    {
        stepSelection(left ? -1 : 1, mods.isShiftDown());
        return true;
    }
    if (is(Command::pianoRollLength))
    {
        changeLength(left ? -1 : 1, mods.isShiftDown());
        return true;
    }
    if (is(Command::pianoRollNudge))
    {
        nudgeSelection(left ? -1 : 1, 0, mods.isShiftDown());
        return true;
    }
    if (is(Command::pianoRollNudgePitch))
    {
        const int step = mods.isCtrlDown() ? 12 : 1;
        nudgeSelection(0, key.getKeyCode() == juce::KeyPress::upKey ? step : -step);
        return true;
    }
    if (is(Command::pianoRollVelocity))
    {
        const int step = mods.isShiftDown() ? 1 : 8;
        changeVelocity(key.getKeyCode() == juce::KeyPress::upKey ? step : -step);
        return true;
    }
    if (is(Command::pianoRollInsertNote))
    {
        insertNote();
        return true;
    }
    return false;
}

bool PianoRoll::keyPressed(const juce::KeyPress& key)
{
    return handleEditKey(key);
}

// ----- the note menu --------------------------------------------------------------------------

void PianoRoll::showNoteMenu(juce::Point<int> at)
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    const bool any = !selection_.empty();
    const auto position = snapped(std::max<core::SamplePosition>(0, sampleForX(at.x)));
    const int pitch = noteForY(at.y);
    enum Item
    {
        cut = 1,
        copyItem,
        pasteHere,
        duplicate,
        remove,
        quantiseStarts,
        quantiseEnds,
        humaniseTiming,
        humaniseVelocity,
        legato,
        lengthGrid,
        lengthHalve,
        lengthDouble,
        up1,
        down1,
        up12,
        down12,
        mute,
        splitHere,
        join,
        selectPitchItem,
        selectAllItem,
        velocitySet = 100,   // + value
        velocityScale = 300, // + percent
        velocityCompress,
        velocityExpand,
        velocityRandom5,
        velocityRandom10,
        velocityRandom20,
        velocityLouder,
        velocitySofter,
    };
    juce::PopupMenu menu;
    menu.addItem(cut, "Cut", any);
    menu.addItem(copyItem, "Copy", any);
    menu.addItem(pasteHere, "Paste here", !clipboard().empty());
    menu.addItem(duplicate, "Duplicate", any);
    menu.addItem(remove, "Delete", any);
    menu.addSeparator();
    menu.addItem(quantiseStarts, any ? "Quantise" : "Quantise all");
    menu.addItem(quantiseEnds, any ? "Quantise starts and ends" : "Quantise all, starts and ends");
    menu.addItem(humaniseTiming,
                 juce::String::fromUTF8("Humanise timing (\xc2\xb1"
                                        "10 ms)"));
    menu.addSeparator();
    juce::PopupMenu velocity;
    for (const int v : {127, 100, 80, 64, 32})
        velocity.addItem(velocitySet + v, "Set to " + juce::String(v), any);
    velocity.addSeparator();
    velocity.addItem(velocityLouder, "Louder (+10)", any);
    velocity.addItem(velocitySofter, "Softer (-10)", any);
    velocity.addItem(velocityScale + 125, "Scale 125%", any);
    velocity.addItem(velocityScale + 80, "Scale 80%", any);
    velocity.addSeparator();
    velocity.addItem(velocityCompress, "Compress (towards the average)", selection_.size() > 1);
    velocity.addItem(velocityExpand, "Expand (away from the average)", selection_.size() > 1);
    velocity.addSeparator();
    velocity.addItem(velocityRandom5,
                     juce::String::fromUTF8("Randomise \xc2\xb1"
                                            "5"),
                     any);
    velocity.addItem(velocityRandom10,
                     juce::String::fromUTF8("Randomise \xc2\xb1"
                                            "10"),
                     any);
    velocity.addItem(velocityRandom20,
                     juce::String::fromUTF8("Randomise \xc2\xb1"
                                            "20"),
                     any);
    velocity.addItem(humaniseVelocity,
                     juce::String::fromUTF8("Humanise (\xc2\xb1"
                                            "8)"),
                     any);
    menu.addSubMenu("Velocity", velocity, any);
    juce::PopupMenu length;
    length.addItem(legato, "Legato (to the next note)", true);
    length.addItem(lengthGrid, "Set to the grid step", any);
    length.addItem(lengthHalve, "Halve", any);
    length.addItem(lengthDouble, "Double", any);
    menu.addSubMenu("Length", length);
    juce::PopupMenu transpose;
    transpose.addItem(up1, "Up a semitone (+)", any);
    transpose.addItem(down1, "Down a semitone (-)", any);
    transpose.addItem(up12, "Up an octave (Ctrl+Up)", any);
    transpose.addItem(down12, "Down an octave (Ctrl+Down)", any);
    menu.addSubMenu("Transpose", transpose, any);
    menu.addSeparator();
    bool allMuted = any;
    for (const auto id : selection_)
        if (const auto* n = c->findNote(id); n != nullptr && !n->muted)
            allMuted = false;
    menu.addItem(mute, allMuted ? "Unmute (Ctrl+M)" : "Mute (Ctrl+M)", any);
    menu.addItem(splitHere, "Split here", true);
    menu.addItem(join, "Join (J)", selection_.size() > 1);
    menu.addSeparator();
    menu.addItem(selectPitchItem, "Select all " + labelFor(pitch));
    menu.addItem(selectAllItem, "Select all (Ctrl+A)");
    juce::Component::SafePointer<PianoRoll> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            localAreaToGlobal(juce::Rectangle<int>(at, at).expanded(1))),
        [safe, position, pitch](int choice)
        {
            if (safe == nullptr || choice == 0)
                return;
            auto& v = *safe;
            const auto* clipModel = v.clip();
            if (clipModel == nullptr)
                return;
            const double rate = v.project_ != nullptr ? v.project_->sampleRate : 48000.0;
            const auto& sel = v.selection_;
            switch (choice)
            {
                case cut:
                    v.cutSelection();
                    return;
                case copyItem:
                    v.copySelection();
                    return;
                case pasteHere:
                    v.pasteAt(position);
                    return;
                case duplicate:
                    v.duplicateSelection();
                    return;
                case remove:
                    v.deleteSelection();
                    return;
                case quantiseStarts:
                    v.quantiseSelection(v.quantiseStrength_, false);
                    return;
                case quantiseEnds:
                    v.quantiseSelection(v.quantiseStrength_, true);
                    return;
                case humaniseTiming:
                    v.humaniseSelection(static_cast<core::SamplePosition>(std::llround(rate * 0.010)), 0);
                    return;
                case humaniseVelocity:
                    v.humaniseSelection(0, 8);
                    return;
                case legato:
                    v.legatoSelection();
                    return;
                case lengthGrid:
                    v.emitEdit(noteops::setLength(*clipModel, sel, v.gridSamples()), "Set note length");
                    return;
                case lengthHalve:
                    v.emitEdit(noteops::scaleLength(*clipModel, sel, 0.5, 1), "Halve note length");
                    return;
                case lengthDouble:
                    v.emitEdit(noteops::scaleLength(*clipModel, sel, 2.0, 1), "Double note length");
                    return;
                case up1:
                case down1:
                case up12:
                case down12:
                    v.transposeSelection(choice == up1     ? 1
                                         : choice == down1 ? -1
                                         : choice == up12  ? 12
                                                           : -12);
                    return;
                case mute:
                    v.toggleMuteSelection();
                    return;
                case splitHere:
                    v.splitSelectionAt(position);
                    return;
                case join:
                    v.joinSelection();
                    return;
                case selectPitchItem:
                    v.selectPitch(pitch);
                    return;
                case selectAllItem:
                    v.selectAll();
                    return;
                case velocityCompress:
                    v.emitEdit(noteops::compressVelocity(*clipModel, sel, 0.5), "Compress velocity");
                    return;
                case velocityExpand:
                    v.emitEdit(noteops::compressVelocity(*clipModel, sel, 1.5), "Expand velocity");
                    return;
                case velocityRandom5:
                case velocityRandom10:
                case velocityRandom20:
                    v.emitEdit(noteops::randomiseVelocity(*clipModel,
                                                          sel,
                                                          choice == velocityRandom5    ? 5
                                                          : choice == velocityRandom10 ? 10
                                                                                       : 20,
                                                          v.nextSeed()),
                               "Randomise velocity");
                    return;
                case velocityLouder:
                case velocitySofter:
                    v.emitEdit(noteops::offsetVelocity(*clipModel, sel, choice == velocityLouder ? 10 : -10),
                               "Change velocity");
                    return;
                default:
                    break;
            }
            if (choice > velocitySet && choice <= velocitySet + 127)
            {
                v.setDefaultVelocity(choice - velocitySet);
                v.emitEdit(noteops::setVelocity(*clipModel, sel, choice - velocitySet), "Set velocity");
            }
            else if (choice > velocityScale && choice < velocityScale + 200)
                v.emitEdit(noteops::scaleVelocity(*clipModel, sel, (choice - velocityScale) / 100.0),
                           "Scale velocity");
        });
}

// ----- the controller lanes -------------------------------------------------------------------

juce::String PianoRoll::laneName(const Lane& lane, bool brief)
{
    return MidiLane::name(lane, brief);
}

PianoRoll::Lane PianoRoll::laneForKey(const juce::String& key)
{
    return MidiLane::forKey(key);
}

void PianoRoll::setLane(Lane lane)
{
    if (lane.type == core::MidiControllerType::polyPressure)
        lane = Lane::velocities(); // per key: not a lane of its own
    auto& first = lanes_.front();
    if (lane == first.lane)
        return;
    const bool wasVelocity = first.lane.velocity;
    first.lane = lane;
    if (lane.velocity)
    {
        if (first.controller != nullptr)
        {
            removeChildComponent(first.controller.get());
            first.controller.reset();
        }
    }
    else
    {
        if (first.controller == nullptr)
            first.controller = makeControllerLane();
        first.controller->setLane(lane);
    }
    // A controller lane is taller: its values want the room.
    if (wasVelocity != lane.velocity)
        first.height = lane.velocity ? defaultLaneHeight : controllerLaneHeight;
    resized();
    repaint();
}

void PianoRoll::addLane(Lane lane)
{
    if (lanes_.size() >= maxLanes)
        return;
    if (lane.type == core::MidiControllerType::polyPressure)
        lane = Lane::velocities();
    LaneRow row{lane, lane.velocity ? defaultLaneHeight : controllerLaneHeight, nullptr};
    if (!lane.velocity)
    {
        row.controller = makeControllerLane();
        row.controller->setLane(lane);
    }
    lanes_.push_back(std::move(row));
    resized();
    repaint();
}

void PianoRoll::removeLane(std::size_t index)
{
    if (lanes_.size() <= 1 || index >= lanes_.size())
        return;
    if (lanes_[index].controller != nullptr)
        removeChildComponent(lanes_[index].controller.get());
    lanes_.erase(lanes_.begin() + static_cast<std::ptrdiff_t>(index));
    focusLane_ = -1;
    resized();
    repaint();
}

void PianoRoll::setLaneHeight(std::size_t index, int height)
{
    if (index >= lanes_.size())
        return;
    // The grid keeps at least a few rows.
    const int others = lanesHeight() - lanes_[index].height;
    const int room = getHeight() - toolbarHeight - rulerHeight_ - scrollbarSize_ - others - 4 * rowHeight_;
    lanes_[index].height =
        std::clamp(height, minLaneHeight, std::max(minLaneHeight, std::min(maxLaneHeight, room)));
    resized();
    repaint();
}

const std::vector<core::Id>& PianoRoll::controllerSelection() const noexcept
{
    static const std::vector<core::Id> none;
    if (focusLane_ >= 0 && static_cast<size_t>(focusLane_) < lanes_.size())
        if (const auto& l = lanes_[static_cast<size_t>(focusLane_)]; l.controller != nullptr)
            return l.controller->selection();
    for (const auto& l : lanes_)
        if (l.controller != nullptr && !l.controller->selection().empty())
            return l.controller->selection();
    return none;
}

void PianoRoll::deleteControllerSelection()
{
    if (focusLane_ >= 0 && static_cast<size_t>(focusLane_) < lanes_.size())
        if (auto& l = lanes_[static_cast<size_t>(focusLane_)]; l.controller != nullptr)
        {
            l.controller->deleteSelection();
            return;
        }
    for (auto& l : lanes_)
        if (l.controller != nullptr && !l.controller->selection().empty())
        {
            l.controller->deleteSelection();
            return;
        }
}

void PianoRoll::setScaleHighlight(bool on)
{
    scaleHighlight_ = on;
    showToggle(scaleButton_, on);
    repaint();
}

int PianoRoll::yForValue(std::size_t lane, int value) const noexcept
{
    const auto& l = lanes_[lane];
    if (l.controller == nullptr)
        return laneArea(lane).getCentreY();
    return l.controller->getY() + l.controller->yForValue(value);
}

void PianoRoll::showLaneMenu(std::size_t index)
{
    const auto* c = clip();
    const auto has = [c](core::MidiControllerType type, int number)
    {
        return c != nullptr &&
               std::any_of(c->controllers.begin(),
                           c->controllers.end(),
                           [&](const core::MidiControllerEvent& e) { return e.sameLane(type, number); });
    };
    const auto label = [&](const Lane& lane)
    {
        // A dot marks the lanes this clip has events in.
        return laneName(lane) + (!lane.velocity && has(lane.type, lane.number)
                                     ? juce::String(juce::CharPointer_UTF8("  \xe2\x80\xa2"))
                                     : juce::String());
    };
    const Lane fixed[] = {Lane::velocities(),
                          Lane::controller(core::MidiControllerType::pitchBend),
                          Lane::controller(core::MidiControllerType::controller, 1),
                          Lane::controller(core::MidiControllerType::controller, 11),
                          Lane::controller(core::MidiControllerType::controller, 64),
                          Lane::controller(core::MidiControllerType::channelPressure)};
    const auto current = lanes_[index].lane;
    juce::PopupMenu menu, add;
    menu.addSectionHeader("Lane");
    for (int i = 0; i < 6; ++i)
    {
        menu.addItem(1 + i, label(fixed[i]), true, current == fixed[i]);
        add.addItem(201 + i, label(fixed[i]));
    }
    juce::PopupMenu others, addOthers;
    for (int number = 0; number < 128; ++number)
    {
        const auto lane = Lane::controller(core::MidiControllerType::controller, number);
        others.addItem(1000 + number, label(lane), true, current == lane);
        addOthers.addItem(2000 + number, label(lane));
    }
    menu.addSubMenu("Other CC", others);
    menu.addSeparator();
    add.addSubMenu("Other CC", addOthers);
    menu.addSubMenu("Add a lane", add, lanes_.size() < maxLanes);
    menu.addItem(300, "Remove this lane", lanes_.size() > 1);
    juce::Component::SafePointer<PianoRoll> safe(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(laneLabelArea(index))),
        [safe, fixed, index](int choice)
        {
            if (safe == nullptr || choice == 0 || index >= safe->lanes_.size())
                return;
            const auto set = [&](Lane lane)
            {
                if (index == 0)
                {
                    safe->setLane(lane);
                    return;
                }
                // A lower lane: replaced in place.
                const auto height = safe->lanes_[index].height;
                safe->removeLane(index);
                safe->addLane(lane);
                auto row = std::move(safe->lanes_.back());
                safe->lanes_.pop_back();
                row.height = height;
                safe->lanes_.insert(safe->lanes_.begin() + static_cast<std::ptrdiff_t>(index),
                                    std::move(row));
                safe->resized();
                safe->repaint();
            };
            if (choice >= 2000)
                safe->addLane(Lane::controller(core::MidiControllerType::controller, choice - 2000));
            else if (choice >= 1000)
                set(Lane::controller(core::MidiControllerType::controller, choice - 1000));
            else if (choice == 300)
                safe->removeLane(index);
            else if (choice >= 201 && choice <= 206)
                safe->addLane(fixed[choice - 201]);
            else if (choice >= 1 && choice <= 6)
                set(fixed[choice - 1]);
        });
}

} // namespace northlight::ui
