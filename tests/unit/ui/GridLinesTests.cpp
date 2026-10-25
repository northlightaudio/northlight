#include "ui/timeline/GridLines.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace northlight;
using Catch::Approx;

namespace
{
std::vector<ui::GridLine> linesOf(const core::TempoMap& map, const ui::TimelineViewport& view, int width)
{
    std::vector<ui::GridLine> out;
    ui::forEachGridLine(
        map, view, view.originX, view.originX + width, [&](const ui::GridLine& l) { out.push_back(l); });
    return out;
}

int count(const std::vector<ui::GridLine>& lines, ui::GridLevel level)
{
    int n = 0;
    for (const auto& l : lines)
        n += l.level == level ? 1 : 0;
    return n;
}
} // namespace

TEST_CASE("Grid lines show beats and their divisions as the zoom makes room", "[ui][timeline]")
{
    const core::TempoMap map; // 120 BPM 4/4 at 48 kHz: a beat is 24 000 samples
    // A beat 10 px wide: bars only.
    auto lines = linesOf(map, {0, 0, 2400.0}, 400);
    CHECK(count(lines, ui::GridLevel::bar) == 11);
    CHECK(count(lines, ui::GridLevel::beat) == 0);
    // 12 px: beats.
    lines = linesOf(map, {0, 0, 2000.0}, 96);
    CHECK(count(lines, ui::GridLevel::bar) == 3);
    CHECK(count(lines, ui::GridLevel::beat) == 6);
    CHECK(count(lines, ui::GridLevel::sub) == 0);
    // 48 px: halves; 192 px: quarters.
    lines = linesOf(map, {0, 0, 500.0}, 191);
    CHECK(count(lines, ui::GridLevel::sub) == 4);
    lines = linesOf(map, {0, 0, 125.0}, 767);
    CHECK(count(lines, ui::GridLevel::beat) == 3);
    CHECK(count(lines, ui::GridLevel::sub) == 12);
}

TEST_CASE("Grid lines follow the tempo track's signatures and come in time order", "[ui][timeline]")
{
    // 4/4 for one bar, then 3/4.
    const core::TempoMap map(48000.0, 120.0, {4, 4}, {}, {{1, {3, 4}}});
    const auto lines = linesOf(map, {0, 0, 1000.0}, 300); // a beat is 24 px
    std::vector<int> barBeats;
    int beats = 0;
    for (const auto& l : lines)
    {
        if (l.level == ui::GridLevel::bar)
        {
            if (l.bar > 0)
                barBeats.push_back(beats + 1);
            beats = 0;
        }
        else
            ++beats;
    }
    REQUIRE(barBeats.size() >= 2);
    CHECK(barBeats[0] == 4);
    CHECK(barBeats[1] == 3);
    for (size_t i = 1; i < lines.size(); ++i)
        CHECK(lines[i].position > lines[i - 1].position);
    CHECK(lines.front().x == 0);
    CHECK(lines.front().barPixels == Approx(96.0));
}

TEST_CASE("Grid lines start at the column the view starts at, not before", "[ui][timeline]")
{
    const core::TempoMap map;
    const ui::TimelineViewport view{100, 96000 * 3 + 100, 1000.0};
    const auto lines = linesOf(map, view, 200);
    REQUIRE_FALSE(lines.empty());
    for (const auto& l : lines)
    {
        CHECK(l.x >= 100);
        CHECK(l.x <= 300);
        CHECK(view.xOf(l.position) == l.x);
    }
}

TEST_CASE("The snap grid counts beats in the signature where the position falls", "[ui][timeline]")
{
    // 4/4 for two bars, then 6/8 from quarter 8.
    const core::TempoMap map(48000.0, 120.0, {4, 4}, {}, {{2, {6, 8}}});
    CHECK(ui::snapStepAt(map, map.positionAt(1.0), 4, 100.0) == core::GridStep{0, 1.0});
    CHECK(ui::snapStepAt(map, map.positionAt(9.0), 4, 100.0) == core::GridStep{0, 0.5});
    CHECK(ui::snapStepAt(map, map.positionAt(9.0), 16, 100.0) == core::GridStep{0, 0.125});
    CHECK(ui::snapStepAt(map, 0, 1, 100.0) == core::GridStep{1, 0.0});
    // Adaptive: what is 16 px wide at the zoom.
    CHECK(ui::snapStepAt(map, 0, 0, 100.0) == map.adaptiveStep(100.0, 16, 0));
    CHECK(ui::snappedToGrid(map, map.positionAt(8.6), 4, 100.0) == map.positionAt(8.5));
    CHECK(ui::snappedToGrid(map, -500, 4, 100.0) == 0);
}
