#include "app/Benchmarks.h"

#include "app/ProjectSession.h"
#include "audio/AudioDeviceLayer.h"
#include "core/Command.h"
#include "plugins/ChannelStrip.h"
#include "ui/ArrangeView.h"
#include "ui/FrameStats.h"
#include "ui/InsertEditor.h"
#include "ui/components/Knob.h"
#include "ui/shell/Panels.h"
#include "ui/strip/ChannelStripView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace northlight
{

Benchmarks::Benchmarks(Parts parts) : p_(std::move(parts)) {}

Benchmarks::~Benchmarks() = default;

// --benchmark-ui (11.8): plays the project with the mixer open, scrolls the
// arrange view a screen every second, and after `seconds` prints the frame
// interval and paint percentiles to stdout and quits.
void Benchmarks::runBenchmark(int seconds)
{
    uiEndMs_ = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    p_.panels.show(ui::PanelId::mixer);
    ui::FrameStats::reset();
    p_.frames.clear();
    p_.session.returnToStart();
    p_.session.play();
}

// --benchmark-panels: with the Editor in the dock, opens and closes the
// inspector in the side panel `cycles` times and prints the intervals between
// the frames of the slides that reached the screen (16.7 ms is 60 fps), then
// quits.
void Benchmarks::runPanelBenchmark(int cycles)
{
    if (!p_.session.project().tracks.empty())
        p_.select(p_.session.project().tracks.front().id);
    p_.panels.show(ui::PanelId::editor);
    p_.panels.hide(ui::PanelId::browser);
    p_.panels.hide(ui::PanelId::inspector);
    const double start = juce::Time::getMillisecondCounterHiRes() + 1500.0; // let it settle first
    auto toggles = std::make_shared<int>(0);
    timer_ = std::make_unique<juce::TimedCallback>(
        [this, start, cycles, toggles]
        {
            if (juce::Time::getMillisecondCounterHiRes() < start)
                return;
            if (*toggles == 0)
            {
                slidePaints_.clear();
                p_.sidePaintTimes = &slidePaints_;
            }
            if (*toggles < 2 * cycles)
            {
                p_.panels.toggle(ui::PanelId::inspector);
                ++*toggles;
                return;
            }
            timer_->stopTimer();
            p_.sidePaintTimes = nullptr;
            // Intervals within a slide (a gap of over 150 ms is the pause
            // between one slide and the next).
            std::vector<double> intervals;
            for (size_t k = 1; k < slidePaints_.size(); ++k)
                if (const double d = slidePaints_[k] - slidePaints_[k - 1]; d < 150.0)
                    intervals.push_back(d);
            std::sort(intervals.begin(), intervals.end());
            const auto at = [&](double q)
            {
                return intervals.empty() ? 0.0
                                         : intervals[std::min(intervals.size() - 1,
                                                              static_cast<size_t>(intervals.size() * q))];
            };
            juce::String report;
            report << "benchmark-panels: " << cycles << " open/close cycles, " << intervals.size()
                   << " frames shown, " << p_.window.getWidth() << "x" << p_.window.getHeight()
                   << "; ms between frames: p50 " << juce::String(at(0.5), 1) << " p95 "
                   << juce::String(at(0.95), 1) << " max "
                   << juce::String(intervals.empty() ? 0.0 : intervals.back(), 1) << "\n";
            std::printf("%s", report.toRawUTF8());
            std::fflush(stdout);
            juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
        });
    timer_->startTimer(450);
}

// --benchmark-strip-drag: a knob drag on the first track's strip EQ (a
// parameter command every 8 ms, like a fast mouse) while the strip shows;
// prints the intervals between the strip's display ticks, then quits.
void Benchmarks::runStripDragBenchmark(int seconds)
{
    // The first track whose strip has an EQ; with none, say so and quit (a
    // benchmark that never quits hangs the script that ran it).
    core::Id trackId = core::invalidId, eq = core::invalidId;
    for (const auto& track : p_.session.project().tracks)
    {
        for (const auto& m : track.strip)
            if (m.type == plugins::strip::types::eq)
                eq = m.id;
        if (eq != core::invalidId)
        {
            trackId = track.id;
            break;
        }
    }
    if (eq == core::invalidId)
    {
        std::printf("benchmark-strip-drag: no track has a strip EQ\n");
        std::fflush(stdout);
        juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
        return;
    }
    p_.select(trackId);
    p_.panels.show(ui::PanelId::editor);
    const double start = juce::Time::getMillisecondCounterHiRes() + 1000.0; // let it settle first
    const double end = start + seconds * 1000.0;
    timer_ = std::make_unique<juce::TimedCallback>(
        [this, trackId, eq, start, end]
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (now < start)
                return;
            if (!dragging_)
            {
                dragging_ = true;
                ui::strip::ChannelStripView::startMeasuring();
                p_.session.beginGesture("Band 2 gain");
            }
            const int gain =
                plugins::strip::eq::paramIndex(plugins::strip::eq::band2, plugins::strip::eq::gain);
            p_.session.execute(std::make_unique<core::SetInsertParameter>(
                trackId, eq, gain, static_cast<float>(12.0 * std::sin(now * 0.004))));
            if (now < end)
                return;
            p_.session.endGesture();
            timer_->stopTimer();
            auto intervals = ui::strip::ChannelStripView::takeTickIntervals();
            std::sort(intervals.begin(), intervals.end());
            const auto at = [&](double q)
            {
                return intervals.empty() ? 0.0
                                         : intervals[std::min(intervals.size() - 1,
                                                              static_cast<size_t>(intervals.size() * q))];
            };
            juce::String report;
            report << "benchmark-strip-drag: " << intervals.size() << " strip ticks in "
                   << juce::String((end - start) / 1000.0, 1) << " s ("
                   << juce::String(intervals.size() * 1000.0 / (end - start), 1)
                   << " per second); interval ms p50 " << juce::String(at(0.5), 1) << " p95 "
                   << juce::String(at(0.95), 1) << " max "
                   << juce::String(intervals.empty() ? 0.0 : intervals.back(), 1) << "\n";
            std::printf("%s", report.toRawUTF8());
            std::fflush(stdout);
            juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
        });
    timer_->startTimer(8);
}

// --benchmark-hover: moves the pointer from knob to knob over the open editor
// windows, a knob a frame, for `seconds`, and times every paint of those
// windows meanwhile (the body and its children, by the window's own renderer);
// prints the percentiles, then quits. The hover goes through the knob's own
// handlers, so nothing reaches the desktop.
void Benchmarks::runHoverBenchmark(int seconds)
{
    const double start = juce::Time::getMillisecondCounterHiRes() + 1500.0; // let it settle first
    const double end = start + seconds * 1000.0;
    auto state = std::make_shared<HoverRun>();
    timer_ = std::make_unique<juce::TimedCallback>(
        [this, start, end, state]
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (now < start)
                return;
            if (state->knobs.empty())
            {
                state->knobs = hoverTargets();
                ui::InsertEditor::paintTimes = &state->ms;
                if (state->knobs.empty())
                {
                    std::printf("benchmark-hover: no knobs to hover\n");
                    std::fflush(stdout);
                    juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
                    timer_->stopTimer();
                    return;
                }
            }
            if (now < end)
            {
                auto& knobs = state->knobs;
                const auto event = [](juce::Component& c)
                {
                    const auto at = c.getLocalBounds().getCentre().toFloat();
                    const auto time = juce::Time::getCurrentTime();
                    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                                            at,
                                            juce::ModifierKeys(),
                                            0.0f,
                                            0.0f,
                                            0.0f,
                                            0.0f,
                                            0.0f,
                                            &c,
                                            &c,
                                            time,
                                            at,
                                            time,
                                            0,
                                            false);
                };
                if (auto* last = state->previous.getComponent())
                    last->mouseExit(event(*last));
                auto* knob = knobs[state->next++ % knobs.size()].getComponent();
                if (knob == nullptr)
                    return;
                knob->mouseEnter(event(*knob));
                state->previous = knob;
                return;
            }
            timer_->stopTimer();
            ui::InsertEditor::paintTimes = nullptr;
            auto& ms = state->ms;
            std::sort(ms.begin(), ms.end());
            const auto at = [&](double q)
            { return ms.empty() ? 0.0 : ms[std::min(ms.size() - 1, static_cast<size_t>(ms.size() * q))]; };
            juce::String report;
            report << "benchmark-hover: " << ms.size() << " hovers over " << state->knobs.size()
                   << " knobs; editor paint ms p50 " << juce::String(at(0.5), 2) << " p95 "
                   << juce::String(at(0.95), 2) << " max " << juce::String(ms.empty() ? 0.0 : ms.back(), 2)
                   << "\n";
            std::printf("%s", report.toRawUTF8());
            std::fflush(stdout);
            juce::MessageManager::callAsync([] { juce::JUCEApplication::getInstance()->quit(); });
        });
    timer_->startTimer(16);
}

// The knobs on the open editor windows.
std::vector<juce::Component::SafePointer<ui::Knob>> Benchmarks::hoverTargets() const
{
    std::vector<juce::Component::SafePointer<ui::Knob>> out;
    std::function<void(juce::Component&)> collect = [&](juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (!child->isVisible())
                continue;
            if (auto* k = dynamic_cast<ui::Knob*>(child))
                out.emplace_back(k);
            collect(*child);
        }
    };
    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
        if (auto* top = desktop.getComponent(i);
            top != nullptr && top != p_.window.getTopLevelComponent() && top->isVisible())
            collect(*top);
    return out;
}

void Benchmarks::tick(int ticks)
{
    if (uiEndMs_ <= 0.0)
        return;
    if (ticks % 30 == 0)
        p_.arrange.setScroll(p_.arrange.scrollPosition() + p_.arrange.visibleSamplesForBenchmark());
    if (juce::Time::getMillisecondCounterHiRes() < uiEndMs_)
        return;
    uiEndMs_ = 0.0;
    p_.session.stop();
    std::vector<double> sorted(p_.frames.begin(), p_.frames.end());
    std::sort(sorted.begin(), sorted.end());
    const auto at = [&](double q)
    {
        return sorted.empty() ? 0.0
                              : sorted[std::min(sorted.size() - 1, static_cast<size_t>(sorted.size() * q))];
    };
    const auto paints = ui::FrameStats::summary();
    const auto stats = p_.audio.callback().stats().summary();
    juce::String report;
    report << "benchmark-ui: tracks " << p_.session.project().tracks.size() << ", " << p_.window.getWidth()
           << "x" << p_.window.getHeight() << " at "
           << juce::String(juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->scale * 100.0, 0)
           << "%\n"
           << "frame interval ms: p50 " << juce::String(at(0.5), 2) << " p99 " << juce::String(at(0.99), 2)
           << " max " << juce::String(sorted.empty() ? 0.0 : sorted.back(), 2) << " (" << sorted.size()
           << " frames)\n"
           << "paint ms (arrange + mixer): p50 " << juce::String(paints.p50Ms, 2) << " p99 "
           << juce::String(paints.p99Ms, 2) << " max " << juce::String(paints.maxMs, 2) << " ("
           << paints.frames << " paints)\n"
           << "audio load: p50 " << juce::String(stats.p50 * 100.0, 1) << "% p99 "
           << juce::String(stats.p99 * 100.0, 1) << "% max " << juce::String(stats.max * 100.0, 1) << "%\n";
    std::printf("%s", report.toRawUTF8());
    std::fflush(stdout);
    juce::JUCEApplication::getInstance()->quit();
}

} // namespace northlight
