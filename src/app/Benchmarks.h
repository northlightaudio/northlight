#pragma once

#include "core/Model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

// The developer benchmarks the launch flags run (--benchmark-ui, --benchmark-panels,
// --benchmark-strip-drag, --benchmark-hover; docs/ui-performance.md): each drives
// the window for a while, prints its figures to stdout and quits. Kept out of the
// main window's own code; it hands them the parts they drive and read.

namespace northlight
{
class ProjectSession;
namespace audio
{
class AudioDeviceLayer;
}
namespace ui
{
class ArrangeView;
class Knob;
class PanelManager;
} // namespace ui

class Benchmarks
{
public:
    struct Parts
    {
        juce::Component& window; // the content: its size for the reports
        ProjectSession& session;
        audio::AudioDeviceLayer& audio;
        ui::ArrangeView& arrange;
        ui::PanelManager& panels;
        std::vector<double>*& sidePaintTimes; // the side splitter's paint log (--benchmark-panels)
        std::deque<double>& frames;           // the window's recent tick intervals (ms)
        std::function<void(core::Id)> select; // selects a track as a click would
    };
    explicit Benchmarks(Parts parts);
    ~Benchmarks();

    // --benchmark-ui (11.8): plays the project with the mixer open, scrolls the
    // arrange view a screen every second, and after `seconds` prints the frame
    // interval and paint percentiles and quits. tick() runs it from the window's tick.
    void runBenchmark(int seconds);
    void tick(int ticks);
    // --benchmark-panels: opens and closes the inspector `cycles` times with the
    // Editor docked and prints the intervals between the slides' frames.
    void runPanelBenchmark(int cycles);
    // --benchmark-strip-drag: a fast knob drag on the first strip EQ; prints the
    // intervals between the strip's display ticks.
    void runStripDragBenchmark(int seconds);
    // --benchmark-hover: hovers the open editor windows' knobs, one a frame, and
    // times their paints.
    void runHoverBenchmark(int seconds);

private:
    struct HoverRun
    {
        std::vector<juce::Component::SafePointer<ui::Knob>> knobs;
        juce::Component::SafePointer<ui::Knob> previous;
        size_t next = 0;
        std::vector<double> ms;
    };
    // The knobs on the open editor windows.
    std::vector<juce::Component::SafePointer<ui::Knob>> hoverTargets() const;

    Parts p_;
    std::unique_ptr<juce::TimedCallback> timer_;
    double uiEndMs_ = 0.0;
    bool dragging_ = false;
    std::vector<double> slidePaints_;
};

} // namespace northlight
