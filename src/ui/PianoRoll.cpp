#include "ui/PianoRoll.h"

#include "core/Command.h"
#include "core/NoteNames.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/PaintStats.h"
#include "ui/components/DragTip.h"
#include "ui/timeline/GridLines.h"
#include "ui/timeline/TimeGrid.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace northlight::ui
{

namespace
{
constexpr int rows = 128;
constexpr core::SamplePosition minNoteLength = 1;

using core::isBlackKey;
} // namespace

PianoRoll::PianoRoll(const Theme& theme)
    : theme_(theme), selectTool_(theme_, Icon::pointer, "Select tool: move, resize and select notes  1"),
      pencilTool_(theme_, Icon::pencil, "Pencil: click to add a note, drag to set its length  3"),
      eraseTool_(theme_, Icon::eraser, "Erase: click or sweep over notes to delete them  4"),
      link_(theme_, Icon::link, "Link zoom to the arrange view"),
      auditionButton_(theme_, Icon::monitor, "Hear notes while drawing and moving them")
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
    verticalScroll_ = (rows - 84) * rowHeight; // start around C3..C6

    selectTool_.onClick = [this] { setTool(Tool::select); };
    pencilTool_.onClick = [this] { setTool(Tool::draw); };
    eraseTool_.onClick = [this] { setTool(Tool::erase); };
    for (auto* b : {&selectTool_, &pencilTool_, &eraseTool_})
    {
        b->setIconSize(theme_.sizes.iconSmall);
        addAndMakeVisible(*b);
    }
    refreshToolButtons();
    snapButton_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    snapButton_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    snapButton_.setTooltip("Snap grid");
    snapButton_.onClick = [this] { showSnapMenu(); };
    addAndMakeVisible(snapButton_);
    quantiseButton_.setButtonText("Quantise");
    quantiseButton_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    quantiseButton_.getProperties().set(NorthlightLookAndFeel::propertyMenu, true);
    quantiseButton_.setTooltip("Quantise the selected notes (Q); the strength is in the menu");
    quantiseButton_.onClick = [this] { showQuantiseMenu(); };
    addAndMakeVisible(quantiseButton_);
    link_.setClickingTogglesState(true);
    link_.onClick = [this]
    {
        linked_ = link_.getToggleState();
        repaint();
    };
    addAndMakeVisible(link_);
    auditionButton_.setClickingTogglesState(true);
    auditionButton_.setToggleState(audition_, juce::dontSendNotification);
    auditionButton_.onClick = [this] { setAudition(auditionButton_.getToggleState()); };
    addAndMakeVisible(auditionButton_);
    scaleButton_.setButtonText("Scale");
    scaleButton_.getProperties().set(NorthlightLookAndFeel::propertyVariant, "ghost");
    scaleButton_.setTooltip("Shade the notes outside the project key's scale and mark its notes on the keys");
    scaleButton_.setClickingTogglesState(true);
    scaleButton_.setToggleState(scaleHighlight_, juce::dontSendNotification);
    scaleButton_.onClick = [this] { setScaleHighlight(scaleButton_.getToggleState()); };
    addAndMakeVisible(scaleButton_);
    refreshToolbar();
    controllerLane_.view = [this]
    {
        auto axis = view(); // in the lane's own coordinates
        axis.originX -= controllerLane_.getX();
        return axis;
    };
    controllerLane_.snap = [this](core::SamplePosition s) { return snapped(s); };
    controllerLane_.gridSamples = [this] { return gridSamples(); };
    controllerLane_.snapEnabled = [this] { return snap_; };
    controllerLane_.onEdit = [this](std::vector<std::unique_ptr<core::Command>> commands,
                                    const std::string& name) { emit(std::move(commands), name); };
    controllerLane_.onPressed = [this]
    {
        laneFocus_ = true;
        grabKeyboardFocus();
    };
    addChildComponent(controllerLane_);
}

PianoRoll::~PianoRoll()
{
    horizontal_.removeListener(this);
    vertical_.removeListener(this);
}

void PianoRoll::setClip(const core::Project* project, core::Id trackId, core::Id clipId)
{
    const bool same = project == project_ && trackId == trackId_ && clipId == clipId_;
    project_ = project;
    tempoMap_ = project != nullptr ? core::tempoMapOf(*project) : core::TempoMap();
    trackId_ = trackId;
    clipId_ = clipId;
    controllerLane_.setClip(project, trackId, clipId);
    controllerLane_.setColour(trackColour());
    const auto* c = clip();
    if (c == nullptr)
    {
        selection_.clear();
        repaint();
        return;
    }
    std::erase_if(selection_, [c](core::Id id) { return c->findNote(id) == nullptr; });
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
        for (int note = 0; note < rows; ++note)
            if (changed[static_cast<size_t>(note)])
                repaint(keys.getX(), yForNote(note), keys.getWidth(), rowHeight);
    }
}

std::bitset<128> PianoRoll::soundingKeys() const
{
    std::bitset<128> lit;
    if (const auto* c = clip(); c != nullptr && playhead_ >= 0)
        for (const auto& n : c->notes)
            if (playhead_ >= n.start && playhead_ < n.end() && n.note >= 0 && n.note < rows)
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

void PianoRoll::setTool(Tool tool)
{
    tool_ = tool;
    controllerLane_.setTool(tool == Tool::draw    ? ControllerLane::Tool::draw
                            : tool == Tool::erase ? ControllerLane::Tool::erase
                                                  : ControllerLane::Tool::select);
    refreshToolButtons();
    setMouseCursor(toolCursor());
    repaint();
}

void PianoRoll::refreshToolButtons()
{
    selectTool_.setToggleState(tool_ == Tool::select, juce::dontSendNotification);
    pencilTool_.setToggleState(tool_ == Tool::draw, juce::dontSendNotification);
    eraseTool_.setToggleState(tool_ == Tool::erase, juce::dontSendNotification);
}

juce::MouseCursor PianoRoll::toolCursor() const
{
    return tool_ == Tool::draw    ? Icons::toolCursor(theme_, Icon::pencil)
           : tool_ == Tool::erase ? Icons::toolCursor(theme_, Icon::eraser)
                                  : juce::MouseCursor(juce::MouseCursor::NormalCursor);
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
    if (t == nullptr || project_ == nullptr)
        return theme_.colours.midiNote;
    return theme_.track.forChannel(*project_, t->id);
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

juce::Rectangle<int> PianoRoll::gridArea() const noexcept
{
    return {keyboardWidth,
            toolbarHeight + rulerHeight_,
            std::max(0, getWidth() - keyboardWidth - scrollbarSize_),
            std::max(0, getHeight() - toolbarHeight - rulerHeight_ - velocityHeight_ - scrollbarSize_)};
}

juce::Rectangle<int> PianoRoll::velocityArea() const noexcept
{
    return {keyboardWidth,
            getHeight() - scrollbarSize_ - velocityHeight_,
            std::max(0, getWidth() - keyboardWidth - scrollbarSize_),
            velocityHeight_};
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
    return toolbarHeight + rulerHeight_ + (127 - note) * rowHeight - verticalScroll_;
}

int PianoRoll::noteForY(int y) const noexcept
{
    const int row = (y - toolbarHeight - rulerHeight_ + verticalScroll_) / rowHeight;
    return std::clamp(127 - row, 0, 127);
}

core::GridStep PianoRoll::gridStep(core::SamplePosition at) const noexcept
{
    // gridDivision_ divides a beat; the arrange view's divisions count a whole note.
    const auto* c = clip();
    const auto absolute = (c != nullptr ? c->start : 0) + at;
    return snapStepAt(tempoMap_, absolute, 4 * std::max(1, gridDivision_), samplesPerPixel_);
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
    return {x0, yForNote(note.note), x1 - x0, rowHeight};
}

void PianoRoll::setAudition(bool on)
{
    if (!on)
        stopAudition();
    audition_ = on;
    auditionButton_.setToggleState(on, juce::dontSendNotification);
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
    }
    else if (drag_ == Drag::resize && draggedFar_ &&
             (note.id == dragNote_ || (isSelected(dragNote_) && isSelected(note.id))))
        p.length = lengthPreview_;
    else if (drag_ == Drag::velocity && draggedFar_)
    {
        if (const auto it = velocityRamp_.find(note.id); it != velocityRamp_.end())
            p.velocity = it->second;
        else if (note.id == dragNote_ || isSelected(note.id))
            p.velocity = velocityPreview_;
    }
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
        const auto bounds = noteBounds(*it);
        if (!bounds.contains(p))
            continue;
        onRightEdge = bounds.getWidth() > 2 * edgeZone && p.x >= bounds.getRight() - edgeZone;
        return &*it;
    }
    return nullptr;
}

const core::MidiNote* PianoRoll::noteAtVelocityBar(juce::Point<int> p) const noexcept
{
    const auto* c = clip();
    if (c == nullptr || !velocityArea().contains(p))
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
        int low = 127, high = 0;
        for (const auto& n : c->notes)
        {
            low = std::min(low, n.note);
            high = std::max(high, n.note);
        }
        const int middle = (low + high) / 2;
        verticalScroll_ = std::clamp((127 - middle) * rowHeight - gridArea().getHeight() / 2,
                                     0,
                                     std::max(0, rows * rowHeight - gridArea().getHeight()));
    }
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
    vertical_.setRangeLimits(0.0, rows * rowHeight);
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

void PianoRoll::resized()
{
    if (fitPending_ && gridArea().getWidth() > 16)
        fitToClip();
    auto bar = getLocalBounds()
                   .removeFromTop(toolbarHeight)
                   .reduced(theme_.spacing.sm, (toolbarHeight - theme_.sizes.controlCompact) / 2);
    for (auto* b : {&selectTool_, &pencilTool_, &eraseTool_})
        b->setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
    bar.removeFromLeft(theme_.spacing.sm);
    snapButton_.setBounds(bar.removeFromLeft(72));
    bar.removeFromLeft(theme_.spacing.sm);
    quantiseButton_.setBounds(bar.removeFromLeft(96));
    bar.removeFromLeft(theme_.spacing.sm);
    link_.setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
    bar.removeFromLeft(theme_.spacing.xs);
    auditionButton_.setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
    bar.removeFromLeft(theme_.spacing.xs);
    scaleButton_.setBounds(bar.removeFromLeft(56));
    horizontal_.setBounds(keyboardWidth,
                          getHeight() - scrollbarSize_,
                          std::max(0, getWidth() - keyboardWidth - scrollbarSize_),
                          scrollbarSize_);
    vertical_.setBounds(
        getWidth() - scrollbarSize_, gridArea().getY(), scrollbarSize_, gridArea().getHeight());
    controllerLane_.setBounds(velocityArea());
    updateScrollbars();
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        const auto zoomed = view().wheelZoomed(wheel.deltaY, e.x, 1.0, 100000.0);
        samplesPerPixel_ = zoomed.samplesPerPixel;
        scroll_ = std::max<core::SamplePosition>(0, zoomed.first);
        notifyTimeView();
    }
    else if (e.mods.isShiftDown())
    {
        scroll_ = std::max<core::SamplePosition>(
            0, scroll_ - static_cast<core::SamplePosition>(wheel.deltaY * 200.0 * samplesPerPixel_));
        notifyTimeView();
    }
    else
    {
        verticalScroll_ = std::clamp(verticalScroll_ - static_cast<int>(wheel.deltaY * 120.0),
                                     0,
                                     std::max(0, rows * rowHeight - gridArea().getHeight()));
    }
    updateScrollbars();
    repaint();
}

// ----- the toolbar ----------------------------------------------------------------------------

void PianoRoll::refreshToolbar()
{
    snapButton_.setButtonText((snap_ ? "1/" : "Off 1/") + juce::String(gridDivision_ * 4));
}

void PianoRoll::showSnapMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Snap", true, snap_);
    menu.addSeparator();
    static constexpr int divisions[] = {1, 2, 4, 8};
    for (int i = 0; i < 4; ++i)
        menu.addItem(10 + i, "1/" + juce::String(divisions[i] * 4), true, gridDivision_ == divisions[i]);
    juce::Component::SafePointer<PianoRoll> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(snapButton_),
                       [safe](int choice)
                       {
                           if (safe == nullptr || choice == 0)
                               return;
                           if (choice == 1)
                               safe->setSnapEnabled(!safe->snap_);
                           else
                               safe->setGridDivision(divisions[choice - 10]);
                       });
}

void PianoRoll::showQuantiseMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, "Quantise starts (Q)");
    menu.addItem(2, "Quantise starts and ends (Shift+Q)");
    menu.addSeparator();
    menu.addSectionHeader("Strength");
    for (const int percent : {100, 75, 50, 25})
        menu.addItem(100 + percent,
                     juce::String(percent) + "%",
                     true,
                     std::abs(quantiseStrength_ - percent / 100.0f) < 0.01f);
    juce::Component::SafePointer<PianoRoll> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(quantiseButton_),
                       [safe](int choice)
                       {
                           if (safe == nullptr || choice == 0)
                               return;
                           if (choice == 1)
                               safe->quantiseSelection(safe->quantiseStrength_, false);
                           else if (choice == 2)
                               safe->quantiseSelection(safe->quantiseStrength_, true);
                           else
                               safe->quantiseStrength_ = static_cast<float>(choice - 100) / 100.0f;
                       });
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

void PianoRoll::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.canvas);
    // Only what the repaint covers is drawn (a playhead frame is a column and a few keys).
    const auto painted = g.getClipBounds();
    const auto grid = gridArea();
    const auto velocity = velocityArea();
    const auto ruler = rulerArea();
    const auto keys = keysArea();
    const auto paintedGrid = grid.getIntersection(painted);
    const auto* clipModel = clip();
    const auto* trackModel = track();
    const auto colour = trackColour();

    // The toolbar.
    g.setColour(c.surface0);
    g.fillRect(0, 0, getWidth(), toolbarHeight);
    g.setColour(c.lineSubtle);
    g.fillRect(0, toolbarHeight - 1, getWidth(), 1);
    g.setFont(theme_.caption());
    g.setColour(c.textTertiary);
    const core::MusicalKey key = project_ != nullptr ? project_->key : core::MusicalKey{};
    g.drawText(key.isSet() ? juce::String(core::pitchClassName(key.root)) + " " + core::scaleName(key.scale)
                           : juce::String("No key"),
               getLocalBounds().removeFromTop(toolbarHeight).reduced(theme_.spacing.sm, 0),
               juce::Justification::centredRight);

    // Rows: laneAlt on the black keys, an octave line under each C.
    g.saveState();
    g.reduceClipRegion(grid);
    for (int note = 0; note < rows; ++note)
    {
        const int y = yForNote(note);
        if (y + rowHeight < paintedGrid.getY() || y > paintedGrid.getBottom())
            continue;
        if (isBlackKey(note))
        {
            g.setColour(c.laneAlt);
            g.fillRect(grid.getX(), y, grid.getWidth(), rowHeight);
        }
        if (scaleHighlight_ && key.isSet() && !key.contains(note))
        {
            g.setColour(c.canvas.withAlpha(0.45f));
            g.fillRect(grid.getX(), y, grid.getWidth(), rowHeight);
        }
        else if (scaleHighlight_ && key.isSet() && note % 12 == key.root)
        {
            g.setColour(c.accentSubtle);
            g.fillRect(grid.getX(), y, grid.getWidth(), rowHeight);
        }
        if (note % 12 == 0)
        {
            g.setColour(c.gridBar);
            g.fillRect(grid.getX(), y + rowHeight - 1, grid.getWidth(), 1);
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

    // Ghost notes: the track's other clips at 30%.
    if (trackModel != nullptr && clipModel != nullptr && !paintedGrid.isEmpty())
        for (const auto& other : trackModel->midiClips)
        {
            if (other.id == clipModel->id)
                continue;
            // A clip wholly outside what is painted has no ghost to draw.
            const int otherLeft = xForSample(other.start - clipModel->start);
            const int otherRight = xForSample(other.end() - clipModel->start);
            if (otherRight < paintedGrid.getX() || otherLeft > paintedGrid.getRight())
                continue;
            for (const auto& n : other.notes)
            {
                core::MidiNote shifted = n;
                shifted.start = other.start + n.start - clipModel->start;
                const auto bounds = noteBounds(shifted).reduced(0, 1);
                if (!bounds.intersects(paintedGrid))
                    continue;
                g.setColour(colour.withAlpha(0.3f * theme_.track.waveformAlpha));
                g.fillRoundedRectangle(bounds.toFloat(), theme_.radius.sm);
            }
        }

    // Notes: the track colour, lighter with velocity, a canvas inset so neighbours
    // separate; selected notes outlined in textPrimary over a selection tint.
    if (clipModel != nullptr)
    {
        const auto low = c.midiNoteVelocityLow;
        for (const auto& model : clipModel->notes)
        {
            const auto note = previewOf(model);
            const auto bounds = noteBounds(note).reduced(0, 1);
            if (!bounds.intersects(paintedGrid))
                continue;
            paintstats::counters().notesDrawn++;
            const bool selected = isSelected(model.id);
            const float t = static_cast<float>(std::clamp(note.velocity, 1, 127) - 1) / 126.0f;
            const auto fill = colour.withAlpha(theme_.track.waveformAlpha);
            g.setColour(fill.withAlpha(
                juce::jmap(t, low.getFloatAlpha() * theme_.track.waveformAlpha, theme_.track.waveformAlpha)));
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
                g.drawText(noteName(note.note), bounds.reduced(4, 0), juce::Justification::centredLeft, true);
            }
        }
        if (drag_ == Drag::draw)
        {
            const auto bounds = noteBounds(drawNote_).reduced(0, 1);
            g.setColour(colour.withAlpha(theme_.track.waveformAlpha));
            g.fillRoundedRectangle(bounds.toFloat(), theme_.radius.sm);
            g.setColour(c.textPrimary);
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
        }
    }
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
                    noteName(pitch) +
                        (pitchDelta_ != 0 ? "  (" + juce::String(pitchDelta_ > 0 ? "+" : "") +
                                                juce::String(pitchDelta_) + ")"
                                          : juce::String()) +
                        (duplicating_ ? "  copy" : ""),
                    dragPoint_);
    }
    g.restoreState();

    // Keyboard: white keys surface2, black keys surface0, C labels; keys whose
    // notes sound at the playhead (or are held) light in accent.
    g.saveState();
    g.reduceClipRegion(keys);
    const auto paintedKeys = keys.getIntersection(painted);
    auto lit = soundingKeys();
    if (heldKey_ >= 0 && heldKey_ < rows)
        lit.set(static_cast<size_t>(heldKey_));
    for (int note = 0; note < rows && !paintedKeys.isEmpty(); ++note)
    {
        const int y = yForNote(note);
        if (y + rowHeight < paintedKeys.getY() || y > paintedKeys.getBottom())
            continue;
        const bool black = isBlackKey(note);
        g.setColour(lit[static_cast<size_t>(note)] ? c.accent : black ? c.surface0 : c.surface2);
        g.fillRect(0, y, black ? keyboardWidth * 2 / 3 : keyboardWidth - 1, rowHeight - 1);
        // The scale: a stripe on the keys in the project key, the root named.
        const bool root = scaleHighlight_ && key.isSet() && note % 12 == key.root;
        if (scaleHighlight_ && key.isSet() && key.contains(note) && !lit[static_cast<size_t>(note)])
        {
            g.setColour(root ? c.accent : c.accent.withAlpha(0.45f));
            g.fillRect(keyboardWidth - 5, y, 3, rowHeight - 1);
        }
        if (note % 12 == 0 || root)
        {
            g.setColour(lit[static_cast<size_t>(note)] ? c.textOnAccent : root ? c.accent : c.textSecondary);
            g.setFont(theme_.caption());
            g.drawText(
                noteName(note), 4, y, keyboardWidth - 10, rowHeight, juce::Justification::centredRight);
        }
    }
    g.setColour(c.lineSubtle);
    g.fillRect(keyboardWidth - 1, keys.getY(), 1, keys.getHeight());
    g.restoreState();

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

    // The lane: velocity (a bar per note with a 6 px cap) or a controller; its
    // name opens the menu that picks it.
    g.setColour(c.surface0);
    g.fillRect(velocity.withX(0).withWidth(getWidth()));
    g.setColour(c.lineSubtle);
    g.fillRect(0, velocity.getY(), getWidth(), 1);
    {
        auto label = laneLabelArea();
        const int chevron = 10;
        g.setColour(c.textTertiary);
        g.setFont(theme_.captionCaps());
        g.drawText(laneName(lane_, true),
                   label.withTrimmedRight(theme_.spacing.sm + chevron),
                   juce::Justification::centredRight);
        Icons::draw(g,
                    Icon::chevronDown,
                    label.removeFromRight(theme_.spacing.sm + chevron)
                        .withSizeKeepingCentre(chevron, chevron)
                        .toFloat(),
                    c.textTertiary);
        if (!lane_.velocity)
        {
            g.setFont(theme_.caption());
            const int low = core::midiControllerMinimum(lane_.type),
                      high = core::midiControllerMaximum(lane_.type);
            g.drawText(juce::String(high > 127 ? "+" : "") + juce::String(high),
                       0,
                       std::max(yForValue(high) - 2, laneLabelArea().getBottom()),
                       keyboardWidth - theme_.spacing.sm,
                       12,
                       juce::Justification::centredRight);
            g.drawText(juce::String(low),
                       0,
                       yForValue(low) - 10,
                       keyboardWidth - theme_.spacing.sm,
                       12,
                       juce::Justification::centredRight);
        }
    }
    if (clipModel != nullptr && lane_.velocity && velocity.intersects(painted))
    {
        g.saveState();
        g.reduceClipRegion(velocity);
        const auto paintedLane = velocity.getIntersection(painted);
        for (const auto& model : clipModel->notes)
        {
            const auto note = previewOf(model);
            const int x = xForSample(note.start);
            if (x < paintedLane.getX() - 6 || x > paintedLane.getRight())
                continue;
            const int h = (velocity.getHeight() - 10) * note.velocity / 127;
            const bool selected = isSelected(model.id);
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

    if (clipModel == nullptr)
    {
        g.setColour(c.textTertiary);
        g.setFont(theme_.body());
        g.drawText("Select a MIDI clip to edit its notes", grid, juce::Justification::centred);
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

    laneFocus_ = velocityArea().contains(e.getPosition());
    if (laneLabelArea().contains(e.getPosition()))
    {
        showLaneMenu();
        return;
    }
    if (velocityArea().contains(e.getPosition()))
    {
        velocityRamp_.clear();
        if (const auto* note = noteAtVelocityBar(e.getPosition()))
        {
            dragNote_ = note->id;
            dragOriginal_ = *note;
            velocityPreview_ = note->velocity;
            if (!isSelected(note->id))
            {
                selection_.clear();
                selection_.push_back(note->id);
            }
            drag_ = Drag::velocity;
            repaint();
        }
        return;
    }
    if (!gridArea().contains(e.getPosition()))
        return;
    if (e.mods.isPopupMenu())
        return;

    bool onEdge = false;
    const auto* note = noteAt(e.getPosition(), onEdge);
    if (tool_ == Tool::erase)
    {
        drag_ = Drag::erase;
        eraseAt(e.getPosition());
        return;
    }
    if (tool_ == Tool::draw && note == nullptr)
    {
        drawNote_.note = noteForY(e.y);
        drawNote_.velocity = 100;
        drawNote_.start = snapped(sampleForX(e.x));
        drawNote_.length = lastLength_ > 0 ? lastLength_ : gridSamples();
        drag_ = Drag::draw;
        auditionNote(drawNote_.note);
        repaint();
        return;
    }
    if (note == nullptr)
    {
        drag_ = Drag::lasso;
        lasso_ = juce::Rectangle<int>(e.x, e.y, 0, 0);
        return;
    }
    dragNote_ = note->id;
    dragOriginal_ = *note;
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
            verticalScroll_ = std::clamp(panVertical_ - (e.y - dragStart_.y),
                                         0,
                                         std::max(0, rows * rowHeight - gridArea().getHeight()));
            updateScrollbars();
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
            const auto area = velocityArea();
            const auto velocityFor = [&](int y)
            {
                return std::clamp(
                    (area.getBottom() - 4 - y) * 127 / std::max(1, area.getHeight() - 10), 1, 127);
            };
            velocityPreview_ = velocityFor(e.y);
            // Dragging across bars ramps the notes between the start and the pointer.
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
    switch (drag)
    {
        case Drag::pan:
            break;
        case Drag::key:
            if (onPreviewNote && heldKey_ >= 0)
                onPreviewNote(trackId_, heldKey_, false);
            heldKey_ = -1;
            break;
        case Drag::erase:
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
                finishVelocity();
            break;
        case Drag::none:
            break;
    }
    duplicating_ = false;
    velocityRamp_.clear();
    repaint();
}

void PianoRoll::mouseMove(const juce::MouseEvent& e)
{
    if (drag_ != Drag::none)
        return;
    bool onEdge = false;
    const auto* note = noteAt(e.getPosition(), onEdge);
    // The pencil on a note and the select tool move and resize it; the eraser
    // keeps its cursor everywhere.
    if (tool_ == Tool::erase || (tool_ == Tool::draw && note == nullptr))
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
    if (hoverNote_ != core::invalidId)
    {
        hoverNote_ = core::invalidId;
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

void PianoRoll::eraseAt(juce::Point<int> p)
{
    bool onEdge = false;
    if (const auto* note = noteAt(p, onEdge))
    {
        std::vector<std::unique_ptr<core::Command>> commands;
        commands.push_back(std::make_unique<core::RemoveNote>(trackId_, clipId_, note->id));
        std::erase(selection_, note->id);
        emit(std::move(commands), "Erase note");
    }
}

void PianoRoll::addNoteAt(juce::Point<int> p, core::SamplePosition length)
{
    const auto* c = clip();
    if (c == nullptr || !gridArea().contains(p))
        return;
    core::MidiNote note;
    note.note = noteForY(p.y);
    note.velocity = 100;
    note.start = snapped(sampleForX(p.x));
    note.length = std::max(minNoteLength, length);
    if (note.start >= c->length)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    auto add = std::make_unique<core::AddNote>(trackId_, clipId_, note);
    const auto* raw = add.get();
    commands.push_back(std::move(add));
    emit(std::move(commands), "Add note");
    // The command allocated the id when it was applied (the owner executed it).
    selection_.clear();
    if (raw != nullptr && raw->noteId() != core::invalidId)
        selection_.push_back(raw->noteId());
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

void PianoRoll::finishVelocity()
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    for (const auto& note : c->notes)
    {
        int target = note.velocity;
        if (const auto it = velocityRamp_.find(note.id); it != velocityRamp_.end())
            target = it->second;
        else if (note.id == dragNote_ || isSelected(note.id))
            target = velocityPreview_;
        if (target == note.velocity)
            continue;
        auto changed = note;
        changed.velocity = target;
        commands.push_back(std::make_unique<core::SetNote>(trackId_, clipId_, changed));
    }
    emit(std::move(commands), "Set velocity");
}

void PianoRoll::finishLasso()
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    for (const auto& n : c->notes)
        if (noteBounds(n).intersects(lasso_) && !isSelected(n.id))
            selection_.push_back(n.id);
}

void PianoRoll::selectAll()
{
    selection_.clear();
    if (const auto* c = clip())
        for (const auto& n : c->notes)
            selection_.push_back(n.id);
    repaint();
}

void PianoRoll::clearSelection()
{
    selection_.clear();
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
    emit(std::move(commands), "Delete notes");
}

void PianoRoll::duplicateSelection()
{
    // Copies land right after the selection's span, keeping their spacing.
    const auto* c = clip();
    if (c == nullptr || selection_.empty())
        return;
    core::SamplePosition first = -1, last = 0;
    std::vector<core::MidiNote> notes;
    for (const auto& n : c->notes)
        if (isSelected(n.id))
        {
            notes.push_back(n);
            first = first < 0 ? n.start : std::min(first, n.start);
            last = std::max(last, n.end());
        }
    if (notes.empty())
        return;
    const auto span = std::max<core::SamplePosition>(gridSamples(), snapped(last - first, true));
    std::vector<std::unique_ptr<core::Command>> commands;
    for (auto note : notes)
    {
        note.id = core::invalidId;
        note.start += span;
        commands.push_back(std::make_unique<core::AddNote>(trackId_, clipId_, note));
    }
    emit(std::move(commands), "Duplicate notes");
}

void PianoRoll::quantiseSelection(float strength, bool ends)
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    const auto grid = gridSamples();
    commands.push_back(std::make_unique<core::QuantiseNotes>(
        trackId_, clipId_, selection_, tempoMap_, std::max(1, gridDivision_), strength));
    const auto snapRelative = [&](core::SamplePosition s)
    { return tempoMap_.snap(c->start + s, gridStep(s)) - c->start; };
    if (ends)
    {
        // The ends move towards the grid by the same strength (at least one step long).
        for (const auto& n : c->notes)
        {
            if (!selection_.empty() && !isSelected(n.id))
                continue;
            const auto start = selection_.empty() || isSelected(n.id)
                                   ? n.start + static_cast<core::SamplePosition>(
                                                   std::llround((snapRelative(n.start) - n.start) * strength))
                                   : n.start;
            const auto end = n.end();
            const auto target = snapRelative(end);
            auto moved = n;
            moved.start = start;
            moved.length = std::max(
                grid,
                end + static_cast<core::SamplePosition>(std::llround((target - end) * strength)) - start);
            commands.push_back(std::make_unique<core::SetNote>(trackId_, clipId_, moved));
        }
    }
    emit(std::move(commands), "Quantise notes");
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
    if (fine)
        grid.quarters /= 4.0;
    const auto origin = c->start + std::max<core::SamplePosition>(0, from);
    const auto step = timeSteps == 0 ? 0 : tempoMap_.move(origin, grid, timeSteps) - origin;
    for (const auto id : selection_)
        if (const auto* n = c->findNote(id))
        {
            auto moved = *n;
            moved.start = std::max<core::SamplePosition>(0, n->start + step);
            moved.note = std::clamp(n->note + semitones, 0, 127);
            commands.push_back(std::make_unique<core::SetNote>(trackId_, clipId_, moved));
        }
    emit(std::move(commands), "Nudge notes");
}

void PianoRoll::transposeSelection(int semitones)
{
    nudgeSelection(0, semitones);
}

void PianoRoll::setGridDivision(int division)
{
    gridDivision_ = std::clamp(division, 1, 32);
    refreshToolbar();
    repaint();
}

void PianoRoll::setSnapEnabled(bool on)
{
    snap_ = on;
    refreshToolbar();
    repaint();
}

bool PianoRoll::handleEditKey(const juce::KeyPress& key)
{
    const auto plain = [&](juce::juce_wchar ch)
    { return key == juce::KeyPress(ch, juce::ModifierKeys(), 0); };
    const auto ctrl = [&](juce::juce_wchar ch)
    { return key == juce::KeyPress(ch, juce::ModifierKeys::commandModifier, 0); };
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (laneFocus_ && !controllerLane_.selection().empty())
            controllerLane_.deleteSelection();
        else
            deleteSelection();
        return true;
    }
    if (plain('1') || plain('3') || plain('4'))
    {
        setTool(plain('3') ? Tool::draw : plain('4') ? Tool::erase : Tool::select);
        return true;
    }
    if (ctrl('a'))
    {
        if (laneFocus_ && !lane_.velocity)
            controllerLane_.selectAll();
        else
            selectAll();
        return true;
    }
    if (ctrl('d'))
    {
        duplicateSelection();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        controllerLane_.clearSelection();
        clearSelection();
        return true;
    }
    if (plain('g'))
    {
        setSnapEnabled(!snap_);
        return true;
    }
    if (plain('q'))
    {
        quantiseSelection(quantiseStrength_, false);
        return true;
    }
    if (key == juce::KeyPress('q', juce::ModifierKeys::shiftModifier, 0) || key == juce::KeyPress('Q'))
    {
        quantiseSelection(quantiseStrength_, true);
        return true;
    }
    if (plain('[') || plain(']'))
    {
        setGridDivision(plain('[') ? gridDivision_ / 2 : gridDivision_ * 2);
        return true;
    }
    if (key.getTextCharacter() == '+' || key.getTextCharacter() == '=' || key.getTextCharacter() == '-')
    {
        transposeSelection(key.getTextCharacter() == '-' ? -1 : 1);
        return true;
    }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        nudgeSelection(key == juce::KeyPress::leftKey ? -1 : 1, 0, key.getModifiers().isShiftDown());
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        const int step = key.getModifiers().isCtrlDown() ? 12 : 1;
        nudgeSelection(0, key == juce::KeyPress::upKey ? step : -step);
        return true;
    }
    return false;
}

bool PianoRoll::keyPressed(const juce::KeyPress& key)
{
    return handleEditKey(key);
}

// ----- the controller lane --------------------------------------------------------------------

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
    if (lane == lane_)
        return;
    lane_ = lane;
    controllerLane_.setLane(lane_);
    controllerLane_.setVisible(!lane_.velocity);
    // A controller lane is taller: its values want the room.
    velocityHeight_ = lane_.velocity ? 80 : 120;
    resized();
    repaint();
}

void PianoRoll::setScaleHighlight(bool on)
{
    scaleHighlight_ = on;
    scaleButton_.setToggleState(on, juce::dontSendNotification);
    repaint();
}

juce::Rectangle<int> PianoRoll::laneLabelArea() const noexcept
{
    return {0, velocityArea().getY() + 1, keyboardWidth, theme_.sizes.controlCompact};
}

int PianoRoll::yForValue(int value) const noexcept
{
    return controllerLane_.getY() + controllerLane_.yForValue(value);
}

void PianoRoll::showLaneMenu()
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
    juce::PopupMenu menu;
    menu.addSectionHeader("Lane");
    for (int i = 0; i < 6; ++i)
        menu.addItem(1 + i, label(fixed[i]), true, lane_ == fixed[i]);
    juce::PopupMenu others;
    for (int number = 0; number < 128; ++number)
    {
        const auto lane = Lane::controller(core::MidiControllerType::controller, number);
        others.addItem(100 + number, label(lane), true, lane_ == lane);
    }
    menu.addSubMenu("Other CC", others);
    juce::Component::SafePointer<PianoRoll> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(laneLabelArea())),
                       [safe, fixed](int choice)
                       {
                           if (safe == nullptr || choice == 0)
                               return;
                           if (choice >= 100)
                               safe->setLane(
                                   Lane::controller(core::MidiControllerType::controller, choice - 100));
                           else if (choice >= 1 && choice <= 6)
                               safe->setLane(fixed[choice - 1]);
                       });
}

void PianoRoll::deleteControllerSelection()
{
    controllerLane_.deleteSelection();
}

} // namespace northlight::ui
