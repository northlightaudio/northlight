#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>
#include <functional>
#include <vector>

namespace northlight::ui
{

// The Diagnostics panel (screens/transport.md): engine health for the user who wants
// to know. Callback time p50/p99/max as sparklines over the last 60 s, dropouts,
// plugin latency, worker thread count, device state and the UI frame time, in
// small and value mono with the status bar's colour thresholds; a "Copy report"
// ghost button copies a text summary. The owner pushes a Sample once a second.
class DiagnosticsPanel final : public juce::Component
{
public:
    explicit DiagnosticsPanel(const Theme& theme);

    struct Sample
    {
        double loadP50 = 0.0, loadP99 = 0.0, loadMax = 0.0;
        int overruns = 0, dropouts = 0;
        juce::String device;
        double sampleRate = 0.0;
        int bufferSize = 0;
        int workerThreads = 0;
        int outputLatencySamples = 0;
        double frameP50Ms = 0.0, frameP99Ms = 0.0, frameMaxMs = 0.0;
        double paintP50Ms = 0.0, paintP99Ms = 0.0, paintMaxMs = 0.0; // the arrange and mixer paints
        std::vector<std::pair<juce::String, int>> trackLatencies;    // name, samples
        bool recording = false;
    };
    void push(const Sample& sample);
    juce::String report() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void drawSparkline(juce::Graphics& g,
                       juce::Rectangle<int> area,
                       const std::deque<double>& values,
                       juce::Colour colour) const;

    const Theme& theme_;
    Sample last_;
    std::deque<double> p50_, p99_, max_;
    juce::TextButton copy_{"Copy report"};
    static constexpr size_t history = 60;
};

} // namespace northlight::ui
