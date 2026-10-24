#pragma once

#include <cstdint>

namespace northlight::ui::paintstats
{

// What painting costs, counted rather than timed, so the UI's work budgets
// (tests/unit_juce/UiWorkBudgetTests.cpp) hold on any machine: the words a face
// sets (each measured by its glyphs' outlines, the costly part of its paint) and
// how often a face rebuilds its cached art. Message thread only; a few integer
// increments, always on.
struct Counters
{
    std::uint64_t wordsSet = 0;    // lines set by hardware::drawCrispText
    std::uint64_t artRebuilds = 0; // RenderedFace's art drawn into its cache
    std::uint64_t facePaints = 0;  // RenderedFace::paint calls
};

inline Counters& counters() noexcept
{
    static Counters c;
    return c;
}

inline void reset() noexcept
{
    counters() = {};
}

} // namespace northlight::ui::paintstats
