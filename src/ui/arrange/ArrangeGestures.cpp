#include "ui/arrange/ArrangeGestures.h"

#include "core/Command.h"
#include "ui/Db.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui
{

// ----- the view ----------------------------------------------------------------------

void ArrangeView::HeaderResizeGesture::drag(const juce::MouseEvent& e, core::SamplePosition)
{
    v.view_.originX = std::clamp(e.x, v.theme_.sizes.trackHeaderMinWidth, v.theme_.sizes.trackHeaderMaxWidth);
    v.resized();
    v.layoutHeaders();
}

ArrangeView::PanGesture::PanGesture(ArrangeView& view) noexcept
    : Gesture(view), scroll_(view.view_.first), vertical_(view.verticalScroll_)
{
}

void ArrangeView::PanGesture::drag(const juce::MouseEvent& e, core::SamplePosition)
{
    v.setScroll(scroll_ -
                static_cast<core::SamplePosition>((e.x - v.dragStart_.x) * v.view_.samplesPerPixel));
    v.setVerticalScroll(vertical_ - (e.y - v.dragStart_.y));
}

ArrangeView::RowResizeGesture::RowResizeGesture(ArrangeView& view, int row) : Gesture(view)
{
    v.resizeStartHeight_ = v.rowHeightOf(row);
    v.resizeHeight_ = v.resizeStartHeight_;
    v.resizeChannel_ = v.channelAtRow(row)->id;
    v.rowsValid_ = false;
}

void ArrangeView::RowResizeGesture::drag(const juce::MouseEvent& e, core::SamplePosition)
{
    const int h = std::clamp(
        v.resizeStartHeight_ + e.y - v.dragStart_.y, v.theme_.sizes.trackRowMin, v.theme_.sizes.trackRowMax);
    if (h == v.resizeHeight_)
        return;
    v.resizeHeight_ = h;
    v.rowsValid_ = false;
    v.layoutHeaders();
    v.updateScrollbar();
    v.automation_.repaint();
}

void ArrangeView::RowResizeGesture::release(const juce::MouseEvent&)
{
    const auto channel = std::exchange(v.resizeChannel_, core::invalidId);
    v.rowsValid_ = false;
    if (v.resizeHeight_ != v.resizeStartHeight_ && v.project_ != nullptr &&
        v.project_->findChannel(channel) != nullptr)
    {
        std::vector<std::unique_ptr<core::Command>> commands;
        commands.push_back(std::make_unique<core::SetChannelHeight>(channel, v.resizeHeight_));
        v.emit(std::move(commands), "Resize track");
    }
    v.layoutHeaders();
    v.updateScrollbar();
}

void ArrangeView::TakesGesture::drag(const juce::MouseEvent& e, core::SamplePosition)
{
    v.takes_.mouseDrag(e);
}

void ArrangeView::TakesGesture::release(const juce::MouseEvent& e)
{
    v.takes_.mouseUp(e);
}

ArrangeView::DrawGesture::DrawGesture(ArrangeView& view, core::Id track, core::SamplePosition at)
    : Gesture(view), track_(track)
{
    // From the grid line at or before the press, a bar long until dragged.
    const auto from = std::max<core::SamplePosition>(0, at);
    anchor_ = v.snapEnabled() ? v.lineAtOrBefore(from) : from;
    start_ = anchor_;
    end_ = v.tempoMap_.move(anchor_, {1, 0.0}, 1);
}

void ArrangeView::DrawGesture::drag(const juce::MouseEvent&, core::SamplePosition at)
{
    if (!v.draggedFar_)
        return;
    // Whole grid cells either way, at least one grid step long.
    const bool grid = v.snapEnabled();
    const auto cell = [this, grid](core::SamplePosition s, bool up)
    {
        s = std::max<core::SamplePosition>(0, s);
        return !grid ? s : up ? v.lineAtOrAfter(s) : v.lineAtOrBefore(s);
    };
    const auto oneStep =
        grid ? std::max(anchor_ + 1, v.tempoMap_.move(anchor_, v.gridAt(anchor_), 1)) : anchor_ + 1;
    if (at >= anchor_)
    {
        start_ = anchor_;
        end_ = std::max(oneStep, cell(at, true));
    }
    else
    {
        start_ = cell(at, false);
        end_ = oneStep;
    }
}

void ArrangeView::DrawGesture::release(const juce::MouseEvent&)
{
    v.addMidiClip(track_, start_, end_);
}

void ArrangeView::DrawGesture::paintLate(juce::Graphics& g)
{
    // The clip it will make, in the track's colour.
    const int row = v.project_ != nullptr ? v.project_->trackIndex(track_) : -1;
    if (row < 0)
        return;
    g.saveState();
    g.reduceClipRegion(v.lanesBounds());
    const auto r = v.spanBounds(start_, end_ - start_, row).toFloat().reduced(0.5f, 1.5f);
    const auto colour = v.trackColour(v.project_->tracks[static_cast<size_t>(row)], row);
    g.setColour(colour.withAlpha(0.35f));
    g.fillRoundedRectangle(r, v.theme_.radius.sm);
    g.setColour(colour);
    g.drawRoundedRectangle(r, v.theme_.radius.sm, 1.0f);
    g.restoreState();
}

// ----- the ruler ---------------------------------------------------------------------

ArrangeView::RegionGesture::RegionGesture(ArrangeView& view, core::SamplePosition at) noexcept
    : Gesture(view), anchor_(at), startSamplesPerPixel_(view.view_.samplesPerPixel)
{
}

void ArrangeView::RegionGesture::drag(const juce::MouseEvent& e, core::SamplePosition at)
{
    if (zooming_)
    {
        // A vertical drag zooms around where it began: 80 px per octave.
        const double factor = std::pow(2.0, static_cast<double>(e.y - v.dragStart_.y) / 80.0);
        v.setZoom(startSamplesPerPixel_ * factor, v.dragStart_.x);
        return;
    }
    if (!v.draggedFar_)
        return;
    // The first movement decides: mostly vertical zooms, otherwise the region.
    if (std::abs(e.y - v.dragStart_.y) > std::abs(e.x - v.dragStart_.x) && std::abs(e.y - v.dragStart_.y) > 6)
    {
        zooming_ = true;
        return;
    }
    v.regionStart_ = v.snapped(std::min(anchor_, at));
    v.regionEnd_ = v.snapped(std::max(anchor_, at));
}

void ArrangeView::RegionGesture::release(const juce::MouseEvent&)
{
    if (zooming_)
        return;
    if (v.draggedFar_)
    {
        if (v.onRegionChanged)
            v.onRegionChanged(v.regionStart_, v.regionEnd_);
    }
    else if (v.onLocate)
        v.onLocate(anchor_);
}

void ArrangeView::RegionGesture::paintTop(juce::Graphics& g)
{
    if (v.draggedFar_ && !zooming_)
        v.drawTooltip(g, v.positionText(v.regionStart_) + " - " + v.positionText(v.regionEnd_), v.dragPoint_);
}

ArrangeView::LoopGesture::LoopGesture(ArrangeView& view, Part part, core::SamplePosition at) noexcept
    : Gesture(view), part_(part), anchor_(at), start_(view.regionStart_), end_(view.regionEnd_)
{
}

void ArrangeView::LoopGesture::drag(const juce::MouseEvent&, core::SamplePosition at)
{
    switch (part_)
    {
        case Part::band:
        {
            const auto delta = v.snapped(start_ + (at - anchor_)) - start_;
            const auto start = std::max<core::SamplePosition>(0, start_ + delta);
            v.regionEnd_ = start + (end_ - start_);
            v.regionStart_ = start;
            break;
        }
        case Part::start:
            v.regionStart_ = std::min(v.snapped(at), end_ - 1);
            break;
        case Part::end:
            v.regionEnd_ = std::max(v.snapped(at), start_ + 1);
            break;
    }
}

void ArrangeView::LoopGesture::release(const juce::MouseEvent&)
{
    if (v.onRegionChanged)
        v.onRegionChanged(v.regionStart_, v.regionEnd_);
}

void ArrangeView::LoopGesture::paintTop(juce::Graphics& g)
{
    if (v.draggedFar_)
        v.drawTooltip(g, v.positionText(v.regionStart_) + " - " + v.positionText(v.regionEnd_), v.dragPoint_);
}

ArrangeView::TimeSelectGesture::TimeSelectGesture(ArrangeView& view, core::SamplePosition at, int row)
    : Gesture(view), anchor_(view.snapped(at)), row_(row), tracksBefore_(view.selectedTracks())
{
    v.timeSelStart_ = v.timeSelEnd_ = anchor_;
    v.finishTimeSelection(); // (empty: no tracks)
}

void ArrangeView::TimeSelectGesture::drag(const juce::MouseEvent& e, core::SamplePosition at)
{
    v.timeSelStart_ = std::min(anchor_, v.snapped(at));
    v.timeSelEnd_ = std::max(anchor_, v.snapped(at));
    // From a lane: the tracks between the press and the pointer are the selection
    // (the pressed one alone until the pointer leaves its row).
    if (row_ >= 0 && v.project_ != nullptr)
    {
        const int count = static_cast<int>(v.project_->tracks.size());
        const int now = v.rowAtY(e.y) >= 0 ? v.rowAtY(e.y) : (e.y < v.rulerHeight ? 0 : count - 1);
        std::vector<core::Id> tracks;
        for (int r = std::min(row_, now); r <= std::max(row_, now) && r < count; ++r)
            if (v.rowHeightOf(r) > 0)
                tracks.push_back(v.project_->tracks[static_cast<size_t>(r)].id);
        // The pressed track keeps the focus.
        const auto pressed = v.project_->tracks[static_cast<size_t>(std::min(row_, count - 1))].id;
        std::erase(tracks, pressed);
        tracks.insert(tracks.begin(), pressed);
        if (tracks != v.selectedTracks())
            v.selectTracks(tracks);
    }
    v.finishTimeSelection();
}

void ArrangeView::TimeSelectGesture::release(const juce::MouseEvent& e)
{
    if (!v.draggedFar_)
    {
        // A click: no range; it locates, as a click in a lane does.
        v.clearTimeSelection();
        if (v.onLocate)
            v.onLocate(anchor_);
        juce::ignoreUnused(e);
        return;
    }
    v.finishTimeSelection();
}

void ArrangeView::TimeSelectGesture::paintTop(juce::Graphics& g)
{
    if (v.draggedFar_)
        v.drawTooltip(
            g, v.positionText(v.timeSelStart_) + " - " + v.positionText(v.timeSelEnd_), v.dragPoint_);
}

void ArrangeView::MarkerGesture::drag(const juce::MouseEvent&, core::SamplePosition at)
{
    v.markers_.dragTo(at);
}

void ArrangeView::MarkerGesture::release(const juce::MouseEvent&)
{
    v.markers_.finishDrag(v.draggedFar_);
}

// ----- the lanes ---------------------------------------------------------------------

ArrangeView::LassoGesture::LassoGesture(ArrangeView& view,
                                        juce::Point<int> press,
                                        core::SamplePosition at) noexcept
    : Gesture(view), lasso_(press.x, press.y, 0, 0), anchor_(at)
{
}

void ArrangeView::LassoGesture::drag(const juce::MouseEvent& e, core::SamplePosition)
{
    const int x = std::clamp(e.x, v.headerWidth(), v.getWidth() - v.scrollbarWidth);
    lasso_ = juce::Rectangle<int>::leftTopRightBottom(std::min(v.dragStart_.x, x),
                                                      std::min(v.dragStart_.y, e.y),
                                                      std::max(v.dragStart_.x, x),
                                                      std::max(v.dragStart_.y, e.y));
}

void ArrangeView::LassoGesture::release(const juce::MouseEvent& e)
{
    if (v.draggedFar_)
    {
        v.selectInLasso(lasso_);
        return;
    }
    if (!e.mods.isShiftDown())
        v.selection_.clear();
    if (v.onLocate)
        v.onLocate(anchor_);
}

void ArrangeView::LassoGesture::paintLanes(juce::Graphics& g)
{
    if (!v.draggedFar_)
        return;
    g.setColour(v.theme_.colours.selectionFill);
    g.fillRect(lasso_);
    g.setColour(v.theme_.colours.selectionOutline);
    g.drawRect(lasso_, 1);
}

ArrangeView::MoveGesture::MoveGesture(ArrangeView& view, ArrangeClipPress press, bool copy) noexcept
    : Gesture(view), press_(std::move(press)), copy_(copy),
      wasSelected_(edit::contains(view.pressSelection_, press_.clip))
{
}

void ArrangeView::MoveGesture::drag(const juce::MouseEvent& e, core::SamplePosition at)
{
    // Ctrl can be pressed or let go mid-drag: what it is at the release counts.
    copy_ = e.mods.isCtrlDown();
    v.setMouseCursor(copy_ && v.draggedFar_ ? juce::MouseCursor::CopyingCursor
                                            : juce::MouseCursor::DraggingHandCursor);
    // The dragged clip's new start snaps; the rest of the selection follows by the
    // same delta, and rows by the same count, never before zero, outside the
    // tracks or onto a track of the other kind.
    const auto& project = *v.project_;
    const auto wanted = press_.span.start + (at - press_.at);
    delta_ = v.snapWithTargets(wanted, press_.row + rows_, true, press_.span.start) - press_.span.start;
    core::SamplePosition earliest = press_.span.start;
    int lowestRow = press_.row, highestRow = press_.row;
    for (const auto& ref : v.selection_)
        if (const auto span = v.spanOf(ref))
        {
            const int t = project.trackIndex(ref.track);
            earliest = std::min(earliest, span->start);
            lowestRow = std::min(lowestRow, t);
            highestRow = std::max(highestRow, t);
        }
    delta_ = std::max(delta_, -earliest);
    const int row = v.rowAtY(e.y);
    const int trackCount = static_cast<int>(project.tracks.size());
    int wantedRows = row >= 0 ? row - press_.row : rows_;
    wantedRows = std::clamp(wantedRows, -lowestRow, std::max(0, trackCount - 1 - highestRow));
    // Every clip must land on a track of its own kind.
    if (std::all_of(v.selection_.begin(),
                    v.selection_.end(),
                    [&](const ClipRef& ref)
                    {
                        return project.trackIndex(ref.track) < 0 ||
                               edit::landingTrack(project, ref, wantedRows) != nullptr;
                    }))
        rows_ = wantedRows;
}

void ArrangeView::MoveGesture::release(const juce::MouseEvent& e)
{
    if (v.draggedFar_)
    {
        if (v.project_ == nullptr || (delta_ == 0 && rows_ == 0))
            return;
        // The automation under the clips goes with them along their own tracks.
        auto automation = v.automationFollows_ && rows_ == 0
                              ? edit::moveAutomation(*v.project_, v.selection_, delta_, copy_)
                              : edit::Edit{};
        if (copy_)
        {
            auto copy = edit::copyClipsTo(*v.project_, v.selection_, delta_, rows_);
            if (copy.edit.empty())
                return;
            // The copies are the selection (their ids come with the edit's apply).
            std::vector<std::pair<const core::Command*, ClipRef>> made;
            for (size_t i = 0; i < copy.edit.commands.size() && i < copy.moved.size(); ++i)
                made.emplace_back(copy.edit.commands[i].get(), copy.moved[i]);
            auto commands = std::move(copy.edit.commands);
            for (auto& c : automation.commands)
                commands.push_back(std::move(c));
            v.emit(std::move(commands), copy.edit.name);
            v.selection_.clear();
            for (auto [command, ref] : made)
            {
                if (const auto* audio = dynamic_cast<const core::AddClip*>(command))
                    ref.clip = audio->clipId();
                else if (const auto* midi = dynamic_cast<const core::AddMidiClip*>(command))
                    ref.clip = midi->clipId();
                if (v.spanOf(ref).has_value())
                    v.select(ref, true);
            }
            v.notifySelection();
            return;
        }
        auto move = edit::moveClips(*v.project_, v.selection_, delta_, rows_);
        if (move.edit.empty())
            return;
        for (auto& c : automation.commands)
            move.edit.commands.push_back(std::move(c));
        v.selection_ = std::move(move.moved); // the selection follows the clips to their new tracks
        v.emit(std::move(move.edit.commands), move.edit.name);
        v.notifySelection();
    }
    else if (e.mods.isCtrlDown())
    {
        // Ctrl+click: toggle membership (the press added it if it was not in).
        if (wasSelected_)
            std::erase(v.selection_, press_.clip);
    }
    else if (!e.mods.isShiftDown())
    {
        v.selection_.clear();
        v.select(press_.clip, true);
    }
}

float ArrangeView::MoveGesture::dim(const ClipRef&, bool selected) const
{
    // A copy leaves the originals where they are, at full strength.
    return selected && v.draggedFar_ && !copy_ ? 0.4f : 1.0f;
}

void ArrangeView::MoveGesture::paintLanes(juce::Graphics& g)
{
    if (!v.draggedFar_)
        return;
    const auto& c = v.theme_.colours;
    const auto& project = *v.project_;
    // Ghosts of the selection where it would land, and the snap guide at the
    // dragged clip's new start.
    for (const auto& ref : v.selection_)
    {
        const int from = project.trackIndex(ref.track);
        const auto span = v.spanOf(ref);
        if (from < 0 || !span.has_value())
            continue;
        const int to = std::clamp(from + rows_, 0, static_cast<int>(project.tracks.size()) - 1);
        const auto bounds =
            v.spanBounds(std::max<core::SamplePosition>(0, span->start + delta_), span->length, to);
        const auto colour = v.trackColour(project.tracks[static_cast<size_t>(from)], from);
        g.setColour(colour.withAlpha(v.theme_.track.clipBodySelectedAlpha));
        g.fillRoundedRectangle(bounds.toFloat(), v.theme_.radius.sm);
        g.setColour(colour);
        g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), v.theme_.radius.sm, 1.0f);
    }
    const auto newStart = std::max<core::SamplePosition>(0, press_.span.start + delta_);
    const int gx = v.xForSample(newStart);
    g.setColour(c.snapGuide);
    g.fillRect(gx, v.rulerHeight, 1, v.lanesHeight());
    // The move in beats of the signature where the clip starts.
    const auto& map = v.tempoMap_;
    const double beatQuarters = core::TempoMap::quartersPerBeat(map.signatureAt(press_.span.start));
    const double deltaBeats = (map.quartersAt(static_cast<double>(newStart)) -
                               map.quartersAt(static_cast<double>(press_.span.start))) /
                              beatQuarters;
    v.drawTooltip(g,
                  juce::String(copy_ ? "Copy to " : "") + v.positionText(newStart) + "  " +
                      (deltaBeats >= 0 ? "+" : "") + juce::String(deltaBeats, 2) + " beats" +
                      (v.snapTargetName_.isNotEmpty() ? "  to " + v.snapTargetName_ : juce::String()),
                  v.dragPoint_);
}

ArrangeView::TrimGesture::TrimGesture(ArrangeView& view, ArrangeClipPress press, bool left) noexcept
    : Gesture(view), press_(std::move(press)), left_(left), start_(press_.span.start),
      length_(press_.span.length), sourceOffset_(press_.clip.midi ? 0 : press_.audio.sourceOffset)
{
}

void ArrangeView::TrimGesture::drag(const juce::MouseEvent&, core::SamplePosition at)
{
    const auto& span = press_.span;
    if (!left_)
    {
        const auto newEnd = std::max(v.snapWithTargets(at, press_.row), span.start + 1);
        length_ = newEnd - span.start;
        return;
    }
    const auto& audio = press_.audio;
    // Not before the source's first sample.
    const auto sourceStart = press_.clip.midi
                                 ? core::SamplePosition{0}
                                 : span.start - static_cast<core::SamplePosition>(std::llround(
                                                    static_cast<double>(audio.sourceOffset) * audio.stretch));
    const auto newStart = std::clamp(
        v.snapWithTargets(at, press_.row), std::max<core::SamplePosition>(0, sourceStart), span.end() - 1);
    start_ = newStart;
    length_ = span.end() - newStart;
    sourceOffset_ = press_.clip.midi
                        ? newStart - span.start
                        : std::max<core::SamplePosition>(0, audio.sourceAt(newStart - span.start));
}

void ArrangeView::TrimGesture::release(const juce::MouseEvent&)
{
    if (!v.draggedFar_ || v.project_ == nullptr)
        return;
    auto e = edit::trimClip(*v.project_, press_.clip, start_, length_, sourceOffset_);
    v.emit(std::move(e.commands), e.name);
}

bool ArrangeView::TrimGesture::preview(const ClipRef& ref,
                                       const core::AudioClip& model,
                                       core::AudioClip& shown) const
{
    if (!(ref == press_.clip))
        return false;
    shown = model;
    shown.start = start_;
    shown.length = length_;
    shown.sourceOffset = sourceOffset_;
    return true;
}

bool ArrangeView::TrimGesture::previewSpan(const ClipRef& ref, Span& span) const
{
    if (!(ref == press_.clip))
        return false;
    span = {start_, length_};
    return true;
}

void ArrangeView::TrimGesture::paintLanes(juce::Graphics& g)
{
    if (!v.draggedFar_)
        return;
    const auto& map = v.tempoMap_;
    const double beatQuarters = core::TempoMap::quartersPerBeat(map.signatureAt(start_));
    const double beats = (map.quartersAt(static_cast<double>(start_ + length_)) -
                          map.quartersAt(static_cast<double>(start_))) /
                         beatQuarters;
    v.drawTooltip(g, "Length " + juce::String(beats, 2) + " beats", v.dragPoint_);
}

ArrangeView::StretchGesture::StretchGesture(ArrangeView& view, ArrangeClipPress press) noexcept
    : Gesture(view), press_(std::move(press)), length_(press_.audio.length)
{
}

void ArrangeView::StretchGesture::drag(const juce::MouseEvent&, core::SamplePosition at)
{
    // Alt is held for the gesture, so the grid snaps here unless it is off.
    auto end = std::max<core::SamplePosition>(at, press_.span.start + 1);
    if (v.snap_)
        end = v.tempoMap_.snap(end, v.gridAt(end));
    // From a twentieth to twenty times the source's own length.
    const double source = static_cast<double>(press_.audio.sourceLength());
    length_ = std::clamp<core::SamplePosition>(end - press_.span.start,
                                               std::max<core::SamplePosition>(1, std::llround(source * 0.05)),
                                               std::llround(source * 20.0));
}

void ArrangeView::StretchGesture::release(const juce::MouseEvent&)
{
    if (!v.draggedFar_ || v.project_ == nullptr)
        return;
    auto e = edit::stretchClip(*v.project_, press_.clip, length_);
    v.emit(std::move(e.commands), e.name);
}

bool ArrangeView::StretchGesture::preview(const ClipRef& ref,
                                          const core::AudioClip& model,
                                          core::AudioClip& shown) const
{
    if (!(ref == press_.clip))
        return false;
    shown = model;
    shown.stretch = model.stretch * static_cast<double>(length_) /
                    static_cast<double>(std::max<core::SamplePosition>(1, model.length));
    shown.length = length_;
    return true;
}

void ArrangeView::StretchGesture::paintLanes(juce::Graphics& g)
{
    if (!v.draggedFar_)
        return;
    const auto& audio = press_.audio;
    v.drawTooltip(g,
                  "Speed " +
                      juce::String(100.0 * static_cast<double>(press_.span.length) /
                                       (audio.stretch *
                                        static_cast<double>(std::max<core::SamplePosition>(1, length_))),
                                   1) +
                      "%  " + v.positionText(press_.span.start + length_),
                  v.dragPoint_);
}

ArrangeView::FadeGesture::FadeGesture(ArrangeView& view, ArrangeClipPress press, bool fadeIn) noexcept
    : Gesture(view), press_(std::move(press)), in_(fadeIn),
      length_(fadeIn ? press_.audio.fadeIn : press_.audio.fadeOut),
      curve_(fadeIn ? press_.audio.fadeInCurve : press_.audio.fadeOutCurve), startCurve_(curve_)
{
}

float ArrangeView::FadeGesture::curveFor(float startCurve, int dy) noexcept
{
    // 80 px from linear to either end; within 4 px of where linear is, linear.
    constexpr float pixelsPerUnit = 80.0f;
    const float curve = std::clamp(startCurve - static_cast<float>(dy) / pixelsPerUnit, -1.0f, 1.0f);
    return std::abs(curve) * pixelsPerUnit < 4.0f ? 0.0f : curve;
}

juce::String ArrangeView::FadeGesture::curveText(float curve)
{
    if (curve == 0.0f)
        return "linear";
    return juce::String(curve > 0.0f ? "fast " : "slow ") +
           juce::String(juce::roundToInt(std::abs(curve) * 100.0f)) + "%";
}

void ArrangeView::FadeGesture::drag(const juce::MouseEvent& e, core::SamplePosition at)
{
    const auto& span = press_.span;
    length_ = in_ ? std::clamp<core::SamplePosition>(at - span.start, 0, span.length)
                  : std::clamp<core::SamplePosition>(span.end() - at, 0, span.length);
    curve_ = curveFor(startCurve_, e.y - v.dragStart_.y);
}

void ArrangeView::FadeGesture::release(const juce::MouseEvent&)
{
    if (!v.draggedFar_ || v.project_ == nullptr)
        return;
    auto e = edit::setFade(*v.project_, press_.clip, in_, length_, curve_);
    v.emit(std::move(e.commands), e.name);
}

bool ArrangeView::FadeGesture::preview(const ClipRef& ref,
                                       const core::AudioClip& model,
                                       core::AudioClip& shown) const
{
    if (!(ref == press_.clip))
        return false;
    shown = model;
    (in_ ? shown.fadeIn : shown.fadeOut) = length_;
    (in_ ? shown.fadeInCurve : shown.fadeOutCurve) = curve_;
    return true;
}

void ArrangeView::FadeGesture::paintLanes(juce::Graphics& g)
{
    if (v.draggedFar_)
        v.drawTooltip(g,
                      "Fade " + juce::String(static_cast<double>(length_) / v.sampleRate(), 2) + " s  " +
                          curveText(curve_),
                      v.dragPoint_);
}

ArrangeView::GainGesture::GainGesture(ArrangeView& view, ArrangeClipPress press) noexcept
    : Gesture(view), press_(std::move(press)), gain_(press_.audio.gain)
{
}

void ArrangeView::GainGesture::drag(const juce::MouseEvent& e, core::SamplePosition)
{
    // A quarter of a dB per pixel, up 24 dB at most.
    const float dbDelta = static_cast<float>(v.dragStart_.y - e.y) * 0.25f;
    const float base = static_cast<float>(db::fromGain(press_.audio.gain));
    gain_ = db::toGain(std::clamp(base + dbDelta, -60.0f, 24.0f));
}

void ArrangeView::GainGesture::release(const juce::MouseEvent&)
{
    if (!v.draggedFar_ || v.project_ == nullptr)
        return;
    auto e = edit::setClipGain(*v.project_, press_.clip, gain_);
    v.emit(std::move(e.commands), e.name);
}

bool ArrangeView::GainGesture::preview(const ClipRef& ref,
                                       const core::AudioClip& model,
                                       core::AudioClip& shown) const
{
    if (!(ref == press_.clip))
        return false;
    shown = model;
    shown.gain = gain_;
    return true;
}

void ArrangeView::GainGesture::paintLanes(juce::Graphics& g)
{
    if (v.draggedFar_)
        v.drawTooltip(g, db::gainText(gain_) + " dB", v.dragPoint_);
}

ArrangeView::SweepGesture::SweepGesture(ArrangeView& view, bool erase) noexcept : Gesture(view), erase_(erase)
{
}

bool ArrangeView::SweepGesture::swept(const ClipRef& ref) const noexcept
{
    return std::find(swept_.begin(), swept_.end(), ref) != swept_.end();
}

void ArrangeView::SweepGesture::over(const ClipRef& ref)
{
    if (v.project_ == nullptr || swept(ref))
        return;
    // The clip pressed first decides what the mute sweep does: a playing clip mutes the rest.
    if (!erase_ && !mute_)
        mute_ = !edit::isMuted(*v.project_, ref);
    swept_.push_back(ref);
    // A comp's clips erase together, as they select together.
    if (erase_)
        if (const auto* track = v.project_->findTrack(ref.track))
            if (const auto* group = track->takeGroupOwning(ref.clip);
                group != nullptr && group->midi == ref.midi)
                for (const auto id : group->clips)
                {
                    const ClipRef other{ref.track, id, ref.midi};
                    const bool exists =
                        ref.midi ? track->findMidiClip(id) != nullptr : track->findClip(id) != nullptr;
                    if (exists && !swept(other))
                        swept_.push_back(other);
                }
    v.repaint();
}

void ArrangeView::SweepGesture::drag(const juce::MouseEvent& e, core::SamplePosition)
{
    if (const auto ref = v.clipAt(v.hitTest(e.getPosition())); ref.clip != core::invalidId)
        over(ref);
}

void ArrangeView::SweepGesture::release(const juce::MouseEvent&)
{
    if (v.project_ == nullptr || swept_.empty())
        return;
    if (erase_)
    {
        v.removeClips(swept_);
        return;
    }
    auto e = edit::setMuted(*v.project_, swept_, mute_.value_or(true));
    v.emit(std::move(e.commands), e.name);
}

float ArrangeView::SweepGesture::dim(const ClipRef& ref, bool) const
{
    return erase_ && swept(ref) ? 0.4f : 1.0f;
}

std::optional<bool> ArrangeView::SweepGesture::shownMuted(const ClipRef& ref) const
{
    if (!erase_ && mute_ && swept(ref))
        return *mute_;
    return std::nullopt;
}

} // namespace northlight::ui
