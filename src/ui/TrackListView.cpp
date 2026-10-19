#include "ui/TrackListView.h"

#include <algorithm>
#include <cmath>

namespace aural::ui
{

TrackListView::TrackListView()
{
    setOpaque(true);
}

void TrackListView::setProject(const core::Project* project)
{
    project_ = project;
    repaint();
}

void TrackListView::setPlayhead(core::SamplePosition position, bool playing)
{
    if (position == playhead_ && playing == playing_)
        return;
    playhead_ = position;
    playing_ = playing;
    repaint();
}

double TrackListView::pixelsPerSample() const noexcept
{
    const double rate = project_ != nullptr && project_->sampleRate > 0.0 ? project_->sampleRate : 48000.0;
    return pixelsPerSecond_ / rate;
}

int TrackListView::xForSample(core::SamplePosition s) const noexcept
{
    return headerWidth + static_cast<int>(std::floor(static_cast<double>(s) * pixelsPerSample()));
}

core::SamplePosition TrackListView::sampleForX(int x) const noexcept
{
    return static_cast<core::SamplePosition>(std::max(0.0, (x - headerWidth) / pixelsPerSample()));
}

void TrackListView::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.background);

    // Ruler
    auto ruler = getLocalBounds().removeFromTop(rulerHeight);
    g.setColour(c.surface);
    g.fillRect(ruler);
    g.setColour(c.border);
    g.fillRect(ruler.removeFromBottom(1));
    g.setFont(juce::FontOptions{theme_.type.caption});
    const double rate = project_ != nullptr ? project_->sampleRate : 48000.0;
    for (int second = 0;; ++second)
    {
        const int x = xForSample(static_cast<core::SamplePosition>(second * rate));
        if (x > getWidth())
            break;
        g.setColour(c.borderStrong);
        g.fillRect(x, rulerHeight - 8, 1, 8);
        g.setColour(c.textSecondary);
        g.drawText(
            juce::String(second) + " s", x + 3, 2, 60, rulerHeight - 8, juce::Justification::centredLeft);
    }

    // Tracks
    int y = rulerHeight;
    if (project_ != nullptr)
    {
        for (const auto& track : project_->tracks)
        {
            const juce::Rectangle<int> row(0, y, getWidth(), rowHeight);
            g.setColour(c.surface);
            g.fillRect(row.withWidth(headerWidth));
            g.setColour(c.border);
            g.fillRect(row.withTrimmedTop(rowHeight - 1));
            g.fillRect(row.withWidth(1).withX(headerWidth - 1));

            g.setColour(track.mute ? c.textDisabled : c.textPrimary);
            g.setFont(juce::FontOptions{theme_.type.body});
            g.drawText(track.name,
                       row.withWidth(headerWidth).reduced(theme_.spacing.sm, 0),
                       juce::Justification::centredLeft,
                       true);

            for (const auto& clip : track.clips)
            {
                const int x0 = xForSample(clip.start);
                const int x1 = std::max(x0 + 2, xForSample(clip.end()));
                const auto block = juce::Rectangle<int>(x0, y + 6, x1 - x0, rowHeight - 12).toFloat();
                g.setColour(c.accent.withAlpha(track.mute ? 0.25f : 0.55f));
                g.fillRoundedRectangle(block, theme_.radius.sm);
                g.setColour(c.accent);
                g.drawRoundedRectangle(block.reduced(0.5f), theme_.radius.sm, 1.0f);
                g.setColour(c.textPrimary);
                g.setFont(juce::FontOptions{theme_.type.caption});
                g.drawText(
                    clip.name, block.reduced(6.0f, 2.0f).toNearestInt(), juce::Justification::topLeft, true);
            }
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

    // Playhead
    const int px = xForSample(playhead_);
    g.setColour(playing_ ? c.success : c.textPrimary);
    g.fillRect(px, 0, 2, getHeight());
}

void TrackListView::mouseDown(const juce::MouseEvent& e)
{
    if (e.x < headerWidth)
        return;
    if (onLocate)
        onLocate(sampleForX(e.x));
}

} // namespace aural::ui
