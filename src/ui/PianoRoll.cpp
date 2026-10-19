#include "ui/PianoRoll.h"

#include <algorithm>
#include <cmath>

namespace aural::ui
{

namespace
{
constexpr int lowestRow = 0;
constexpr int rows = 128;
constexpr core::SamplePosition minNoteLength = 1;

bool isBlackKey(int note) noexcept
{
    switch (note % 12)
    {
        case 1:
        case 3:
        case 6:
        case 8:
        case 10:
            return true;
        default:
            return false;
    }
}
} // namespace

PianoRoll::PianoRoll()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    for (auto* bar : {&horizontal_, &vertical_})
    {
        bar->setAutoHide(false);
        bar->addListener(this);
        addAndMakeVisible(*bar);
    }
    verticalScroll_ = (rows - 84) * rowHeight; // start around C3..C6
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
    trackId_ = trackId;
    clipId_ = clipId;
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
    if (relative != playhead_)
    {
        playhead_ = relative;
        repaint();
    }
}

const core::MidiClip* PianoRoll::clip() const noexcept
{
    if (project_ == nullptr)
        return nullptr;
    const auto* track = project_->findTrack(trackId_);
    return track != nullptr ? track->findMidiClip(clipId_) : nullptr;
}

// ----- geometry -------------------------------------------------------------------------------

juce::Rectangle<int> PianoRoll::gridArea() const noexcept
{
    return {keyboardWidth,
            rulerHeight,
            std::max(0, getWidth() - keyboardWidth - scrollbarSize),
            std::max(0, getHeight() - rulerHeight - velocityHeight - scrollbarSize)};
}

juce::Rectangle<int> PianoRoll::velocityArea() const noexcept
{
    return {keyboardWidth,
            getHeight() - scrollbarSize - velocityHeight,
            std::max(0, getWidth() - keyboardWidth - scrollbarSize),
            velocityHeight};
}

int PianoRoll::xForSample(core::SamplePosition s) const noexcept
{
    return keyboardWidth + static_cast<int>(std::floor(static_cast<double>(s - scroll_) / samplesPerPixel_));
}

core::SamplePosition PianoRoll::sampleForX(int x) const noexcept
{
    return scroll_ + static_cast<core::SamplePosition>(std::floor((x - keyboardWidth) * samplesPerPixel_));
}

int PianoRoll::yForNote(int note) const noexcept
{
    return rulerHeight + (127 - note) * rowHeight - verticalScroll_;
}

int PianoRoll::noteForY(int y) const noexcept
{
    const int row = (y - rulerHeight + verticalScroll_) / rowHeight;
    return std::clamp(127 - row, 0, 127);
}

core::SamplePosition PianoRoll::gridSamples() const noexcept
{
    if (project_ == nullptr)
        return 1;
    const double beat =
        core::samplesPerBeat(project_->sampleRate, project_->tempoBpm, project_->timeSignature);
    return std::max<core::SamplePosition>(
        1, static_cast<core::SamplePosition>(std::llround(beat / std::max(1, gridDivision_))));
}

core::SamplePosition PianoRoll::snapped(core::SamplePosition s) const noexcept
{
    s = std::max<core::SamplePosition>(0, s);
    return snap_ ? core::snapToGrid(s, gridSamples()) : s;
}

juce::Rectangle<int> PianoRoll::noteBounds(const core::MidiNote& note) const noexcept
{
    const int x0 = xForSample(note.start);
    const int x1 = std::max(x0 + 2, xForSample(note.end()));
    return {x0, yForNote(note.note), x1 - x0, rowHeight};
}

core::MidiNote PianoRoll::previewOf(const core::MidiNote& note) const noexcept
{
    core::MidiNote p = note;
    if (drag_ == Drag::move && draggedFar_ && isSelected(note.id))
    {
        p.start = std::max<core::SamplePosition>(0, note.start + moveDelta_);
        p.note = std::clamp(note.note + pitchDelta_, 0, 127);
    }
    else if (drag_ == Drag::resize && draggedFar_ && note.id == dragNote_)
        p.length = lengthPreview_;
    else if (drag_ == Drag::velocity && draggedFar_ && (note.id == dragNote_ || isSelected(note.id)))
        p.velocity = velocityPreview_;
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
        const int distance = std::abs(x + 2 - p.x);
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
    horizontal_.setRangeLimits(0.0, std::max(length, static_cast<double>(scroll_) + visible));
    horizontal_.setCurrentRange(static_cast<double>(scroll_), visible);
    vertical_.setRangeLimits(0.0, rows * rowHeight);
    vertical_.setCurrentRange(verticalScroll_, gridArea().getHeight());
}

void PianoRoll::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (updatingScrollbars_)
        return;
    if (bar == &horizontal_)
        scroll_ = static_cast<core::SamplePosition>(newRangeStart);
    else
        verticalScroll_ = static_cast<int>(newRangeStart);
    repaint();
}

void PianoRoll::resized()
{
    horizontal_.setBounds(keyboardWidth,
                          getHeight() - scrollbarSize,
                          std::max(0, getWidth() - keyboardWidth - scrollbarSize),
                          scrollbarSize);
    vertical_.setBounds(getWidth() - scrollbarSize, rulerHeight, scrollbarSize, gridArea().getHeight());
    updateScrollbars();
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown())
    {
        const auto anchor = sampleForX(e.x);
        const double factor = wheel.deltaY > 0 ? 0.8 : 1.25;
        samplesPerPixel_ = std::clamp(samplesPerPixel_ * factor, 1.0, 100000.0);
        scroll_ = std::max<core::SamplePosition>(
            0, anchor - static_cast<core::SamplePosition>((e.x - keyboardWidth) * samplesPerPixel_));
    }
    else if (e.mods.isShiftDown())
    {
        scroll_ = std::max<core::SamplePosition>(
            0, scroll_ - static_cast<core::SamplePosition>(wheel.deltaY * 200.0 * samplesPerPixel_));
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

// ----- painting -------------------------------------------------------------------------------

juce::String PianoRoll::noteName(int note)
{
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[note % 12]) + juce::String(note / 12 - 2);
}

void PianoRoll::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.background);
    const auto grid = gridArea();
    const auto velocity = velocityArea();
    const auto* clipModel = clip();

    // Rows: alternating by key colour, with a line at each C.
    g.saveState();
    g.reduceClipRegion(grid);
    for (int note = 0; note < rows; ++note)
    {
        const int y = yForNote(note);
        if (y + rowHeight < grid.getY() || y > grid.getBottom())
            continue;
        g.setColour(isBlackKey(note) ? c.surface : c.surface.brighter(0.08f));
        g.fillRect(grid.getX(), y, grid.getWidth(), rowHeight);
        if (note % 12 == 0)
        {
            g.setColour(c.borderStrong);
            g.fillRect(grid.getX(), y + rowHeight - 1, grid.getWidth(), 1);
        }
    }
    // Grid lines: beats strong, subdivisions faint, bars stronger.
    if (project_ != nullptr && clipModel != nullptr)
    {
        const double beat =
            core::samplesPerBeat(project_->sampleRate, project_->tempoBpm, project_->timeSignature);
        const auto step = gridSamples();
        const int beatsPerBar = std::max(1, project_->timeSignature.numerator);
        // Grid relative to the timeline's bars, not the clip start.
        const auto first = sampleForX(grid.getX());
        const auto last = sampleForX(grid.getRight());
        const auto absoluteFirst = clipModel->start + first;
        core::SamplePosition s =
            core::snapToGrid(std::max<core::SamplePosition>(0, absoluteFirst - step), step);
        const bool subdivisions = static_cast<double>(step) / samplesPerPixel_ >= 6.0;
        for (; s - clipModel->start <= last; s += step)
        {
            const auto relative = s - clipModel->start;
            if (relative < first)
                continue;
            const double beats = static_cast<double>(s) / beat;
            const bool onBeat = std::abs(beats - std::round(beats)) < 1.0e-6;
            const bool onBar = onBeat && (static_cast<long long>(std::llround(beats)) % beatsPerBar) == 0;
            if (!onBeat && !subdivisions)
                continue;
            g.setColour(onBar ? c.borderStrong : onBeat ? c.border : c.border.withAlpha(0.4f));
            g.fillRect(xForSample(relative), grid.getY(), 1, grid.getHeight());
        }
        // The clip's end.
        g.setColour(c.warning.withAlpha(0.6f));
        g.fillRect(xForSample(clipModel->length), grid.getY(), 1, grid.getHeight());
        g.setColour(c.background.withAlpha(0.5f));
        g.fillRect(xForSample(clipModel->length) + 1, grid.getY(), grid.getRight(), grid.getHeight());
    }

    // Notes.
    if (clipModel != nullptr)
    {
        for (const auto& model : clipModel->notes)
        {
            const auto note = previewOf(model);
            const auto bounds = noteBounds(note).reduced(0, 1);
            if (bounds.getRight() < grid.getX() || bounds.getX() > grid.getRight())
                continue;
            const bool selected = isSelected(model.id);
            const float strength = 0.35f + 0.65f * static_cast<float>(note.velocity) / 127.0f;
            g.setColour((selected ? c.accentHover : c.accent).withMultipliedBrightness(strength));
            g.fillRoundedRectangle(bounds.toFloat(), 2.0f);
            g.setColour(selected ? c.textPrimary : c.accentText.withAlpha(0.6f));
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 2.0f, selected ? 1.5f : 1.0f);
        }
        // Ghosts of what a lasso covers are shown by selection on release; a move
        // shows the preview in place (previewOf).
    }
    if (drag_ == Drag::lasso && draggedFar_)
    {
        g.setColour(c.accent.withAlpha(0.15f));
        g.fillRect(lasso_);
        g.setColour(c.accent);
        g.drawRect(lasso_, 1);
    }
    if (playhead_ >= 0)
    {
        g.setColour(c.textPrimary);
        g.fillRect(xForSample(playhead_), grid.getY(), 1, grid.getHeight());
    }
    g.restoreState();

    // Keyboard.
    g.saveState();
    g.reduceClipRegion(0, rulerHeight, keyboardWidth, grid.getHeight());
    for (int note = 0; note < rows; ++note)
    {
        const int y = yForNote(note);
        if (y + rowHeight < grid.getY() || y > grid.getBottom())
            continue;
        const bool black = isBlackKey(note);
        g.setColour(black ? c.surfacePressed : c.textPrimary.withAlpha(0.85f));
        g.fillRect(0, y, keyboardWidth - 1, rowHeight - 1);
        if (note % 12 == 0)
        {
            g.setColour(c.background);
            g.setFont(juce::FontOptions{theme_.type.caption});
            g.drawText(noteName(note), 4, y, keyboardWidth - 8, rowHeight, juce::Justification::centredRight);
        }
    }
    g.restoreState();

    // Ruler: bar.beat labels at bars, ticks at beats (timeline positions).
    g.setColour(c.surface);
    g.fillRect(0, 0, getWidth(), rulerHeight);
    g.setColour(c.border);
    g.fillRect(0, rulerHeight - 1, getWidth(), 1);
    g.fillRect(keyboardWidth - 1, 0, 1, getHeight());
    if (project_ != nullptr && clipModel != nullptr)
    {
        g.saveState();
        g.reduceClipRegion(grid.withY(0).withHeight(rulerHeight));
        const double beat =
            core::samplesPerBeat(project_->sampleRate, project_->tempoBpm, project_->timeSignature);
        const int beatsPerBar = std::max(1, project_->timeSignature.numerator);
        const auto first = clipModel->start + sampleForX(grid.getX());
        const auto last = clipModel->start + sampleForX(grid.getRight());
        const bool showBeats = beat / samplesPerPixel_ >= 24.0;
        g.setFont(juce::FontOptions{theme_.type.caption});
        for (long long b = static_cast<long long>(std::floor(static_cast<double>(first) / beat));
             b * beat <= static_cast<double>(last);
             ++b)
        {
            if (b < 0)
                continue;
            const bool bar = b % beatsPerBar == 0;
            if (!bar && !showBeats)
                continue;
            const int x =
                xForSample(static_cast<core::SamplePosition>(std::llround(b * beat)) - clipModel->start);
            g.setColour(bar ? c.textPrimary : c.textDisabled);
            g.fillRect(x, rulerHeight - (bar ? 12 : 6), 1, bar ? 12 : 6);
            if (bar)
                g.drawText(juce::String(b / beatsPerBar + 1),
                           x + 3,
                           2,
                           40,
                           rulerHeight - 14,
                           juce::Justification::centredLeft);
        }
        g.restoreState();
    }
    // Grid and snap state in the ruler's corner.
    g.setFont(juce::FontOptions{theme_.type.caption});
    g.setColour(snap_ ? c.textPrimary : c.textDisabled);
    g.drawText("1/" + juce::String(gridDivision_ * 4) + (snap_ ? "" : " off"),
               2,
               0,
               keyboardWidth - 4,
               rulerHeight,
               juce::Justification::centred);

    // Velocity lane.
    g.setColour(c.surface);
    g.fillRect(velocity.withX(0).withWidth(getWidth()));
    g.setColour(c.border);
    g.fillRect(0, velocity.getY(), getWidth(), 1);
    g.setColour(c.textSecondary);
    g.drawText("Vel", 0, velocity.getY(), keyboardWidth - 4, 20, juce::Justification::centredRight);
    if (clipModel != nullptr)
    {
        g.saveState();
        g.reduceClipRegion(velocity);
        for (const auto& model : clipModel->notes)
        {
            const auto note = previewOf(model);
            const int x = xForSample(note.start);
            if (x < velocity.getX() - 4 || x > velocity.getRight())
                continue;
            const int h = (velocity.getHeight() - 8) * note.velocity / 127;
            const bool selected = isSelected(model.id);
            g.setColour(selected ? c.accentHover : c.accent.withAlpha(0.8f));
            g.fillRect(x, velocity.getBottom() - 4 - h, 4, h);
            g.setColour(selected ? c.textPrimary : c.accentHover);
            g.fillEllipse(static_cast<float>(x) - 1.0f,
                          static_cast<float>(velocity.getBottom() - 4 - h) - 3.0f,
                          6.0f,
                          6.0f);
        }
        g.restoreState();
    }

    if (clipModel == nullptr)
    {
        g.setColour(c.textDisabled);
        g.setFont(juce::FontOptions{theme_.type.title});
        g.drawText("No MIDI clip", grid, juce::Justification::centred);
    }
}

// ----- mouse ----------------------------------------------------------------------------------

void PianoRoll::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    draggedFar_ = false;
    dragStart_ = e.getPosition();
    const auto* c = clip();
    if (c == nullptr)
        return;

    if (velocityArea().contains(e.getPosition()))
    {
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

    bool onEdge = false;
    const auto* note = noteAt(e.getPosition(), onEdge);
    if (note == nullptr)
    {
        if (e.mods.isAltDown() || e.mods.isCtrlDown())
            return;
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
        // Toggles on release unless dragged.
        if (!isSelected(note->id))
            selection_.push_back(note->id);
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
    repaint();
}

void PianoRoll::mouseDrag(const juce::MouseEvent& e)
{
    if (drag_ == Drag::none)
        return;
    draggedFar_ = draggedFar_ || e.mouseWasDraggedSinceMouseDown();
    const auto* c = clip();
    if (c == nullptr)
        return;
    const int x = std::clamp(e.x, keyboardWidth, getWidth() - scrollbarSize);
    const auto at = sampleForX(x);
    switch (drag_)
    {
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
            velocityPreview_ =
                std::clamp((area.getBottom() - 4 - e.y) * 127 / std::max(1, area.getHeight() - 8), 1, 127);
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
    switch (drag)
    {
        case Drag::move:
            if (draggedFar_)
                finishMove();
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
            else
            {
                if (!e.mods.isShiftDown())
                    selection_.clear();
                addNoteAt(e.getPosition());
            }
            break;
        case Drag::velocity:
            if (draggedFar_)
                finishVelocity();
            break;
        case Drag::none:
            break;
    }
    repaint();
}

void PianoRoll::mouseMove(const juce::MouseEvent& e)
{
    if (drag_ != Drag::none)
        return;
    bool onEdge = false;
    const auto* note = noteAt(e.getPosition(), onEdge);
    setMouseCursor(note == nullptr ? juce::MouseCursor::NormalCursor
                   : onEdge        ? juce::MouseCursor::LeftRightResizeCursor
                                   : juce::MouseCursor::DraggingHandCursor);
}

void PianoRoll::mouseDoubleClick(const juce::MouseEvent& e)
{
    // Double-click on a note removes it.
    bool onEdge = false;
    if (const auto* note = noteAt(e.getPosition(), onEdge))
    {
        std::vector<std::unique_ptr<core::Command>> commands;
        commands.push_back(std::make_unique<core::RemoveNote>(trackId_, clipId_, note->id));
        std::erase(selection_, note->id);
        emit(std::move(commands), "Remove note");
    }
}

// ----- edits ----------------------------------------------------------------------------------

void PianoRoll::emit(std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
{
    if (!commands.empty() && onEdit)
        onEdit(std::move(commands), name);
}

void PianoRoll::addNoteAt(juce::Point<int> p)
{
    const auto* c = clip();
    if (c == nullptr || !gridArea().contains(p))
        return;
    core::MidiNote note;
    note.note = noteForY(p.y);
    note.velocity = 100;
    note.start = snapped(sampleForX(p.x));
    note.length = gridSamples();
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

void PianoRoll::finishMove()
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
            commands.push_back(std::make_unique<core::SetNote>(trackId_, clipId_, moved));
        }
    emit(std::move(commands), commands.size() > 1 ? "Move notes" : "Move note");
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
    emit(std::move(commands), "Resize notes");
}

void PianoRoll::finishVelocity()
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    for (const auto& note : c->notes)
        if (note.id == dragNote_ || isSelected(note.id))
        {
            if (note.velocity == velocityPreview_)
                continue;
            auto changed = note;
            changed.velocity = velocityPreview_;
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
    const auto span = std::max<core::SamplePosition>(gridSamples(), snapped(last - first));
    std::vector<std::unique_ptr<core::Command>> commands;
    for (auto note : notes)
    {
        note.id = core::invalidId;
        note.start += span;
        commands.push_back(std::make_unique<core::AddNote>(trackId_, clipId_, note));
    }
    emit(std::move(commands), "Duplicate notes");
}

void PianoRoll::quantiseSelection(float strength)
{
    if (clip() == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(
        std::make_unique<core::QuantiseNotes>(trackId_, clipId_, selection_, gridSamples(), strength));
    emit(std::move(commands), "Quantise notes");
}

void PianoRoll::nudgeSelection(int timeSteps, int semitones)
{
    const auto* c = clip();
    if (c == nullptr || selection_.empty())
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    const auto step = gridSamples() * timeSteps;
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

void PianoRoll::setGridDivision(int division)
{
    gridDivision_ = std::clamp(division, 1, 32);
    repaint();
}

void PianoRoll::setSnapEnabled(bool on)
{
    snap_ = on;
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
        deleteSelection();
        return true;
    }
    if (ctrl('a'))
    {
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
        quantiseSelection(1.0f);
        return true;
    }
    if (key == juce::KeyPress('q', juce::ModifierKeys::shiftModifier, 0) || key == juce::KeyPress('Q'))
    {
        quantiseSelection(0.5f);
        return true;
    }
    if (plain('[') || plain(']'))
    {
        setGridDivision(plain('[') ? gridDivision_ / 2 : gridDivision_ * 2);
        return true;
    }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        nudgeSelection(key == juce::KeyPress::leftKey ? -1 : 1, 0);
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        const int step = key.getModifiers().isShiftDown() ? 12 : 1;
        nudgeSelection(0, key == juce::KeyPress::upKey ? step : -step);
        return true;
    }
    return false;
}

bool PianoRoll::keyPressed(const juce::KeyPress& key)
{
    return handleEditKey(key);
}

} // namespace aural::ui
