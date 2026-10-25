#include "ui/arrange/ArrangeLayout.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

namespace
{
// Three rows: 56 px, 56 px with 40 px of lanes under it, and a hidden one.
ui::ArrangeLayout threeRows(int gapRow = -1, int gap = 0)
{
    static core::Channel a, b, c;
    ui::ArrangeLayout layout;
    layout.build({{&a, 56, 0}, {&b, 56, 40}, {&c, 0, 0}}, gapRow, gap);
    return layout;
}
} // namespace

TEST_CASE("The arrange rows stack with their lanes under them", "[ui][arrange]")
{
    const auto layout = threeRows();
    CHECK(layout.count() == 3);
    CHECK(layout.top(0) == 0);
    CHECK(layout.top(1) == 56);
    CHECK(layout.top(2) == 152);
    CHECK(layout.end() == 152);
    CHECK(layout.height(1) == 56);
    CHECK(layout.blockHeight(1) == 96);
    CHECK(layout.height(2) == 0);
    CHECK(layout.channel(3) == nullptr);
    CHECK(layout.top(9) == 152); // clamped
}

TEST_CASE("The arrange rows answer which row a point is in", "[ui][arrange]")
{
    const auto layout = threeRows();
    CHECK(layout.rowAt(-1) == -1);
    CHECK(layout.rowAt(0) == 0);
    CHECK(layout.rowAt(55) == 0);
    CHECK(layout.rowAt(56) == 1);
    CHECK(layout.rowAt(140) == 1);  // in its lanes
    CHECK(layout.rowAt(152) == -1); // past the end
}

TEST_CASE("A row resizes from its own bottom edge, not under its lanes", "[ui][arrange]")
{
    const auto layout = threeRows();
    CHECK(layout.resizeEdgeAt(52, 4) == 0);
    CHECK(layout.resizeEdgeAt(57, 4) == 0); // a pixel past it
    CHECK(layout.resizeEdgeAt(50, 4) == -1);
    CHECK(layout.resizeEdgeAt(110, 4) == 1);  // row 1's own bottom at 112
    CHECK(layout.resizeEdgeAt(150, 4) == -1); // in row 1's lanes
}

TEST_CASE("A removed row's closing gap moves the rows after it down", "[ui][arrange]")
{
    const auto layout = threeRows(1, 30);
    CHECK(layout.top(1) == 86);
    CHECK(layout.end() == 182);
    CHECK(layout.height(0) == 86); // the gap sits under the row before it
    CHECK(layout.rowAt(60) == 0);
}
