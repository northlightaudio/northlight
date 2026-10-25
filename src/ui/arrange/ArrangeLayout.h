#pragma once

#include "core/Model.h"

#include <vector>

// The arrange view's rows as plain numbers (no JUCE, unit tested): each row a
// channel (the tracks, then the buses, then the master) with its own height and
// the lanes under it (take lanes, automation lanes), stacked from the top of the
// lanes area in content coordinates (before the vertical scroll).

namespace northlight::ui
{

struct ArrangeRow
{
    const core::Channel* channel = nullptr;
    int height = 0; // the row itself (0: hidden, as in a closed folder)
    int below = 0;  // its take and automation lanes
};

class ArrangeLayout
{
public:
    // Lays the rows out; from `gapRow` on (when >= 0) every row sits `gapHeight`
    // lower: a removed row's space, closing.
    void build(std::vector<ArrangeRow> rows, int gapRow = -1, int gapHeight = 0);

    int count() const noexcept { return static_cast<int>(rows_.size()); }
    const core::Channel* channel(int row) const noexcept;
    // A row's top, and the content's end (the last row's bottom).
    int top(int row) const noexcept; // clamped to [0, count()]
    int end() const noexcept { return tops_.back(); }
    // The row's own height (and a closing gap under it) and its whole block (with
    // the lanes under it).
    int height(int row) const noexcept;
    int blockHeight(int row) const noexcept;
    // The tops of every row and the end, to tell whether a refresh moved anything.
    const std::vector<int>& tops() const noexcept { return tops_; }
    // The row whose block holds a content y, or -1.
    int rowAt(int contentY) const noexcept;
    // The row whose own bottom edge is within `reach` above (or 1 px below) a
    // content y, or -1: where a drag resizes that row.
    int resizeEdgeAt(int contentY, int reach) const noexcept;

private:
    std::vector<ArrangeRow> rows_;
    std::vector<int> tops_{0};
};

} // namespace northlight::ui
