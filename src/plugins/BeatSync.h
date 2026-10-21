#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

// Note values for tempo-synced times (a delay's time, an arpeggio's step): a
// stepped parameter whose choice 0 is "Off" (the free time in ms or Hz) and
// the rest note values from short to long, each with its triplet (T: two
// thirds as long) before it and its dotted (D: half as long again) after.

namespace northlight::plugins::sync
{

struct NoteValue
{
    const char* name;
    double quarters; // its length in quarter notes (0: off)
};

inline constexpr std::array<NoteValue, 17> noteValues{{
    {"Off", 0.0},
    {"1/64", 1.0 / 16.0},
    {"1/32 T", 1.0 / 12.0},
    {"1/32", 1.0 / 8.0},
    {"1/16 T", 1.0 / 6.0},
    {"1/16", 1.0 / 4.0},
    {"1/16 D", 3.0 / 8.0},
    {"1/8 T", 1.0 / 3.0},
    {"1/8", 1.0 / 2.0},
    {"1/8 D", 3.0 / 4.0},
    {"1/4 T", 2.0 / 3.0},
    {"1/4", 1.0},
    {"1/4 D", 3.0 / 2.0},
    {"1/2 T", 4.0 / 3.0},
    {"1/2", 2.0},
    {"1/2 D", 3.0},
    {"1/1", 4.0},
}};

inline constexpr int choiceCount = static_cast<int>(noteValues.size());

// The parameter's choices, for its ParameterInfo.
inline std::vector<std::string> choices()
{
    std::vector<std::string> names;
    for (const auto& v : noteValues)
        names.emplace_back(v.name);
    return names;
}

inline int choiceOf(float value) noexcept
{
    const auto i = static_cast<int>(std::lround(value));
    return i < 0 ? 0 : i >= choiceCount ? choiceCount - 1 : i;
}

// A choice's length in quarter notes; 0 for off.
inline double quarters(float choice) noexcept
{
    return noteValues[static_cast<size_t>(choiceOf(choice))].quarters;
}

// A synced choice's length in seconds at a tempo (quarter notes a minute).
inline double seconds(float choice, double tempoBpm) noexcept
{
    return quarters(choice) * 60.0 / (tempoBpm > 1.0 ? tempoBpm : 1.0);
}

// The synced choice (1..) nearest a length in quarter notes, by ratio.
inline int nearest(double lengthQuarters) noexcept
{
    int best = 1;
    double bestDistance = 1.0e9;
    for (int i = 1; i < choiceCount; ++i)
    {
        const double d = std::abs(
            std::log(std::max(1.0e-6, lengthQuarters) / noteValues[static_cast<size_t>(i)].quarters));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

} // namespace northlight::plugins::sync
