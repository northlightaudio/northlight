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
    peaks_.addChangeListener(this);
}

ArrangeView::~ArrangeView()
{
    peaks_.removeChangeListener(this);
    scrollbar_.removeListener(this);
}

void ArrangeView::setProject(const core::Project* project)
{
    project_ = project;
    updateScrollbar();
    repaint();
}

void ArrangeView::setPlayhead(core::SamplePosition position, bool playing)
{
    if (position == playhead_ && playing == playing_)
        return;
    playhead_ = position;
    playing_ = playing;

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
    return static_cast<core::SamplePosition>(std::max(1, getWidth() - headerWidth) * samplesPerPixel_);
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
}

void ArrangeView::scrollBarMoved(juce::ScrollBar*, double newRangeStart)
{
    if (updatingScrollbar_)
        return;
    scroll_ = static_cast<core::SamplePosition>(newRangeStart);
    repaint();
}

void ArrangeView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    repaint(); // peaks arrived
}

void ArrangeView::resized()
{
    scrollbar_.setBounds(
        headerWidth, getHeight() - scrollbarHeight, getWidth() - headerWidth, scrollbarHeight);
    updateScrollbar();
}

// ----- input -------------------------------------------------------------------------------

void ArrangeView::mouseDown(const juce::MouseEvent& e)
{
    if (e.x < headerWidth || e.y >= getHeight() - scrollbarHeight)
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
    const float delta = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
    if (delta != 0.0f)
        setScroll(scroll_ -
                  static_cast<core::SamplePosition>(delta * static_cast<float>(visibleSamples()) * 0.5f));
}

// ----- painting ------------------------------------------------------------------------------

void ArrangeView::drawRuler(juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& c = theme_.colours;
    g.setColour(c.surface);
    g.fillRect(area);
    g.setColour(c.border);
    g.fillRect(area.withTrimmedTop(area.getHeight() - 1));

    // Tick every 1, 2, 5, 10, 30 or 60 s depending on zoom, aiming for ~90 px.
    const double rate = sampleRate();
    const double secondsPerPixel = samplesPerPixel_ / rate;
    double step = 1.0;
    for (double candidate : {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0})
    {
        step = candidate;
        if (candidate / secondsPerPixel >= 90.0)
            break;
    }

    g.setFont(juce::FontOptions{theme_.type.caption});
    const double firstSecond = std::floor(static_cast<double>(scroll_) / rate / step) * step;
    for (double t = firstSecond;; t += step)
    {
        const int x = xForSample(static_cast<core::SamplePosition>(t * rate));
        if (x > getWidth())
            break;
        if (x < headerWidth)
            continue;
        g.setColour(c.borderStrong);
        g.fillRect(x, area.getBottom() - 8, 1, 8);
        g.setColour(c.textSecondary);
        const juce::String label = step < 1.0  ? juce::String(t, 2) + " s"
                                   : t >= 60.0 ? juce::String(static_cast<int>(t) / 60) + ":" +
                                                     juce::String(static_cast<int>(t) % 60).paddedLeft('0', 2)
                                               : juce::String(static_cast<int>(t)) + " s";
        g.drawText(label, x + 3, area.getY() + 2, 70, area.getHeight() - 8, juce::Justification::centredLeft);
    }
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
    const auto visible = block.getIntersection(lane.withLeft(headerWidth));
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

    int y = rulerHeight;
    if (project_ != nullptr)
    {
        for (const auto& track : project_->tracks)
        {
            const juce::Rectangle<int> row(0, y, getWidth(), rowHeight);
            g.setColour(c.border);
            g.fillRect(row.withTrimmedTop(rowHeight - 1));

            for (const auto& clip : track.clips)
                drawClip(g, track, clip, row);

            // Header over the lane so clips scrolled left disappear under it.
            g.setColour(c.surface);
            g.fillRect(row.withWidth(headerWidth));
            g.setColour(c.border);
            g.fillRect(row.withWidth(1).withX(headerWidth - 1));
            g.fillRect(row.withWidth(headerWidth).withTrimmedTop(rowHeight - 1));
            g.setColour(track.mute ? c.textDisabled : c.textPrimary);
            g.setFont(juce::FontOptions{theme_.type.body});
            g.drawText(track.name,
                       row.withWidth(headerWidth).reduced(theme_.spacing.sm, 0),
                       juce::Justification::centredLeft,
                       true);
            y += rowHeight;
        }
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
        g.setColour(playing_ ? c.success : c.textPrimary);
        g.fillRect(px, 0, 2, getHeight() - scrollbarHeight);
    }
}

} // namespace aural::ui
