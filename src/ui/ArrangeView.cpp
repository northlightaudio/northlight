#include "ui/ArrangeView.h"

#include <algorithm>
#include <cmath>

namespace aural::ui
{

namespace
{
constexpr double minSamplesPerPixel = 1.0;
constexpr double maxSamplesPerPixel = 48000.0 * 4.0; // four seconds per pixel
} // namespace

ArrangeView::ArrangeView(audio::PeakStore& peaks) : peaks_(peaks)
{
    setOpaque(true);
    scrollbar_.setAutoHide(false);
    scrollbar_.addListener(this);
    addAndMakeVisible(scrollbar_);
    verticalScrollbar_.setAutoHide(false);
    verticalScrollbar_.addListener(this);
    addAndMakeVisible(verticalScrollbar_);
    peaks_.addChangeListener(this);
}

ArrangeView::~ArrangeView()
{
    peaks_.removeChangeListener(this);
    scrollbar_.removeListener(this);
    verticalScrollbar_.removeListener(this);
}

void ArrangeView::setProject(const core::Project* project)
{
    project_ = project;
    std::erase_if(selection_, [this](const ClipRef& ref) { return findClip(ref) == nullptr; });
    rebuildHeaders();
    updateScrollbar();
    repaint();
}

void ArrangeView::setInputNames(std::vector<std::string> names)
{
    inputNames_ = std::move(names);
    rebuildHeaders();
}

void ArrangeView::rebuildHeaders()
{
    const size_t count = project_ != nullptr ? project_->tracks.size() : 0;
    while (headers_.size() > count)
        headers_.pop_back();
    while (headers_.size() < count)
    {
        auto header = std::make_unique<TrackHeader>();
        header->onInputChanged = [this](core::Id id, int channel)
        {
            if (onTrackInputChanged)
                onTrackInputChanged(id, channel);
        };
        header->onArmChanged = [this](core::Id id, bool armed)
        {
            if (onTrackArmChanged)
                onTrackArmChanged(id, armed);
        };
        header->onMonitorChanged = [this](core::Id id, core::MonitorMode mode)
        {
            if (onTrackMonitorChanged)
                onTrackMonitorChanged(id, mode);
        };
        addAndMakeVisible(*header);
        headers_.push_back(std::move(header));
    }
    for (size_t i = 0; i < count; ++i)
        headers_[i]->setTrack(project_->tracks[i], inputNames_);
    layoutHeaders();
}

void ArrangeView::setMonitoringStates(const std::vector<bool>& monitoring, bool recording)
{
    for (size_t i = 0; i < headers_.size(); ++i)
        headers_[i]->setLiveState(i < monitoring.size() && monitoring[i], recording);
}

void ArrangeView::layoutHeaders()
{
    int y = rulerHeight - verticalScroll_;
    for (auto& header : headers_)
    {
        const bool visible = y + rowHeight > rulerHeight && y < getHeight() - scrollbarHeight;
        header->setVisible(visible);
        header->setBounds(0, y, headerWidth, rowHeight);
        y += rowHeight;
    }
}

void ArrangeView::setPlayhead(core::SamplePosition position, bool playing, bool recording)
{
    if (position == playhead_ && playing == playing_ && recording == recording_)
        return;
    playhead_ = position;
    playing_ = playing;
    recording_ = recording;

    // Follow playback: page forward when the playhead leaves the view.
    if (playing_)
    {
        const auto visible = visibleSamples();
        if (playhead_ >= scroll_ + visible || playhead_ < scroll_)
            setScroll(playhead_ - visible / 10);
    }
    repaint();
}

// ----- geometry -------------------------------------------------------------------------

double ArrangeView::sampleRate() const noexcept
{
    return project_ != nullptr && project_->sampleRate > 0.0 ? project_->sampleRate : 48000.0;
}

int ArrangeView::xForSample(core::SamplePosition s) const noexcept
{
    return headerWidth + static_cast<int>(std::floor(static_cast<double>(s - scroll_) / samplesPerPixel_));
}

core::SamplePosition ArrangeView::sampleForX(int x) const noexcept
{
    return scroll_ + static_cast<core::SamplePosition>(std::max(0.0, (x - headerWidth) * samplesPerPixel_));
}

core::SamplePosition ArrangeView::visibleSamples() const noexcept
{
    return static_cast<core::SamplePosition>(std::max(1, getWidth() - headerWidth - scrollbarWidth) *
                                             samplesPerPixel_);
}

int ArrangeView::contentHeight() const noexcept
{
    return project_ != nullptr ? static_cast<int>(project_->tracks.size()) * rowHeight : 0;
}

void ArrangeView::setVerticalScroll(int firstRowPixel)
{
    const int maxScroll = std::max(0, contentHeight() - lanesHeight());
    verticalScroll_ = std::clamp(firstRowPixel, 0, maxScroll);
    updateScrollbar();
    layoutHeaders();
    repaint();
}

core::SamplePosition ArrangeView::contentLength() const noexcept
{
    const auto projectLength = project_ != nullptr ? project_->length() : 0;
    // Always leave room to scroll a screen past the end, and at least a minute.
    return std::max<core::SamplePosition>(projectLength,
                                          static_cast<core::SamplePosition>(60.0 * sampleRate())) +
           visibleSamples();
}

void ArrangeView::setZoom(double samplesPerPixel, int anchorX)
{
    const auto anchorSample = sampleForX(anchorX);
    samplesPerPixel_ = std::clamp(samplesPerPixel, minSamplesPerPixel, maxSamplesPerPixel);
    // Keep the sample under the pointer where it is.
    const auto newScroll =
        anchorSample - static_cast<core::SamplePosition>((anchorX - headerWidth) * samplesPerPixel_);
    setScroll(newScroll);
}

void ArrangeView::setScroll(core::SamplePosition firstVisible)
{
    const auto maxScroll = std::max<core::SamplePosition>(0, contentLength() - visibleSamples());
    scroll_ = std::clamp<core::SamplePosition>(firstVisible, 0, maxScroll);
    updateScrollbar();
    repaint();
}

void ArrangeView::updateScrollbar()
{
    const juce::ScopedValueSetter<bool> guard(updatingScrollbar_, true);
    scrollbar_.setRangeLimits(0.0, static_cast<double>(contentLength()));
    scrollbar_.setCurrentRange(static_cast<double>(scroll_), static_cast<double>(visibleSamples()));
    verticalScrollbar_.setRangeLimits(0.0, static_cast<double>(std::max(contentHeight(), lanesHeight())));
    verticalScrollbar_.setCurrentRange(static_cast<double>(verticalScroll_),
                                       static_cast<double>(lanesHeight()));
}

void ArrangeView::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (updatingScrollbar_)
        return;
    if (bar == &verticalScrollbar_)
    {
        verticalScroll_ = static_cast<int>(newRangeStart);
        layoutHeaders();
    }
    else
    {
        scroll_ = static_cast<core::SamplePosition>(newRangeStart);
    }
    repaint();
}

void ArrangeView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    repaint(); // peaks arrived
}

void ArrangeView::resized()
{
    scrollbar_.setBounds(headerWidth,
                         getHeight() - scrollbarHeight,
                         getWidth() - headerWidth - scrollbarWidth,
                         scrollbarHeight);
    verticalScrollbar_.setBounds(getWidth() - scrollbarWidth, rulerHeight, scrollbarWidth, lanesHeight());
    setVerticalScroll(verticalScroll_);
}

// ----- input -------------------------------------------------------------------------------

void ArrangeView::setRegion(core::SamplePosition start, core::SamplePosition end, bool loop, bool punch)
{
    regionStart_ = start;
    regionEnd_ = end;
    loop_ = loop;
    punch_ = punch;
    repaint();
}

void ArrangeView::mouseDown(const juce::MouseEvent& e)
{
    draggedFar_ = false;
    if (e.x < headerWidth || e.x >= getWidth() - scrollbarWidth || e.y >= getHeight() - scrollbarHeight)
        return;
    if (e.y < rulerHeight)
    {
        // Drag in the ruler to set the loop/punch region; a plain click locates.
        drag_ = Drag::region;
        dragAnchor_ = sampleForX(e.x);
        return;
    }

    dragStart_ = e.getPosition();
    dragStartSample_ = sampleForX(e.x);
    const auto hit = hitTest(e.getPosition());
    dragStartRow_ = hit.row;
    if (hit.clip == nullptr)
    {
        // Empty lane: a drag lassos, a click locates (and clears the selection).
        drag_ = Drag::lasso;
        lasso_ = juce::Rectangle<int>(e.x, e.y, 0, 0);
        dragAnchor_ = dragStartSample_;
        return;
    }

    const ClipRef ref{hit.track->id, hit.clip->id};
    if (e.mods.isCtrlDown() && hit.zone == Drag::move)
    {
        // Ctrl+drag on the body: gain. (Ctrl+click alone toggles the selection.)
        drag_ = Drag::gain;
    }
    else
    {
        drag_ = hit.zone;
    }
    if (e.mods.isShiftDown())
        select(ref, true);
    else if (e.mods.isCtrlDown() && drag_ != Drag::gain)
        select(ref, true); // toggle happens on mouse up if nothing was dragged
    else if (!isSelected(ref.track, ref.clip))
    {
        selection_.clear();
        select(ref, true);
    }
    dragClip_ = ref;
    dragOriginal_ = *hit.clip;
    moveDelta_ = 0;
    rowDelta_ = 0;
    trimStart_ = hit.clip->start;
    trimLength_ = hit.clip->length;
    trimOffset_ = hit.clip->sourceOffset;
    fadePreview_ = drag_ == Drag::fadeOut ? hit.clip->fadeOut : hit.clip->fadeIn;
    gainPreview_ = hit.clip->gain;
    repaint();
}

void ArrangeView::mouseDrag(const juce::MouseEvent& e)
{
    if (drag_ == Drag::none)
        return;
    draggedFar_ = draggedFar_ || e.mouseWasDraggedSinceMouseDown();
    const int x = std::clamp(e.x, headerWidth, getWidth() - scrollbarWidth);
    const auto at = sampleForX(x);
    switch (drag_)
    {
        case Drag::region:
            regionStart_ = std::min(dragAnchor_, at);
            regionEnd_ = std::max(dragAnchor_, at);
            break;
        case Drag::lasso:
            lasso_ = juce::Rectangle<int>::leftTopRightBottom(std::min(dragStart_.x, x),
                                                              std::min(dragStart_.y, e.y),
                                                              std::max(dragStart_.x, x),
                                                              std::max(dragStart_.y, e.y));
            break;
        case Drag::move:
        {
            // The dragged clip's new start snaps; the rest of the selection follows by
            // the same delta. Nothing may land before zero or outside the tracks.
            const auto wanted = dragOriginal_.start + (at - dragStartSample_);
            moveDelta_ = snapped(wanted) - dragOriginal_.start;
            core::SamplePosition earliest = dragOriginal_.start;
            int lowestRow = dragStartRow_, highestRow = dragStartRow_;
            if (project_ != nullptr)
                for (const auto& ref : selection_)
                    for (size_t t = 0; t < project_->tracks.size(); ++t)
                        if (project_->tracks[t].id == ref.track)
                            if (const auto* c = project_->tracks[t].findClip(ref.clip))
                            {
                                earliest = std::min(earliest, c->start);
                                lowestRow = std::min(lowestRow, static_cast<int>(t));
                                highestRow = std::max(highestRow, static_cast<int>(t));
                            }
            moveDelta_ = std::max(moveDelta_, -earliest);
            const int row = rowAtY(e.y);
            const int trackCount = project_ != nullptr ? static_cast<int>(project_->tracks.size()) : 0;
            rowDelta_ = row >= 0 ? row - dragStartRow_ : rowDelta_;
            rowDelta_ = std::clamp(rowDelta_, -lowestRow, std::max(0, trackCount - 1 - highestRow));
            break;
        }
        case Drag::trimLeft:
        {
            const auto sourceStart =
                dragOriginal_.start - dragOriginal_.sourceOffset; // where source sample 0 sits
            const auto newStart = std::clamp(
                snapped(at), std::max<core::SamplePosition>(0, sourceStart), dragOriginal_.end() - 1);
            trimStart_ = newStart;
            trimLength_ = dragOriginal_.end() - newStart;
            trimOffset_ = dragOriginal_.sourceOffset + (newStart - dragOriginal_.start);
            break;
        }
        case Drag::trimRight:
        {
            const auto newEnd = std::max(snapped(at), dragOriginal_.start + 1);
            trimLength_ = newEnd - dragOriginal_.start;
            break;
        }
        case Drag::fadeIn:
            fadePreview_ =
                std::clamp<core::SamplePosition>(at - dragOriginal_.start, 0, dragOriginal_.length);
            break;
        case Drag::fadeOut:
            fadePreview_ =
                std::clamp<core::SamplePosition>(dragOriginal_.end() - at, 0, dragOriginal_.length);
            break;
        case Drag::gain:
        {
            // A quarter of a dB per pixel, up 24 dB at most.
            const float db = static_cast<float>(dragStart_.y - e.y) * 0.25f;
            const float base = 20.0f * std::log10(std::max(dragOriginal_.gain, 1.0e-4f));
            gainPreview_ = std::pow(10.0f, std::clamp(base + db, -60.0f, 24.0f) / 20.0f);
            break;
        }
        case Drag::none:
            break;
    }
    repaint();
}

void ArrangeView::mouseUp(const juce::MouseEvent& e)
{
    const auto drag = drag_;
    drag_ = Drag::none;
    switch (drag)
    {
        case Drag::region:
            if (e.mouseWasDraggedSinceMouseDown())
            {
                if (onRegionChanged)
                    onRegionChanged(regionStart_, regionEnd_);
            }
            else if (onLocate)
            {
                onLocate(dragAnchor_);
            }
            break;
        case Drag::lasso:
            if (draggedFar_)
            {
                finishLasso();
            }
            else
            {
                if (!e.mods.isShiftDown())
                    selection_.clear();
                if (onLocate)
                    onLocate(dragAnchor_);
            }
            break;
        case Drag::move:
            if (draggedFar_)
                finishMove();
            else if (e.mods.isCtrlDown())
            {
                // Ctrl+click: toggle membership.
                std::erase(selection_, dragClip_);
            }
            else if (!e.mods.isShiftDown())
            {
                selection_.clear();
                select(dragClip_, true);
            }
            break;
        case Drag::trimLeft:
        case Drag::trimRight:
            if (draggedFar_)
                finishTrim();
            break;
        case Drag::fadeIn:
        case Drag::fadeOut:
            if (draggedFar_)
                finishFade();
            break;
        case Drag::gain:
            if (draggedFar_)
                finishGain();
            else
                std::erase(selection_, dragClip_); // Ctrl+click without a drag toggles
            break;
        case Drag::none:
            break;
    }
    repaint();
}

void ArrangeView::mouseMove(const juce::MouseEvent& e)
{
    if (drag_ != Drag::none)
        return;
    const auto hit = hitTest(e.getPosition());
    setMouseCursor(cursorFor(hit.clip != nullptr ? hit.zone : Drag::none));
}

void ArrangeView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const double factor = std::pow(1.25, -wheel.deltaY * 4.0);
        setZoom(samplesPerPixel_ * factor, std::max(headerWidth, e.x));
        return;
    }
    // Plain wheel scrolls tracks, Shift+wheel (or a horizontal wheel) scrolls time.
    const bool horizontal = e.mods.isShiftDown() || wheel.deltaX != 0.0f;
    const float delta = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
    if (delta == 0.0f)
        return;
    if (horizontal)
        setScroll(scroll_ -
                  static_cast<core::SamplePosition>(delta * static_cast<float>(visibleSamples()) * 0.5f));
    else
        setVerticalScroll(verticalScroll_ - static_cast<int>(delta * static_cast<float>(rowHeight) * 3.0f));
}

// ----- painting ------------------------------------------------------------------------------

void ArrangeView::drawRuler(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& c = theme_.colours;
    g.setColour(c.surface);
    g.fillRect(area);
    g.setColour(c.border);
    g.fillRect(area.withTrimmedTop(area.getHeight() - 1));
    g.setFont(juce::FontOptions{theme_.type.caption});

    const double rate = sampleRate();
    const auto barsRow = area.removeFromTop(area.getHeight() / 2);
    const auto timeRow = area;

    // Bars and beats: label every 1, 2, 4, 8... bars so labels are ~80 px apart;
    // beat ticks appear once a beat is at least 12 px wide.
    if (project_ != nullptr)
    {
        const double spb = core::samplesPerBeat(rate, project_->tempoBpm, project_->timeSignature);
        const int beatsPerBar = std::max(1, project_->timeSignature.numerator);
        const double barPixels = spb * beatsPerBar / samplesPerPixel_;
        int barStep = 1;
        while (barPixels * barStep < 80.0)
            barStep *= 2;
        const bool showBeats = spb / samplesPerPixel_ >= 12.0;

        const auto firstBar = static_cast<std::int64_t>(static_cast<double>(scroll_) / (spb * beatsPerBar));
        for (std::int64_t bar = firstBar;; ++bar)
        {
            const auto barStart =
                static_cast<core::SamplePosition>(std::llround(static_cast<double>(bar) * spb * beatsPerBar));
            const int x = xForSample(barStart);
            if (x > getWidth())
                break;
            const bool labelled = bar % barStep == 0;
            if (x >= headerWidth)
            {
                g.setColour(labelled ? c.textSecondary : c.borderStrong);
                g.fillRect(x, barsRow.getBottom() - (labelled ? 10 : 5), 1, labelled ? 10 : 5);
                if (labelled)
                {
                    g.setColour(c.textPrimary);
                    g.drawText(juce::String(bar + 1),
                               x + 3,
                               barsRow.getY() + 1,
                               60,
                               barsRow.getHeight() - 2,
                               juce::Justification::centredLeft);
                }
            }
            if (showBeats)
                for (int beat = 1; beat < beatsPerBar; ++beat)
                {
                    const int bx =
                        xForSample(barStart + static_cast<core::SamplePosition>(std::llround(beat * spb)));
                    if (bx >= headerWidth && bx <= getWidth())
                    {
                        g.setColour(c.borderStrong);
                        g.fillRect(bx, barsRow.getBottom() - 4, 1, 4);
                    }
                }
        }
    }

    // Time: tick every 0.1 .. 300 s depending on zoom, aiming for ~90 px.
    const double secondsPerPixel = samplesPerPixel_ / rate;
    double step = 1.0;
    for (double candidate : {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0})
    {
        step = candidate;
        if (candidate / secondsPerPixel >= 90.0)
            break;
    }
    const double firstSecond = std::floor(static_cast<double>(scroll_) / rate / step) * step;
    for (double t = firstSecond;; t += step)
    {
        const int x = xForSample(static_cast<core::SamplePosition>(t * rate));
        if (x > getWidth())
            break;
        if (x < headerWidth)
            continue;
        g.setColour(c.borderStrong);
        g.fillRect(x, timeRow.getBottom() - 8, 1, 8);
        g.setColour(c.textSecondary);
        const juce::String label = step < 1.0  ? juce::String(t, 2) + " s"
                                   : t >= 60.0 ? juce::String(static_cast<int>(t) / 60) + ":" +
                                                     juce::String(static_cast<int>(t) % 60).paddedLeft('0', 2)
                                               : juce::String(static_cast<int>(t)) + " s";
        g.drawText(
            label, x + 3, timeRow.getY() + 1, 70, timeRow.getHeight() - 8, juce::Justification::centredLeft);
    }
}

// ----- drag and drop ------------------------------------------------------------------------

void ArrangeView::setAcceptedExtensions(const juce::String& wildcards)
{
    acceptedExtensions_.clear();
    for (const auto& pattern : juce::StringArray::fromTokens(wildcards, ";", ""))
        acceptedExtensions_.add(pattern.trim().fromLastOccurrenceOf("*", false, false).toLowerCase());
}

bool ArrangeView::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto extension = juce::File(f).getFileExtension().toLowerCase();
        if (acceptedExtensions_.isEmpty() || acceptedExtensions_.contains(extension))
            return true;
    }
    return false;
}

void ArrangeView::fileDragEnter(const juce::StringArray&, int, int)
{
    dragOver_ = true;
    repaint();
}

void ArrangeView::fileDragExit(const juce::StringArray&)
{
    dragOver_ = false;
    repaint();
}

void ArrangeView::filesDropped(const juce::StringArray& files, int x, int)
{
    dragOver_ = false;
    repaint();
    if (!onImportFiles)
        return;
    std::vector<juce::File> accepted;
    for (const auto& f : files)
    {
        const juce::File file(f);
        if (acceptedExtensions_.isEmpty() ||
            acceptedExtensions_.contains(file.getFileExtension().toLowerCase()))
            accepted.push_back(file);
    }
    if (!accepted.empty())
        onImportFiles(accepted, sampleForX(std::max(headerWidth, x)));
}

void ArrangeView::drawClip(juce::Graphics& g,
                           const core::Track& track,
                           const core::AudioClip& model,
                           juce::Rectangle<int> lane)
{
    const auto& c = theme_.colours;
    // What is drawn: the model clip, or its preview while it is being edited.
    core::AudioClip clip = model;
    const bool editing = drag_ != Drag::none && dragClip_.track == track.id && dragClip_.clip == model.id;
    if (editing)
    {
        if (drag_ == Drag::trimLeft || drag_ == Drag::trimRight)
        {
            clip.start = trimStart_;
            clip.length = trimLength_;
            clip.sourceOffset = trimOffset_;
        }
        else if (drag_ == Drag::fadeIn)
            clip.fadeIn = fadePreview_;
        else if (drag_ == Drag::fadeOut)
            clip.fadeOut = fadePreview_;
        else if (drag_ == Drag::gain)
            clip.gain = gainPreview_;
    }
    const bool selected = isSelected(track.id, model.id);
    const bool moving = drag_ == Drag::move && selected && draggedFar_;

    const int x0 = xForSample(clip.start);
    const int x1 = std::max(x0 + 2, xForSample(clip.end()));
    if (x1 < headerWidth || x0 > getWidth())
        return;

    const auto block = juce::Rectangle<int>(x0, lane.getY() + 4, x1 - x0, lane.getHeight() - 8);
    const auto visible =
        block.getIntersection(lane.withLeft(headerWidth).withRight(getWidth() - scrollbarWidth));
    if (visible.isEmpty())
        return;

    const float dim = moving ? 0.4f : 1.0f;
    g.setColour(c.accent.withAlpha((track.mute ? 0.15f : selected ? 0.45f : 0.3f) * dim));
    g.fillRoundedRectangle(block.toFloat(), theme_.radius.sm);

    // Waveform: one min/max column per pixel from the peak cache, scaled by the
    // clip gain so a gain change is visible.
    if (const auto peaks = peaks_.peaksFor(clip.mediaPath, sampleRate()); peaks != nullptr && !peaks->empty())
    {
        const int channels = std::min(2, peaks->numChannels());
        const float laneHeight = static_cast<float>(visible.getHeight()) / static_cast<float>(channels);
        g.setColour((track.mute ? c.textDisabled : c.accentHover).withAlpha(dim));
        const float scale = std::min(1.0f, clip.gain);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float midY =
                static_cast<float>(visible.getY()) + laneHeight * (static_cast<float>(ch) + 0.5f);
            const float half = laneHeight * 0.48f * scale;
            for (int x = visible.getX(); x < visible.getRight(); ++x)
            {
                const auto from = clip.sourceOffset + (sampleForX(x) - clip.start);
                const auto to = clip.sourceOffset + (sampleForX(x + 1) - clip.start);
                const auto r = peaks->lookup(ch, from, std::max(to, from + 1));
                const float top = midY - r.maximum * half;
                const float bottom = midY - r.minimum * half;
                g.fillRect(static_cast<float>(x),
                           std::min(top, bottom),
                           1.0f,
                           std::max(1.0f, std::abs(bottom - top)));
            }
        }
    }

    // Fades: the shaded wedge above the ramp, and the handles at the top corners.
    const auto fadeWedge = [&](core::SamplePosition fade, bool in)
    {
        if (fade <= 0)
            return;
        const float w = static_cast<float>(fade) / static_cast<float>(samplesPerPixel_);
        const float top = static_cast<float>(block.getY());
        const float bottom = static_cast<float>(block.getBottom());
        juce::Path wedge;
        if (in)
        {
            wedge.startNewSubPath(static_cast<float>(x0), top);
            wedge.lineTo(static_cast<float>(x0) + w, top);
            wedge.lineTo(static_cast<float>(x0), bottom);
        }
        else
        {
            wedge.startNewSubPath(static_cast<float>(x1), top);
            wedge.lineTo(static_cast<float>(x1) - w, top);
            wedge.lineTo(static_cast<float>(x1), bottom);
        }
        wedge.closeSubPath();
        g.setColour(c.background.withAlpha(0.45f));
        g.fillPath(wedge);
        g.setColour(c.textSecondary.withAlpha(0.8f));
        g.drawLine(in ? static_cast<float>(x0) : static_cast<float>(x1),
                   bottom,
                   in ? static_cast<float>(x0) + w : static_cast<float>(x1) - w,
                   top,
                   1.0f);
    };
    fadeWedge(clip.fadeIn, true);
    fadeWedge(clip.fadeOut, false);
    if (selected && block.getWidth() > 2 * fadeHandle + 4)
    {
        g.setColour(c.textPrimary.withAlpha(0.9f));
        g.fillRect(x0 + 2, block.getY() + 2, fadeHandle - 4, fadeHandle - 4);
        g.fillRect(x1 - fadeHandle + 2, block.getY() + 2, fadeHandle - 4, fadeHandle - 4);
    }

    g.setColour(selected ? c.textPrimary : c.accent);
    g.drawRoundedRectangle(block.toFloat().reduced(0.5f), theme_.radius.sm, selected ? 2.0f : 1.0f);
    g.setColour(c.textPrimary);
    g.setFont(juce::FontOptions{theme_.type.caption});
    juce::String label = juce::String::fromUTF8(clip.name.c_str());
    if (std::abs(clip.gain - 1.0f) > 1.0e-3f)
        label << "  " << juce::String(20.0 * std::log10(std::max(clip.gain, 1.0e-4f)), 1) << " dB";
    g.drawText(label, visible.reduced(fadeHandle + 2, 2), juce::Justification::topLeft, true);
}

void ArrangeView::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.background);

    drawRuler(g, getLocalBounds().removeFromTop(rulerHeight));

    int y = rulerHeight - verticalScroll_;
    if (project_ != nullptr)
    {
        g.saveState();
        g.reduceClipRegion(0, rulerHeight, getWidth() - scrollbarWidth, lanesHeight());
        for (const auto& track : project_->tracks)
        {
            if (y + rowHeight < rulerHeight || y > getHeight())
            {
                y += rowHeight;
                continue;
            }
            const juce::Rectangle<int> row(0, y, getWidth() - scrollbarWidth, rowHeight);
            g.setColour(c.border);
            g.fillRect(row.withTrimmedTop(rowHeight - 1));

            for (const auto& clip : track.clips)
                drawClip(g, track, clip, row);

            // The TrackHeader component covers the lane's left edge, so clips scrolled
            // left disappear under it. Armed tracks get a tinted lane.
            if (track.armed)
            {
                g.setColour(c.danger.withAlpha(recording_ ? 0.12f : 0.06f));
                g.fillRect(row.withLeft(headerWidth));
            }
            y += rowHeight;
        }
        g.restoreState();
    }

    if (project_ == nullptr || project_->tracks.empty())
    {
        g.setColour(c.textDisabled);
        g.setFont(juce::FontOptions{theme_.type.title});
        g.drawText("Import an audio file to create a track",
                   getLocalBounds().withTrimmedTop(rulerHeight),
                   juce::Justification::centred);
    }

    drawEditOverlay(g);

    // Loop/punch region over the ruler and the lanes.
    if (regionEnd_ > regionStart_)
    {
        const int x0 = std::max(headerWidth, xForSample(regionStart_));
        const int x1 = std::min(getWidth() - scrollbarWidth, xForSample(regionEnd_));
        if (x1 > x0)
        {
            const auto colour = punch_ ? c.danger : loop_ ? c.accent : c.textSecondary;
            g.setColour(colour.withAlpha(loop_ || punch_ ? 0.35f : 0.2f));
            g.fillRect(x0, 0, x1 - x0, rulerHeight - 1);
            g.setColour(colour.withAlpha(0.08f));
            g.fillRect(x0, rulerHeight, x1 - x0, lanesHeight());
            g.setColour(colour);
            g.fillRect(x0, 0, 1, getHeight() - scrollbarHeight);
            g.fillRect(x1 - 1, 0, 1, getHeight() - scrollbarHeight);
        }
    }

    const int px = xForSample(playhead_);
    if (px >= headerWidth)
    {
        g.setColour(recording_ ? c.danger : playing_ ? c.success : c.textPrimary);
        g.fillRect(px, 0, 2, getHeight() - scrollbarHeight);
    }

    if (dragOver_)
    {
        g.setColour(c.accent);
        g.drawRect(getLocalBounds(), 2);
    }
}

// ----- editing -------------------------------------------------------------------------------

int ArrangeView::rowAtY(int y) const noexcept
{
    if (project_ == nullptr || y < rulerHeight || y >= getHeight() - scrollbarHeight)
        return -1;
    const int row = (y - rulerHeight + verticalScroll_) / rowHeight;
    return row >= 0 && static_cast<size_t>(row) < project_->tracks.size() ? row : -1;
}

juce::Rectangle<int> ArrangeView::rowBounds(int row) const noexcept
{
    return {0, rulerHeight - verticalScroll_ + row * rowHeight, getWidth() - scrollbarWidth, rowHeight};
}

juce::Rectangle<int> ArrangeView::clipBounds(const core::AudioClip& clip, int row) const noexcept
{
    const auto lane = rowBounds(row);
    const int x0 = xForSample(clip.start);
    const int x1 = std::max(x0 + 2, xForSample(clip.end()));
    return {x0, lane.getY() + 4, x1 - x0, lane.getHeight() - 8};
}

ArrangeView::Hit ArrangeView::hitTest(juce::Point<int> p) const noexcept
{
    Hit hit;
    if (p.x < headerWidth || p.x >= getWidth() - scrollbarWidth)
        return hit;
    hit.row = rowAtY(p.y);
    if (hit.row < 0)
        return hit;
    hit.track = &project_->tracks[static_cast<size_t>(hit.row)];
    // Later clips draw on top, so they win the hit.
    for (auto it = hit.track->clips.rbegin(); it != hit.track->clips.rend(); ++it)
    {
        const auto bounds = clipBounds(*it, hit.row);
        if (!bounds.contains(p))
            continue;
        hit.clip = &*it;
        const bool topRow = p.y < bounds.getY() + fadeHandle;
        if (topRow && p.x < bounds.getX() + fadeHandle && bounds.getWidth() > 2 * fadeHandle + 4)
            hit.zone = Drag::fadeIn;
        else if (topRow && p.x >= bounds.getRight() - fadeHandle && bounds.getWidth() > 2 * fadeHandle + 4)
            hit.zone = Drag::fadeOut;
        else if (p.x < bounds.getX() + edgeZone)
            hit.zone = Drag::trimLeft;
        else if (p.x >= bounds.getRight() - edgeZone)
            hit.zone = Drag::trimRight;
        else
            hit.zone = Drag::move;
        return hit;
    }
    return hit;
}

bool ArrangeView::isSelected(core::Id track, core::Id clip) const noexcept
{
    return std::find(selection_.begin(), selection_.end(), ClipRef{track, clip}) != selection_.end();
}

void ArrangeView::select(const ClipRef& ref, bool add)
{
    if (!add)
        selection_.clear();
    if (!isSelected(ref.track, ref.clip))
        selection_.push_back(ref);
}

void ArrangeView::selectAll()
{
    selection_.clear();
    if (project_ != nullptr)
        for (const auto& t : project_->tracks)
            for (const auto& c : t.clips)
                selection_.push_back({t.id, c.id});
    repaint();
}

void ArrangeView::clearSelection()
{
    selection_.clear();
    repaint();
}

void ArrangeView::setSnapEnabled(bool on)
{
    snap_ = on;
    repaint();
}

void ArrangeView::setRippleEnabled(bool on)
{
    ripple_ = on;
    repaint();
}

core::SamplePosition ArrangeView::grid() const noexcept
{
    if (project_ == nullptr)
        return 1;
    return core::gridSamples(
        sampleRate(), project_->tempoBpm, project_->timeSignature, samplesPerPixel_, gridMinPixels);
}

core::SamplePosition ArrangeView::snapped(core::SamplePosition s) const noexcept
{
    return snap_ ? core::snapToGrid(std::max<core::SamplePosition>(0, s), grid())
                 : std::max<core::SamplePosition>(0, s);
}

const core::AudioClip* ArrangeView::findClip(const ClipRef& ref, const core::Track** track) const noexcept
{
    if (project_ == nullptr)
        return nullptr;
    const auto* t = project_->findTrack(ref.track);
    if (t == nullptr)
        return nullptr;
    if (track != nullptr)
        *track = t;
    return t->findClip(ref.clip);
}

void ArrangeView::emit(std::vector<std::unique_ptr<core::Command>> commands, const std::string& name)
{
    if (!commands.empty() && onEdit)
        onEdit(std::move(commands), name);
}

void ArrangeView::finishLasso()
{
    if (project_ == nullptr)
        return;
    const auto from = sampleForX(lasso_.getX());
    const auto to = sampleForX(lasso_.getRight());
    for (size_t t = 0; t < project_->tracks.size(); ++t)
    {
        const auto lane = rowBounds(static_cast<int>(t));
        if (lane.getBottom() <= lasso_.getY() || lane.getY() >= lasso_.getBottom())
            continue;
        for (const auto& c : project_->tracks[t].clips)
            if (c.end() > from && c.start < to)
                select({project_->tracks[t].id, c.id}, true);
    }
}

void ArrangeView::finishMove()
{
    if (project_ == nullptr || (moveDelta_ == 0 && rowDelta_ == 0))
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    std::vector<ClipRef> moved;
    for (const auto& ref : selection_)
    {
        const int from = project_->trackIndex(ref.track);
        const auto* clip = findClip(ref);
        if (from < 0 || clip == nullptr)
            continue;
        const int to = std::clamp(from + rowDelta_, 0, static_cast<int>(project_->tracks.size()) - 1);
        const auto& target = project_->tracks[static_cast<size_t>(to)];
        commands.push_back(std::make_unique<core::MoveClip>(
            ref.track, ref.clip, target.id, std::max<core::SamplePosition>(0, clip->start + moveDelta_)));
        moved.push_back({target.id, ref.clip});
    }
    selection_ = moved; // the selection follows the clips to their new tracks
    emit(std::move(commands), commands.size() > 1 ? "Move clips" : "Move clip");
}

void ArrangeView::finishTrim()
{
    if (findClip(dragClip_) == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::make_unique<core::TrimClip>(
        dragClip_.track, dragClip_.clip, trimStart_, trimLength_, trimOffset_));
    emit(std::move(commands), "Trim clip");
}

void ArrangeView::finishFade()
{
    const auto* clip = findClip(dragClip_);
    if (clip == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(
        std::make_unique<core::SetClipFades>(dragClip_.track,
                                             dragClip_.clip,
                                             drag_ == Drag::fadeIn ? fadePreview_ : clip->fadeIn,
                                             drag_ == Drag::fadeOut ? fadePreview_ : clip->fadeOut));
    emit(std::move(commands), "Set fade");
}

void ArrangeView::finishGain()
{
    if (findClip(dragClip_) == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::make_unique<core::SetClipGain>(dragClip_.track, dragClip_.clip, gainPreview_));
    emit(std::move(commands), "Set clip gain");
}

void ArrangeView::deleteSelection()
{
    if (project_ == nullptr || selection_.empty())
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    for (const auto& t : project_->tracks)
    {
        // Later clips first, so with ripple each gap closes over the clips that
        // were already shifted.
        std::vector<const core::AudioClip*> doomed;
        for (const auto& c : t.clips)
            if (isSelected(t.id, c.id))
                doomed.push_back(&c);
        std::sort(
            doomed.begin(), doomed.end(), [](const auto* a, const auto* b) { return a->start > b->start; });
        for (const auto* c : doomed)
        {
            commands.push_back(std::make_unique<core::RemoveClip>(t.id, c->id));
            if (ripple_)
                commands.push_back(std::make_unique<core::ShiftClips>(t.id, c->end(), -c->length));
        }
    }
    selection_.clear();
    emit(std::move(commands), ripple_ ? "Ripple delete" : "Delete clips");
}

void ArrangeView::duplicateSelection()
{
    std::vector<std::unique_ptr<core::Command>> commands;
    for (const auto& ref : selection_)
        if (findClip(ref) != nullptr)
            commands.push_back(std::make_unique<core::DuplicateClip>(ref.track, ref.clip));
    emit(std::move(commands), "Duplicate clips");
}

void ArrangeView::splitAtPlayhead()
{
    if (project_ == nullptr)
        return;
    const auto at = playheadForEdits ? playheadForEdits() : playhead_;
    std::vector<std::unique_ptr<core::Command>> commands;
    // Selected clips under the playhead; with nothing selected, every clip there.
    for (const auto& t : project_->tracks)
        for (const auto& c : t.clips)
            if (at > c.start && at < c.end() && (selection_.empty() || isSelected(t.id, c.id)))
                commands.push_back(std::make_unique<core::SplitClip>(t.id, c.id, at));
    emit(std::move(commands), "Split clips");
}

void ArrangeView::joinSelection()
{
    if (project_ == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    for (const auto& t : project_->tracks)
    {
        const core::AudioClip* left = nullptr;
        core::AudioClip merged;       // what `left` will look like after the joins so far
        for (const auto& c : t.clips) // sorted by start
        {
            if (!isSelected(t.id, c.id))
                continue;
            if (left != nullptr && core::JoinClips::canJoin(merged, c))
            {
                commands.push_back(std::make_unique<core::JoinClips>(t.id, left->id, c.id));
                merged.length += c.length;
                merged.fadeOut = c.fadeOut;
                continue;
            }
            left = &c;
            merged = c;
        }
    }
    emit(std::move(commands), "Join clips");
}

void ArrangeView::crossfadeSelection()
{
    if (project_ == nullptr)
        return;
    std::vector<std::unique_ptr<core::Command>> commands;
    for (const auto& t : project_->tracks)
    {
        const core::AudioClip* previous = nullptr;
        for (const auto& c : t.clips)
        {
            if (!isSelected(t.id, c.id))
                continue;
            if (previous != nullptr && c.start > previous->start && c.start < previous->end())
                commands.push_back(std::make_unique<core::CrossfadeClips>(t.id, previous->id, c.id));
            previous = &c;
        }
    }
    emit(std::move(commands), "Crossfade clips");
}

void ArrangeView::nudgeSelection(int direction)
{
    if (project_ == nullptr)
        return;
    const auto step = grid() * direction;
    std::vector<std::unique_ptr<core::Command>> commands;
    for (const auto& ref : selection_)
        if (const auto* c = findClip(ref))
            commands.push_back(std::make_unique<core::MoveClip>(
                ref.track, ref.clip, ref.track, std::max<core::SamplePosition>(0, c->start + step)));
    emit(std::move(commands), "Nudge clips");
}

void ArrangeView::normaliseSelection()
{
    if (!onNormalise)
        return;
    for (const auto ref : std::vector<ClipRef>(selection_))
        onNormalise(ref.track, ref.clip);
}

bool ArrangeView::handleEditKey(const juce::KeyPress& key)
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
    if (ctrl('d'))
    {
        duplicateSelection();
        return true;
    }
    if (ctrl('a'))
    {
        selectAll();
        return true;
    }
    if (key ==
        juce::KeyPress('n', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        normaliseSelection();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        clearSelection();
        return true;
    }
    if (plain('s'))
    {
        splitAtPlayhead();
        return true;
    }
    if (plain('j'))
    {
        joinSelection();
        return true;
    }
    if (plain('x'))
    {
        crossfadeSelection();
        return true;
    }
    if (plain('g'))
    {
        setSnapEnabled(!snap_);
        return true;
    }
    if (plain('e'))
    {
        setRippleEnabled(!ripple_);
        return true;
    }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        if (!selection_.empty())
        {
            nudgeSelection(key == juce::KeyPress::leftKey ? -1 : 1);
            return true;
        }
    }
    return false;
}

juce::MouseCursor ArrangeView::cursorFor(Drag zone) const noexcept
{
    switch (zone)
    {
        case Drag::trimLeft:
        case Drag::trimRight:
            return juce::MouseCursor::LeftRightResizeCursor;
        case Drag::fadeIn:
        case Drag::fadeOut:
            return juce::MouseCursor::PointingHandCursor;
        case Drag::move:
            return juce::MouseCursor::DraggingHandCursor;
        default:
            return juce::MouseCursor::NormalCursor;
    }
}

void ArrangeView::drawEditOverlay(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    // Snap and ripple state in the ruler's corner over the headers.
    g.setFont(juce::FontOptions{theme_.type.caption});
    g.setColour(snap_ ? c.textPrimary : c.textDisabled);
    g.drawText(snap_ ? "Snap" : "Snap off",
               8,
               2,
               headerWidth / 2 - 8,
               rulerHeight / 2 - 2,
               juce::Justification::centredLeft);
    g.setColour(ripple_ ? c.warning : c.textDisabled);
    g.drawText(ripple_ ? "Ripple" : "Ripple off",
               headerWidth / 2,
               2,
               headerWidth / 2 - 8,
               rulerHeight / 2 - 2,
               juce::Justification::centredLeft);
    if (!selection_.empty())
    {
        g.setColour(c.textSecondary);
        g.drawText(juce::String(selection_.size()) + " selected",
                   8,
                   rulerHeight / 2,
                   headerWidth - 16,
                   rulerHeight / 2 - 2,
                   juce::Justification::centredLeft);
    }

    if (project_ == nullptr)
        return;
    g.saveState();
    g.reduceClipRegion(headerWidth, rulerHeight, getWidth() - headerWidth - scrollbarWidth, lanesHeight());
    if (drag_ == Drag::lasso && draggedFar_)
    {
        g.setColour(c.accent.withAlpha(0.15f));
        g.fillRect(lasso_);
        g.setColour(c.accent);
        g.drawRect(lasso_, 1);
    }
    if (drag_ == Drag::move && draggedFar_)
    {
        // Ghosts of the selection where it would land.
        for (const auto& ref : selection_)
        {
            const int from = project_->trackIndex(ref.track);
            const auto* clip = findClip(ref);
            if (from < 0 || clip == nullptr)
                continue;
            const int to = std::clamp(from + rowDelta_, 0, static_cast<int>(project_->tracks.size()) - 1);
            core::AudioClip ghost = *clip;
            ghost.start = std::max<core::SamplePosition>(0, clip->start + moveDelta_);
            const auto bounds = clipBounds(ghost, to);
            g.setColour(c.accent.withAlpha(0.25f));
            g.fillRoundedRectangle(bounds.toFloat(), theme_.radius.sm);
            g.setColour(c.textPrimary);
            g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), theme_.radius.sm, 1.5f);
        }
    }
    g.restoreState();
}

} // namespace aural::ui
