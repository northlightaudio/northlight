#pragma once

#include "core/Model.h"

#include <algorithm>
#include <cmath>

// How a timeline view maps time to pixels: the arrange view, the piano roll (in
// clip-relative samples), the audio editor (in the clip's source samples) and the
// lanes the arrange view hosts (take lanes, automation, the tempo track) all use
// this one value, so a sample lands in the same column in every view and a
// column means the same samples. No JUCE: the rule is unit tested on its own.

namespace northlight::ui
{

struct TimelineViewport
{
    int originX = 0;                // the x where `first` is drawn
    core::SamplePosition first = 0; // the sample at originX
    double samplesPerPixel = 1.0;

    // The column a sample falls in: its exact x rounded down. The one rule, both
    // ways: column x holds the samples from sampleAt(x) up to sampleAt(x + 1), and
    // xOf maps each of them back to x.
    int xOf(core::SamplePosition s) const noexcept
    {
        return originX + static_cast<int>(std::floor(static_cast<double>(s - first) / samplesPerPixel));
    }
    // Where a position falls, not rounded to a column: for curves and shapes
    // drawn between columns.
    double exactX(double s) const noexcept
    {
        return static_cast<double>(originX) + (s - static_cast<double>(first)) / samplesPerPixel;
    }
    // The first sample of a column: the first whose exact x is at or after x (for
    // a fractional x, the first sample at or after that point).
    core::SamplePosition sampleAt(double x) const noexcept
    {
        return first + offsetAt(x - static_cast<double>(originX), samplesPerPixel);
    }
    core::SamplePosition sampleAt(int x) const noexcept { return sampleAt(static_cast<double>(x)); }

    // The same view at another zoom, the sample under `anchorX` staying where it is.
    TimelineViewport zoomedAround(double newSamplesPerPixel, int anchorX) const noexcept
    {
        TimelineViewport v = *this;
        v.samplesPerPixel = newSamplesPerPixel;
        v.first = sampleAt(anchorX) - offsetAt(static_cast<double>(anchorX - originX), newSamplesPerPixel);
        return v;
    }

    // The zoom factor for a wheel movement (Ctrl+wheel in every timeline view):
    // smooth, a notch (about a quarter of a unit) about 0.8, up (positive) zooming in.
    static double wheelZoomFactor(double deltaY) noexcept { return std::pow(1.25, -deltaY * 4.0); }
    // A wheel zoom around a point, within a view's zoom range.
    TimelineViewport wheelZoomed(double deltaY,
                                 int anchorX,
                                 double minSamplesPerPixel,
                                 double maxSamplesPerPixel) const noexcept
    {
        return zoomedAround(
            std::clamp(samplesPerPixel * wheelZoomFactor(deltaY), minSamplesPerPixel, maxSamplesPerPixel),
            anchorX);
    }

private:
    static core::SamplePosition offsetAt(double pixels, double samplesPerPixel) noexcept
    {
        return static_cast<core::SamplePosition>(std::ceil(pixels * samplesPerPixel));
    }
};

} // namespace northlight::ui
