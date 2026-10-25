#pragma once

#include <algorithm>
#include <vector>

// A zero-phase moving average for pitch curves: two passes of a box 2 * radius + 1
// wide (a triangle, so no ripple from the box's corners), each by prefix sums in
// double. Near the ends the box shrinks to what is there. Off the audio thread: it
// allocates.

namespace northlight::engine::pitch
{

inline std::vector<float> boxSmooth(std::vector<float> values, int radius)
{
    const int n = static_cast<int>(values.size());
    std::vector<double> prefix(static_cast<size_t>(n) + 1);
    for (int pass = 0; pass < 2; ++pass)
    {
        prefix[0] = 0.0;
        for (int i = 0; i < n; ++i)
            prefix[static_cast<size_t>(i) + 1] =
                prefix[static_cast<size_t>(i)] + values[static_cast<size_t>(i)];
        for (int i = 0; i < n; ++i)
        {
            const int lo = std::max(0, i - radius), hi = std::min(n, i + radius + 1);
            values[static_cast<size_t>(i)] = static_cast<float>(
                (prefix[static_cast<size_t>(hi)] - prefix[static_cast<size_t>(lo)]) / (hi - lo));
        }
    }
    return values;
}

} // namespace northlight::engine::pitch
