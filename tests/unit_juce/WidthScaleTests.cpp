#include "ui/strip/WidthScale.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace northlight::ui::strip;
using Catch::Approx;

TEST_CASE("The widener's width axis: linear to 200%, a compressed strip to 800% above it", "[ui][widener]")
{
    const juce::Rectangle<float> plot(0.0f, 20.0f, 600.0f, 200.0f);
    const float strip = widthscale::headroom(plot);
    CHECK(strip == 26.0f); // 14% of 200 px, at most 26
    CHECK(widthscale::headroom({0.0f, 0.0f, 600.0f, 80.0f}) == 16.0f);
    const float edge = widthscale::settableTop(plot);
    CHECK(edge == Approx(plot.getY() + strip));

    // Mono at the floor, 100% half way up the linear part, 200% at the strip's edge.
    CHECK(widthscale::toY(0.0f, plot) == Approx(plot.getBottom()));
    CHECK(widthscale::toY(100.0f, plot) == Approx((plot.getBottom() + edge) * 0.5f));
    CHECK(widthscale::toY(200.0f, plot) == Approx(edge));
    // Past 200%: up the strip on a log scale, 400% half way, 800% and beyond at the top.
    CHECK(widthscale::toY(400.0f, plot) == Approx(edge - strip * 0.5f));
    CHECK(widthscale::toY(800.0f, plot) == Approx(plot.getY()));
    CHECK(widthscale::toY(5000.0f, plot) == Approx(plot.getY()));
    // Wider is always higher.
    float last = widthscale::toY(0.0f, plot);
    for (float w = 10.0f; w <= 1000.0f; w += 10.0f)
    {
        const float y = widthscale::toY(w, plot);
        CHECK(y <= last);
        last = y;
    }

    // A drag sets a width on the linear part only, and lands where it is drawn.
    for (const float w : {0.0f, 37.0f, 100.0f, 163.0f, 200.0f})
        CHECK(widthscale::settableAt(widthscale::toY(w, plot), plot) == Approx(w).margin(0.01));
    CHECK(widthscale::settableAt(plot.getY(), plot) == Approx(200.0f)); // dragged into the strip: 200%
    CHECK(widthscale::settableAt(plot.getBottom() + 40.0f, plot) == Approx(0.0f));
}
