#include "ui/PianoRoll.h"

#include "ui/AuralLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace aural::ui
{

namespace
{
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
    : tools_(theme_), link_(theme_, Icon::link, "Link zoom to the arrange view"),
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

    tools_.setSegments({"Draw", "Select", "Erase"});
    tools_.setSelected(1, juce::dontSendNotification);
    tools_.onChange = [this](int index)
    {
        setTool(index == 0 ? Tool::draw : index == 2 ? Tool::erase : Tool::select);
    };
    addAndMakeVisible(tools_);
    snapButton_.getProperties().set(AuralLookAndFeel::propertyVariant, "ghost");
    snapButton_.getProperties().set(AuralLookAndFeel::propertyMenu, true);
    snapButton_.setTooltip("Snap grid");
    snapButton_.onClick = [this] { showSnapMenu(); };
    addAndMakeVisible(snapButton_);
    quantiseButton_.setButtonText("Quantise");
    quantiseButton_.getProperties().set(AuralLookAndFeel::propertyVariant, "ghost");
    quantiseButton_.getProperties().set(AuralLookAndFeel::propertyMenu, true);
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
    refreshToolbar();
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
    // The playhead's columns and the keyboard (its lit keys) repaint; nothing else.
    if (oldX >= 0)
        repaint(oldX - 2, 0, 5, getHeight());
    if (x >= 0)
        repaint(x - 2, 0, 5, getHeight());
    repaint(keysArea());
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
    tools_.setSelected(tool == Tool::draw ? 0 : tool == Tool::erase ? 2 : 1, juce::dontSendNotification);
    setMouseCursor(tool == Tool::draw ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    repaint();
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
    return theme_.track.forTrack(t->colour, project_->trackIndex(t->id));
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
    return keyboardWidth + static_cast<int>(std::floor(static_cast<double>(s - scroll_) / samplesPerPixel_));
}

core::SamplePosition PianoRoll::sampleForX(int x) const noexcept
{
    return scroll_ + static_cast<core::SamplePosition>(std::floor((x - keyboardWidth) * samplesPerPixel_));
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

core::GridStep PianoRoll::gridStep() const noexcept
{
    const auto* c = clip();
    const auto at = c != nullptr ? c->start : 0;
    return {0, core::TempoMap::quartersPerBeat(tempoMap_.signatureAt(at)) / std::max(1, gridDivision_)};
}

core::SamplePosition PianoRoll::gridSamples() const noexcept
{
    const auto* c = clip();
    const auto at = c != nullptr ? c->start : 0;
    return std::max<core::SamplePosition>(1, std::llround(tempoMap_.stepSamples(gridStep(), at)));
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
    return std::max<core::SamplePosition>(0, tempoMap_.snap(start + s, gridStep()) - start);
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
        scroll_ = static_cast<core::SamplePosition>(newRangeStart);
    else
        verticalScroll_ = static_cast<int>(newRangeStart);
    repaint();
}

void PianoRoll::resized()
{
    if (fitPending_ && gridArea().getWidth() > 16)
        fitToClip();
    auto bar = getLocalBounds()
                   .removeFromTop(toolbarHeight)
                   .reduced(theme_.spacing.sm, (toolbarHeight - theme_.sizes.controlCompact) / 2);
    tools_.setBounds(bar.removeFromLeft(std::min(bar.getWidth(), tools_.preferredWidth())));
    bar.removeFromLeft(theme_.spacing.sm);
    snapButton_.setBounds(bar.removeFromLeft(72));
    bar.removeFromLeft(theme_.spacing.sm);
    quantiseButton_.setBounds(bar.removeFromLeft(96));
    bar.removeFromLeft(theme_.spacing.sm);
    link_.setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
    bar.removeFromLeft(theme_.spacing.xs);
    auditionButton_.setBounds(bar.removeFromLeft(theme_.sizes.controlCompact));
    horizontal_.setBounds(keyboardWidth,
                          getHeight() - scrollbarSize_,
                          std::max(0, getWidth() - keyboardWidth - scrollbarSize_),
                          scrollbarSize_);
    vertical_.setBounds(
        getWidth() - scrollbarSize_, gridArea().getY(), scrollbarSize_, gridArea().getHeight());
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
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[note % 12]) + juce::String(note / 12 - 2);
}

void PianoRoll::drawTooltip(juce::Graphics& g, const juce::String& text, juce::Point<int> at)
{
    const auto& c = theme_.colours;
    g.setFont(theme_.caption());
    const int w = juce::GlyphArrangement::getStringWidthInt(theme_.caption(), text) + 2 * theme_.spacing.sm;
    const int h = theme_.sizes.controlCompact;
    auto box = juce::Rectangle<int>(at.x + 14, at.y + 18, w, h);
    if (box.getRight() > getWidth() - scrollbarSize_)
        box.setX(at.x - w - 6);
    g.setColour(c.surface2);
    g.fillRoundedRectangle(box.toFloat(), theme_.radius.sm);
    g.setColour(c.lineStrong);
    g.drawRoundedRectangle(box.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
    g.setColour(c.textPrimary);
    g.drawText(text, box, juce::Justification::centred);
}

void PianoRoll::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.canvas);
    const auto grid = gridArea();
    const auto velocity = velocityArea();
    const auto ruler = rulerArea();
    const auto keys = keysArea();
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
        if (y + rowHeight < grid.getY() || y > grid.getBottom())
            continue;
        if (isBlackKey(note))
        {
            g.setColour(c.laneAlt);
            g.fillRect(grid.getX(), y, grid.getWidth(), rowHeight);
        }
        if (key.isSet() && !key.contains(note))
        {
            g.setColour(c.canvas.withAlpha(0.45f));
            g.fillRect(grid.getX(), y, grid.getWidth(), rowHeight);
        }
        if (note % 12 == 0)
        {
            g.setColour(c.gridBar);
            g.fillRect(grid.getX(), y + rowHeight - 1, grid.getWidth(), 1);
        }
    }
    // Grid lines: bars, beats, subdivisions, relative to the timeline's bars.
    if (project_ != nullptr && clipModel != nullptr)
    {
        const auto& map = tempoMap_;
        const auto step = gridStep();
        const auto first = sampleForX(grid.getX());
        const auto last = sampleForX(grid.getRight());
        const auto absoluteFirst = std::max<core::SamplePosition>(0, clipModel->start + first);
        const bool subdivisions = static_cast<double>(gridSamples()) / samplesPerPixel_ >= 6.0;
        auto s = map.snap(absoluteFirst, step);
        for (int guard = 0; s - clipModel->start <= last && guard < 100000; ++guard)
        {
            const auto here = s;
            s = std::max(here + 1, map.snap(map.move(here, step, 1), step));
            const auto relative = here - clipModel->start;
            if (relative < first)
                continue;
            // Where the line falls in its bar, in beats of the bar's signature.
            const double q = map.quartersAt(static_cast<double>(here));
            const int bar = map.barAtQuarter(q + 1.0e-6);
            const double beats =
                (q - map.barStartQuarter(bar)) / core::TempoMap::quartersPerBeat(map.signatureAt(here));
            const bool onBeat = std::abs(beats - std::round(beats)) < 1.0e-4;
            const bool onBar = onBeat && std::llround(beats) == 0;
            if (!onBeat && !subdivisions)
                continue;
            g.setColour(onBar ? c.gridBar : onBeat ? c.gridBeat : c.gridSub);
            g.fillRect(xForSample(relative), grid.getY(), 1, grid.getHeight());
        }
        // The clip's end: beyond it the grid dims.
        g.setColour(c.canvas.withAlpha(0.5f));
        g.fillRect(xForSample(clipModel->length) + 1, grid.getY(), grid.getRight(), grid.getHeight());
        g.setColour(c.lineStrong);
        g.fillRect(xForSample(clipModel->length), grid.getY(), 1, grid.getHeight());
    }

    // Ghost notes: the track's other clips at 30%.
    if (trackModel != nullptr && clipModel != nullptr)
        for (const auto& other : trackModel->midiClips)
        {
            if (other.id == clipModel->id)
                continue;
            for (const auto& n : other.notes)
            {
                core::MidiNote shifted = n;
                shifted.start = other.start + n.start - clipModel->start;
                const auto bounds = noteBounds(shifted).reduced(0, 1);
                if (bounds.getRight() < grid.getX() || bounds.getX() > grid.getRight())
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
            if (bounds.getRight() < grid.getX() || bounds.getX() > grid.getRight())
                continue;
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
    std::vector<bool> lit(rows, false);
    if (clipModel != nullptr && playhead_ >= 0)
        for (const auto& n : clipModel->notes)
            if (playhead_ >= n.start && playhead_ < n.end())
                lit[static_cast<size_t>(n.note)] = true;
    if (heldKey_ >= 0)
        lit[static_cast<size_t>(heldKey_)] = true;
    for (int note = 0; note < rows; ++note)
    {
        const int y = yForNote(note);
        if (y + rowHeight < keys.getY() || y > keys.getBottom())
            continue;
        const bool black = isBlackKey(note);
        g.setColour(lit[static_cast<size_t>(note)] ? c.accent : black ? c.surface0 : c.surface2);
        g.fillRect(0, y, black ? keyboardWidth * 2 / 3 : keyboardWidth - 1, rowHeight - 1);
        if (note % 12 == 0)
        {
            g.setColour(lit[static_cast<size_t>(note)] ? c.textOnAccent : c.textSecondary);
            g.setFont(theme_.caption());
            g.drawText(noteName(note), 4, y, keyboardWidth - 8, rowHeight, juce::Justification::centredRight);
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
    if (project_ != nullptr && clipModel != nullptr)
    {
        g.saveState();
        g.reduceClipRegion(ruler);
        const auto& map = tempoMap_;
        const auto first = std::max<core::SamplePosition>(0, clipModel->start + sampleForX(grid.getX()));
        const auto last = clipModel->start + sampleForX(grid.getRight());
        const double beatPixels =
            map.stepSamples({0, core::TempoMap::quartersPerBeat(map.signatureAt(first))}, first) /
            samplesPerPixel_;
        const bool showBeats = beatPixels >= 24.0;
        g.setFont(theme_.caption());
        auto beatAt = map.beatAtOrAfter(static_cast<double>(first));
        for (int guard = 0; beatAt.position <= last && guard < 100000; ++guard)
        {
            const auto here = beatAt;
            beatAt = map.nextBeat(here);
            const bool bar = here.beat == 0;
            if (!bar && !showBeats)
                continue;
            const int x = xForSample(here.position - clipModel->start);
            g.setColour(bar ? c.gridBar : c.gridBeat);
            g.fillRect(x, ruler.getBottom() - (bar ? ruler.getHeight() : 6), 1, bar ? ruler.getHeight() : 6);
            if (bar)
            {
                g.setColour(c.rulerText);
                g.drawText(juce::String(here.bar + 1),
                           x + 4,
                           ruler.getY(),
                           40,
                           ruler.getHeight(),
                           juce::Justification::centredLeft);
            }
        }
        g.restoreState();
    }

    // Velocity lane: a bar per note with a 6 px cap.
    g.setColour(c.surface0);
    g.fillRect(velocity.withX(0).withWidth(getWidth()));
    g.setColour(c.lineSubtle);
    g.fillRect(0, velocity.getY(), getWidth(), 1);
    g.setColour(c.textTertiary);
    g.setFont(theme_.captionCaps());
    g.drawText("VEL",
               0,
               velocity.getY(),
               keyboardWidth - theme_.spacing.sm,
               theme_.sizes.controlCompact,
               juce::Justification::centredRight);
    if (clipModel != nullptr)
    {
        g.saveState();
        g.reduceClipRegion(velocity);
        for (const auto& model : clipModel->notes)
        {
            const auto note = previewOf(model);
            const int x = xForSample(note.start);
            if (x < velocity.getX() - 6 || x > velocity.getRight())
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
    if (tool_ == Tool::draw && note == nullptr)
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
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
    // In Select mode a double-click on empty grid adds a note.
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
    const auto step = gridStep();
    commands.push_back(
        std::make_unique<core::QuantiseNotes>(trackId_, clipId_, selection_, tempoMap_, step, strength));
    const auto snapRelative = [&](core::SamplePosition s)
    { return tempoMap_.snap(c->start + s, step) - c->start; };
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
    auto grid = gridStep();
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

} // namespace aural::ui
