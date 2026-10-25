#include "ui/timeline/TimelineViewport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using northlight::ui::TimelineViewport;

TEST_CASE("A viewport puts a sample in the column that holds it, rounding down", "[ui][timeline]")
{
    const TimelineViewport v{100, 1000, 10.0};
    CHECK(v.xOf(1000) == 100);
    CHECK(v.xOf(1009) == 100);
    CHECK(v.xOf(1010) == 101);
    // Before the first visible sample: still down, not towards the origin.
    CHECK(v.xOf(999) == 99);
    CHECK(v.xOf(990) == 99);
    CHECK(v.xOf(989) == 98);
}

TEST_CASE("A viewport's column starts at the sample that maps back into it", "[ui][timeline]")
{
    const TimelineViewport v{64, 500, 7.5};
    for (int x = 0; x < 300; ++x)
    {
        const auto s = v.sampleAt(x);
        CHECK(v.xOf(s) == x);
        CHECK(v.xOf(v.sampleAt(x + 1) - 1) == x);
    }
    // Left of the origin the samples count down from the first visible one.
    CHECK(v.sampleAt(63) == 500 - 7);
    CHECK(v.xOf(500 - 7) == 63);
}

TEST_CASE("A viewport's exact x is the column's without rounding", "[ui][timeline]")
{
    const TimelineViewport v{10, 0, 4.0};
    CHECK(v.exactX(6.0) == Approx(11.5));
    CHECK(v.xOf(6) == 11);
}

TEST_CASE("Zooming around a point keeps the sample under it", "[ui][timeline]")
{
    const TimelineViewport v{200, 48000, 100.0};
    const int anchor = 650;
    const auto under = v.sampleAt(anchor);
    const auto zoomed = v.zoomedAround(37.0, anchor);
    CHECK(zoomed.samplesPerPixel == Approx(37.0));
    CHECK(zoomed.originX == 200);
    CHECK(zoomed.sampleAt(anchor) == under);
}

TEST_CASE("The wheel zooms every view by one smooth factor within its range", "[ui][timeline]")
{
    // Up zooms in, down out, by the same amount; no movement, no zoom.
    CHECK(TimelineViewport::wheelZoomFactor(0.25) < 1.0);
    CHECK(TimelineViewport::wheelZoomFactor(-0.25) > 1.0);
    CHECK(TimelineViewport::wheelZoomFactor(0.25) * TimelineViewport::wheelZoomFactor(-0.25) == Approx(1.0));
    CHECK(TimelineViewport::wheelZoomFactor(0.0) == Approx(1.0));
    // A notch (about a quarter of a unit) is about a fifth.
    CHECK(TimelineViewport::wheelZoomFactor(0.25) == Approx(0.8));

    const TimelineViewport v{0, 0, 2.0};
    CHECK(v.wheelZoomed(10.0, 100, 1.0, 1000.0).samplesPerPixel == Approx(1.0));
    CHECK(v.wheelZoomed(-10.0, 100, 1.0, 1000.0).samplesPerPixel == Approx(1000.0));
}
