#pragma once

#include "core/TempoMap.h"

#include <array>
#include <cstddef>
#include <string_view>

// The piano roll's snap grid (screens/piano-roll.md, interaction.md 10.3): Adaptive,
// Bar, the straight divisions of the beat, and their triplet and dotted variants.
// A division counts the beat of the bar where a note falls (a 6/8 bar's eighths),
// as the arrange view's grid follows the tempo track. No JUCE.
//
// The arrange view's own grid list lives with it for now; this list is one table
// so the piano roll can take a shared list (the arrange's Bar/Beat/Adaptive and
// its triplet and dotted values) by swapping the table and stepOf below.

namespace northlight::ui
{

struct NoteGrid
{
    enum class Kind : unsigned char
    {
        adaptive, // the finest of bar, beat and its divisions at least 16 px wide
        bar,
        beats, // `beats` of the bar's beat a step
    };
    enum class Family : unsigned char
    {
        straight,
        triplet,
        dotted,
    };
    Kind kind = Kind::beats;
    double beats = 0.25;
    std::string_view label = "1/16";
    Family family = Family::straight;
};

inline constexpr std::array<NoteGrid, 13> noteGrids{{
    {NoteGrid::Kind::adaptive, 0.0, "Adaptive"},
    {NoteGrid::Kind::bar, 0.0, "Bar"},
    {NoteGrid::Kind::beats, 1.0, "1/4"},
    {NoteGrid::Kind::beats, 0.5, "1/8"},
    {NoteGrid::Kind::beats, 0.25, "1/16"},
    {NoteGrid::Kind::beats, 0.125, "1/32"},
    {NoteGrid::Kind::beats, 2.0 / 3.0, "1/4T", NoteGrid::Family::triplet},
    {NoteGrid::Kind::beats, 1.0 / 3.0, "1/8T", NoteGrid::Family::triplet},
    {NoteGrid::Kind::beats, 1.0 / 6.0, "1/16T", NoteGrid::Family::triplet},
    {NoteGrid::Kind::beats, 1.0 / 12.0, "1/32T", NoteGrid::Family::triplet},
    {NoteGrid::Kind::beats, 1.5, "1/4.", NoteGrid::Family::dotted},
    {NoteGrid::Kind::beats, 0.75, "1/8.", NoteGrid::Family::dotted},
    {NoteGrid::Kind::beats, 0.375, "1/16.", NoteGrid::Family::dotted},
}};

inline constexpr std::size_t defaultNoteGrid = 4; // 1/16

// The grid step at a timeline position.
inline core::GridStep noteGridStep(const NoteGrid& grid,
                                   const core::TempoMap& map,
                                   core::SamplePosition at,
                                   double samplesPerPixel) noexcept
{
    switch (grid.kind)
    {
        case NoteGrid::Kind::adaptive:
            return map.adaptiveStep(samplesPerPixel, 16, at);
        case NoteGrid::Kind::bar:
            return {1, 0.0};
        case NoteGrid::Kind::beats:
            break;
    }
    return {0, core::TempoMap::quartersPerBeat(map.signatureAt(at)) * grid.beats};
}

// The straight division `steps` finer (negative: coarser) than `index`, for the
// [ and ] keys; a triplet or dotted grid steps within its own family; Adaptive
// and Bar step into the straight divisions.
inline std::size_t noteGridStepped(std::size_t index, int steps) noexcept
{
    const auto& from = noteGrids[index < noteGrids.size() ? index : defaultNoteGrid];
    const auto family = from.kind == NoteGrid::Kind::beats ? from.family : NoteGrid::Family::straight;
    double beats = from.kind == NoteGrid::Kind::beats ? from.beats : (steps > 0 ? 2.0 : 0.125 / 2.0);
    std::size_t best = index;
    for (int i = 0; i < (steps > 0 ? steps : -steps); ++i)
    {
        // The next value in the family past `beats` in the direction asked.
        double next = steps > 0 ? 0.0 : 1.0e9;
        std::size_t found = noteGrids.size();
        for (std::size_t g = 0; g < noteGrids.size(); ++g)
        {
            const auto& candidate = noteGrids[g];
            if (candidate.kind != NoteGrid::Kind::beats || candidate.family != family)
                continue;
            const bool beyond =
                steps > 0 ? candidate.beats < beats - 1.0e-9 : candidate.beats > beats + 1.0e-9;
            const bool closer = steps > 0 ? candidate.beats > next : candidate.beats < next;
            if (beyond && closer)
            {
                next = candidate.beats;
                found = g;
            }
        }
        if (found == noteGrids.size())
            break;
        best = found;
        beats = next;
    }
    return best;
}

} // namespace northlight::ui
