#pragma once

#include "core/TempoMap.h"
#include "ui/timeline/TimelineViewport.h"

#include <algorithm>

// The bar, beat and sub-beat lines every timeline view draws (the arrange view's
// lanes and ruler, the piano roll's grid and ruler, the audio editor's), and the
// one snapping rule they edit with. Both follow the tempo track: a bar's beats
// and divisions are counted in the signature of that bar, wherever the view
// starts or a clip sits. No JUCE: unit tested on its own.

namespace northlight::ui
{

enum class GridLevel
{
    bar,
    beat,
    sub, // a half or a quarter of a beat
};

struct GridLine
{
    core::SamplePosition position = 0;
    int x = 0;
    int bar = 0; // 0-based
    GridLevel level = GridLevel::bar;
    double barPixels = 0.0; // the bar's width at this zoom
};

// How dense the lines are (screens/arrange.md "Ruler"): a beat shows when it is at
// least 12 px wide, its halves from 48 px and its quarters from 192 px.
struct GridDensity
{
    static constexpr double beatsFrom = 12.0;
    static constexpr double halvesFrom = 48.0;
    static constexpr double quartersFrom = 192.0;
    static int subdivisions(double beatPixels) noexcept
    {
        return beatPixels >= quartersFrom ? 4 : beatPixels >= halvesFrom ? 2 : 1;
    }
};

// Calls visit(const GridLine&) for every line whose column is in [left, right],
// in time order, from the song's start on (none before it).
template <typename Visit>
void forEachGridLine(
    const core::TempoMap& map, const TimelineViewport& view, int left, int right, Visit&& visit)
{
    const auto firstPainted = std::max<core::SamplePosition>(0, view.sampleAt(left));
    const int firstBar = map.barAtQuarter(map.quartersAt(static_cast<double>(firstPainted)));
    for (int bar = std::max(0, firstBar - 1);; ++bar)
    {
        const double barQuarter = map.barStartQuarter(bar);
        const auto barStart = map.positionAt(barQuarter);
        const int x = view.xOf(barStart);
        if (x > right)
            break;
        const auto signature = map.signatureAtBar(bar);
        const int beatsPerBar = std::max(1, signature.numerator);
        const double beatQuarters = core::TempoMap::quartersPerBeat(signature);
        const double beatPixels = map.stepSamples({0, beatQuarters}, barStart) / view.samplesPerPixel;
        const double barPixels = beatPixels * beatsPerBar;
        if (x >= left)
            visit(GridLine{barStart, x, bar, GridLevel::bar, barPixels});
        if (beatPixels < GridDensity::beatsFrom)
            continue;
        const int subdivisions = GridDensity::subdivisions(beatPixels);
        for (int beat = 0; beat < beatsPerBar; ++beat)
            for (int sub = 0; sub < subdivisions; ++sub)
            {
                if (beat == 0 && sub == 0)
                    continue;
                const auto at = map.positionAt(barQuarter + (beat + static_cast<double>(sub) / subdivisions) *
                                                                beatQuarters);
                const int bx = view.xOf(at);
                if (bx < left || bx > right)
                    continue;
                visit(GridLine{at, bx, bar, sub == 0 ? GridLevel::beat : GridLevel::sub, barPixels});
            }
    }
}

// The snap grid (interaction.md 10.3) at a position, on the tempo track:
// `division` 0 is adaptive (the finest of bar, beat and its divisions at least
// 16 px wide at the zoom), 1 a bar, 4 a beat, 8, 16 and 32 its divisions, each
// beat counted in the signature where the position falls.
inline core::GridStep
snapStepAt(const core::TempoMap& map, core::SamplePosition at, int division, double samplesPerPixel) noexcept
{
    constexpr int adaptiveMinPixels = 16;
    if (division == 1)
        return {1, 0.0};
    if (division > 0)
        return {0,
                core::TempoMap::quartersPerBeat(map.signatureAt(at)) * 4.0 / static_cast<double>(division)};
    return map.adaptiveStep(samplesPerPixel, adaptiveMinPixels, at);
}

// A position on the nearest line of that grid (never before the song's start).
inline core::SamplePosition snappedToGrid(const core::TempoMap& map,
                                          core::SamplePosition at,
                                          int division,
                                          double samplesPerPixel) noexcept
{
    at = std::max<core::SamplePosition>(0, at);
    return std::max<core::SamplePosition>(0, map.snap(at, snapStepAt(map, at, division, samplesPerPixel)));
}

} // namespace northlight::ui
