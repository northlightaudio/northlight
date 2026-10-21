#include "ui/shell/StatusBar.h"

#include <cmath>

namespace northlight::ui
{

StatusBar::StatusBar(const Theme& theme) : theme_(theme)
{
    setOpaque(true);
}

void StatusBar::setState(const State& state)
{
    // Rebuilt and repainted only when something shown changed (11.6).
    if (state.deviceOpen == state_.deviceOpen &&
        state.devicePreferredUnavailable == state_.devicePreferredUnavailable &&
        state.driver == state_.driver && state.device == state_.device &&
        state.sampleRate == state_.sampleRate && state.bufferSize == state_.bufferSize &&
        std::abs(state.roundTripMs - state_.roundTripMs) < 0.05 &&
        state.roundTripMeasured == state_.roundTripMeasured &&
        std::abs(state.loadP50 - state_.loadP50) < 0.005 &&
        std::abs(state.loadP99 - state_.loadP99) < 0.005 &&
        std::abs(state.loadMax - state_.loadMax) < 0.005 && state.dropouts == state_.dropouts &&
        state.recording == state_.recording && std::abs(state.diskFreeGb - state_.diskFreeGb) < 0.05 &&
        state.warning == state_.warning)
        return;
    state_ = state;
    rebuildSegments();
    repaint();
}

void StatusBar::showMessage(const juce::String& message)
{
    message_ = message;
    messageShownAt_ = juce::Time::getMillisecondCounterHiRes();
    startTimer(250);
    repaint();
    // A live region for screen readers (11.3).
    juce::AccessibilityHandler::postAnnouncement(message,
                                                 juce::AccessibilityHandler::AnnouncementPriority::low);
}

void StatusBar::setHint(const juce::String& hint)
{
    if (hint == hint_)
        return;
    hint_ = hint;
    repaint();
}

void StatusBar::timerCallback()
{
    if (juce::Time::getMillisecondCounterHiRes() - messageShownAt_ > messageMs)
    {
        message_.clear();
        stopTimer();
    }
    repaint();
}

void StatusBar::rebuildSegments()
{
    const auto& c = theme_.colours;
    const auto& s = state_;
    segments_.clear();
    Segment device;
    device.text = s.deviceOpen ? s.driver + juce::String::fromUTF8(" \xc2\xb7 ") + s.device
                               : juce::String("No audio device");
    device.tooltip = s.devicePreferredUnavailable ? "The preferred device is unavailable; using " + s.device +
                                                        ". Click to open Audio Setup."
                                                  : "Click to open Audio Setup";
    device.colour = s.deviceOpen ? c.textSecondary : c.warning;
    device.warningDot = s.devicePreferredUnavailable;
    device.action = &onDeviceClicked;
    segments_.push_back(device);
    if (s.deviceOpen)
    {
        Segment format;
        format.text = juce::String(s.sampleRate / 1000.0, 1) + " kHz" + juce::String::fromUTF8(" \xc2\xb7 ") +
                      juce::String(s.bufferSize);
        format.tooltip = juce::String(s.bufferSize) + " samples = " +
                         juce::String(s.sampleRate > 0.0 ? 1000.0 * s.bufferSize / s.sampleRate : 0.0, 2) +
                         " ms";
        format.colour = c.textSecondary;
        segments_.push_back(format);
        Segment rt;
        rt.text = juce::String(s.roundTripMs, 1) + " ms RT";
        rt.tooltip = s.roundTripMeasured ? "Round trip measured by loopback calibration"
                                         : "Round trip as reported by the driver (not measured)";
        rt.colour = s.roundTripMeasured ? c.textPrimary : c.textSecondary;
        segments_.push_back(rt);
        Segment load;
        load.text = "CPU " + juce::String(static_cast<int>(std::round(s.loadP50 * 100.0))) + "%";
        load.tooltip = "Callback time against the deadline: p50 " + juce::String(s.loadP50 * 100.0, 1) +
                       "%, p99 " + juce::String(s.loadP99 * 100.0, 1) + "%, max " +
                       juce::String(s.loadMax * 100.0, 1) + "%";
        load.colour = s.loadP99 > 0.9 ? c.danger : s.loadP99 > 0.7 ? c.warning : c.textSecondary;
        segments_.push_back(load);
        if (s.dropouts > 0)
        {
            Segment drop;
            drop.text = juce::String(s.dropouts) + (s.dropouts == 1 ? " dropout" : " dropouts");
            drop.tooltip =
                (s.lastDropoutTime.isNotEmpty() ? "Last at " + s.lastDropoutTime + ". " : juce::String()) +
                "Click to reset";
            drop.colour = c.warning;
            drop.action = &onDropoutsClicked;
            segments_.push_back(drop);
        }
        if (s.recording)
        {
            Segment disk;
            disk.text = "Disk " + juce::String(s.diskMbPerSec, 1) + " MB/s" +
                        juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(s.diskFreeGb, 1) + " GB free";
            disk.tooltip = "Recording throughput and free space in the media folder";
            disk.colour = c.textSecondary;
            segments_.push_back(disk);
        }
    }
    if (s.warning.isNotEmpty())
    {
        Segment warning;
        warning.text = s.warning;
        warning.colour = c.warning;
        segments_.push_back(warning);
    }
    // Lay out left to right.
    const auto font = theme_.timeSmall();
    int x = theme_.spacing.md;
    for (auto& segment : segments_)
    {
        const int w = juce::GlyphArrangement::getStringWidthInt(font, segment.text) +
                      (segment.warningDot ? theme_.spacing.md : 0);
        segment.bounds = {x, 0, w, getHeight()};
        x += w + theme_.spacing.xl;
    }
}

void StatusBar::paint(juce::Graphics& g)
{
    const auto& c = theme_.colours;
    g.fillAll(c.surface0);
    g.setFont(theme_.timeSmall());
    for (size_t i = 0; i < segments_.size(); ++i)
    {
        const auto& segment = segments_[i];
        auto area = segment.bounds;
        if (segment.warningDot)
        {
            g.setColour(c.warning);
            const int d = theme_.spacing.sm - theme_.spacing.xxs;
            g.fillEllipse(static_cast<float>(area.getX()),
                          static_cast<float>((getHeight() - d) / 2),
                          static_cast<float>(d),
                          static_cast<float>(d));
            area.removeFromLeft(theme_.spacing.md);
        }
        g.setColour(static_cast<int>(i) == hoverSegment_ && segment.action != nullptr ? c.textPrimary
                                                                                      : segment.colour);
        g.drawText(segment.text, area, juce::Justification::centredLeft);
        if (i + 1 < segments_.size())
        {
            g.setColour(c.textTertiary);
            g.drawText(juce::String::fromUTF8("\xc2\xb7"),
                       segment.bounds.getRight(),
                       0,
                       theme_.spacing.xl,
                       getHeight(),
                       juce::Justification::centred);
        }
    }
    // Right: the transient message, else the hover hint.
    juce::String rightText = message_;
    float alpha = 1.0f;
    if (message_.isNotEmpty())
    {
        const double age = juce::Time::getMillisecondCounterHiRes() - messageShownAt_;
        alpha = age > messageMs - 500 ? static_cast<float>(juce::jlimit(0.0, 1.0, (messageMs - age) / 500.0))
                                      : 1.0f;
    }
    else
        rightText = hint_;
    if (rightText.isNotEmpty())
    {
        g.setColour(c.textSecondary.withMultipliedAlpha(alpha));
        g.setFont(theme_.small());
        const int left = segments_.empty() ? 0 : segments_.back().bounds.getRight() + theme_.spacing.xl;
        g.drawText(rightText,
                   juce::Rectangle<int>(left, 0, getWidth() - left - theme_.spacing.md, getHeight()),
                   juce::Justification::centredRight,
                   true);
    }
}

void StatusBar::mouseMove(const juce::MouseEvent& e)
{
    int hover = -1;
    for (size_t i = 0; i < segments_.size(); ++i)
        if (segments_[i].bounds.contains(e.getPosition()))
            hover = static_cast<int>(i);
    if (hover != hoverSegment_)
    {
        hoverSegment_ = hover;
        setTooltip(hover >= 0 ? segments_[static_cast<size_t>(hover)].tooltip : juce::String());
        repaint();
    }
}

void StatusBar::mouseDown(const juce::MouseEvent& e)
{
    for (const auto& segment : segments_)
        if (segment.bounds.contains(e.getPosition()) && segment.action != nullptr && *segment.action)
            (*segment.action)();
}

} // namespace northlight::ui
