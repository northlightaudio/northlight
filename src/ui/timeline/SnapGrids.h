#pragma once

#include "core/Model.h"
#include "core/TempoMap.h"

#include <string>
#include <vector>

// The snap grids every timeline view offers (interaction.md 10.3): Adaptive,
// Bar, Beat and its straight divisions, and their triplet and dotted variants,
// as one table, so the arrange view's snap menu, the piano roll and the audio
// editor quantise to the same set. No JUCE.
//
// A grid is a code (what the settings file keeps as editing.snapGrid):
//   0          adaptive (the finest of bar, beat and its divisions that fits the zoom)
//   1          a bar
//   4, 8, ...  straight: a beat (4), an eighth of a 4/4 bar's worth (8), ...; the
//              step is a beat * 4 / code, the beat counted in the signature of
//              the bar the position falls in (in 6/8, "Beat" is an eighth)
//   6, 12, ... triplets of the straight grid half their code (12 = 1/8 triplet:
//              three in the time of two eighths, 1/3 of a 4/4 beat)
//   1000 + n   the straight grid n, dotted (half as long again)
// A step that does not fill a bar evenly (a dotted eighth in 4/4) restarts on
// every bar line (core::GridStep).

namespace northlight::ui
{

enum class SnapFamily
{
    adaptive,
    bar,
    straight,
    triplet,
    dotted,
};

struct SnapGridChoice
{
    int code = 0;
    const char* name = "";  // in menus: "1/8 triplet"
    const char* label = ""; // compact, for the header button: "1/8T"
    SnapFamily family = SnapFamily::straight;
};

inline constexpr int dottedSnapBase = 1000;

// Every grid, in menu order: Adaptive, Bar, the straight grids, then the
// triplets and the dotted ones (the snap menu's submenus).
inline const std::vector<SnapGridChoice>& snapGridChoices()
{
    static const std::vector<SnapGridChoice> choices{
        {0, "Adaptive", "Adaptive", SnapFamily::adaptive},
        {1, "Bar", "Bar", SnapFamily::bar},
        {4, "Beat", "Beat", SnapFamily::straight},
        {8, "1/8", "1/8", SnapFamily::straight},
        {16, "1/16", "1/16", SnapFamily::straight},
        {32, "1/32", "1/32", SnapFamily::straight},
        {6, "Beat triplet", "Beat T", SnapFamily::triplet},
        {12, "1/8 triplet", "1/8T", SnapFamily::triplet},
        {24, "1/16 triplet", "1/16T", SnapFamily::triplet},
        {48, "1/32 triplet", "1/32T", SnapFamily::triplet},
        {dottedSnapBase + 4, "Beat dotted", "Beat.", SnapFamily::dotted},
        {dottedSnapBase + 8, "1/8 dotted", "1/8.", SnapFamily::dotted},
        {dottedSnapBase + 16, "1/16 dotted", "1/16.", SnapFamily::dotted},
    };
    return choices;
}

inline const SnapGridChoice* findSnapGrid(int code) noexcept
{
    for (const auto& c : snapGridChoices())
        if (c.code == code)
            return &c;
    return nullptr;
}

// A code the table knows; anything else (an old or hand-edited setting) reads as adaptive.
inline int validSnapGrid(int code) noexcept
{
    return findSnapGrid(code) != nullptr ? code : 0;
}

// The compact name ("1/8T"); "Adaptive" for an unknown code.
inline std::string snapGridLabel(int code)
{
    const auto* c = findSnapGrid(code);
    return c != nullptr ? c->label : "Adaptive";
}

// The step of a fixed grid in quarter notes for a signature (0 for adaptive, and
// for a bar, which is GridStep{1, 0}). Codes outside the table follow the same
// rule (a division of 4 * beats, dotted past dottedSnapBase), so a view may
// offer finer divisions of its own.
inline double snapGridQuarters(int code, core::TimeSignature signature) noexcept
{
    if (code <= 1)
        return 0.0;
    const double beat = core::TempoMap::quartersPerBeat(signature);
    if (code > dottedSnapBase)
        return beat * 4.0 / static_cast<double>(code - dottedSnapBase) * 1.5;
    return beat * 4.0 / static_cast<double>(code); // a triplet's code is already 3/2 of its straight one
}

} // namespace northlight::ui
