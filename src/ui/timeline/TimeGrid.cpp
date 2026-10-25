#include "ui/timeline/TimeGrid.h"

namespace northlight::ui
{

void paintGridLines(juce::Graphics& g,
                    const Theme& theme,
                    const core::TempoMap& map,
                    const TimelineViewport& view,
                    juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;
    const auto& c = theme.colours;
    forEachGridLine(map,
                    view,
                    area.getX(),
                    area.getRight(),
                    [&](const GridLine& line)
                    {
                        g.setColour(line.level == GridLevel::bar    ? c.gridBar
                                    : line.level == GridLevel::beat ? c.gridBeat
                                                                    : c.gridSub);
                        g.fillRect(line.x, area.getY(), 1, area.getHeight());
                    });
}

void paintBarsRuler(juce::Graphics& g,
                    const Theme& theme,
                    const core::TempoMap& map,
                    const TimelineViewport& view,
                    juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;
    const auto& c = theme.colours;
    // Label every barStep-th bar, from the bar width where the view starts (bars
    // change width with the tempo and signature, but the labels keep one rhythm).
    constexpr double labelPixels = 80.0;
    const auto firstVisible = std::max<core::SamplePosition>(0, view.first);
    const double viewBarPixels = map.stepSamples({1, 0.0}, firstVisible) / view.samplesPerPixel;
    int barStep = 1;
    while (viewBarPixels * barStep < labelPixels && barStep < (1 << 20))
        barStep *= 2;
    const double rate = map.sampleRate();
    g.setFont(theme.caption());
    forEachGridLine(
        map,
        view,
        area.getX(),
        area.getRight(),
        [&](const GridLine& line)
        {
            if (line.level != GridLevel::bar)
            {
                const bool beat = line.level == GridLevel::beat;
                g.setColour(beat ? c.gridBeat : c.gridSub);
                g.fillRect(line.x, area.getBottom() - (beat ? 6 : 3), 1, beat ? 6 : 3);
                return;
            }
            g.setColour(c.gridBar);
            g.fillRect(line.x, area.getY(), 1, area.getHeight());
            if (line.bar % barStep != 0)
                return;
            g.setColour(c.rulerText);
            juce::String label(line.bar + 1);
            if (line.barPixels < labelPixels)
            {
                const double seconds = static_cast<double>(line.position) / rate;
                label << "  " << static_cast<int>(seconds) / 60 << ":"
                      << juce::String(static_cast<int>(seconds) % 60).paddedLeft('0', 2);
            }
            g.drawText(
                label, line.x + 4, area.getY(), 90, area.getHeight(), juce::Justification::centredLeft);
        });
}

} // namespace northlight::ui
