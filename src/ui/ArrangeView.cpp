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

void ArrangeView::mouseDown(const juce::MouseEvent& e)
{
    if (e.x < headerWidth || e.x >= getWidth() - scrollbarWidth || e.y >= getHeight() - scrollbarHeight)
        return;
    if (onLocate)
        onLocate(sampleForX(e.x));
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
                           const core::AudioClip& clip,
                           juce::Rectangle<int> lane)
{
    const auto& c = theme_.colours;
    const int x0 = xForSample(clip.start);
    const int x1 = std::max(x0 + 2, xForSample(clip.end()));
    if (x1 < headerWidth || x0 > getWidth())
        return;

    const auto block = juce::Rectangle<int>(x0, lane.getY() + 4, x1 - x0, lane.getHeight() - 8);
    const auto visible =
        block.getIntersection(lane.withLeft(headerWidth).withRight(getWidth() - scrollbarWidth));
    if (visible.isEmpty())
        return;

    g.setColour(c.accent.withAlpha(track.mute ? 0.15f : 0.3f));
    g.fillRoundedRectangle(block.toFloat(), theme_.radius.sm);

    // Waveform: one min/max column per pixel from the peak cache.
    if (const auto peaks = peaks_.peaksFor(clip.mediaPath, sampleRate()); peaks != nullptr && !peaks->empty())
    {
        const int channels = std::min(2, peaks->numChannels());
        const float laneHeight = static_cast<float>(visible.getHeight()) / static_cast<float>(channels);
        g.setColour(track.mute ? c.textDisabled : c.accentHover);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float midY =
                static_cast<float>(visible.getY()) + laneHeight * (static_cast<float>(ch) + 0.5f);
            const float half = laneHeight * 0.48f;
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

    g.setColour(c.accent);
    g.drawRoundedRectangle(block.toFloat().reduced(0.5f), theme_.radius.sm, 1.0f);
    g.setColour(c.textPrimary);
    g.setFont(juce::FontOptions{theme_.type.caption});
    g.drawText(clip.name, visible.reduced(6, 2), juce::Justification::topLeft, true);
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

} // namespace aural::ui
