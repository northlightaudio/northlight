#include "ui/arrange/ArrangeLayout.h"

#include <algorithm>

namespace northlight::ui
{

void ArrangeLayout::build(std::vector<ArrangeRow> rows, int gapRow, int gapHeight)
{
    rows_ = std::move(rows);
    tops_.assign(1, 0);
    for (const auto& r : rows_)
        tops_.push_back(tops_.back() + r.height + r.below);
    if (gapRow >= 0 && gapHeight > 0)
        for (size_t k = static_cast<size_t>(std::min(gapRow, static_cast<int>(tops_.size()) - 1));
             k < tops_.size();
             ++k)
            tops_[k] += gapHeight;
}

const core::Channel* ArrangeLayout::channel(int row) const noexcept
{
    return row >= 0 && row < count() ? rows_[static_cast<size_t>(row)].channel : nullptr;
}

int ArrangeLayout::top(int row) const noexcept
{
    return tops_[static_cast<size_t>(std::clamp(row, 0, count()))];
}

int ArrangeLayout::height(int row) const noexcept
{
    // What the block leaves over the lanes under it: the row's own height, plus a
    // closing gap that sits under it.
    return row >= 0 && row < count() ? std::max(0, blockHeight(row) - rows_[static_cast<size_t>(row)].below)
                                     : 0;
}

int ArrangeLayout::blockHeight(int row) const noexcept
{
    return row >= 0 && row < count() ? top(row + 1) - top(row) : 0;
}

int ArrangeLayout::rowAt(int contentY) const noexcept
{
    // The last top at or above the point (tops ascend; the last is the end).
    const auto it = std::upper_bound(tops_.begin(), tops_.end(), contentY);
    const int row = static_cast<int>(it - tops_.begin()) - 1;
    return row >= 0 && row < count() ? row : -1;
}

int ArrangeLayout::resizeEdgeAt(int contentY, int reach) const noexcept
{
    // The row the point is in and the one above: their own bottoms (lanes under a
    // row do not resize it).
    const auto it = std::upper_bound(tops_.begin(), tops_.end(), contentY);
    const int at = static_cast<int>(it - tops_.begin()) - 1;
    for (const int row : {at - 1, at})
    {
        if (row < 0 || row >= count())
            continue;
        const int bottom = top(row) + height(row);
        if (contentY >= bottom - reach && contentY <= bottom + 1)
            return row;
    }
    return -1;
}

} // namespace northlight::ui
