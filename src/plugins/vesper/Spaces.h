#pragma once

#include "plugins/FeedbackNetwork.h"

#include <array>

// Vesper's three spaces (plate, chamber, hall, in the order of the Space
// choice): each a tuning of the shared feedback network (plugins/FeedbackNetwork),
// how hard the send drives it, how much it diffuses, how it moves, and its early
// reflections. JUCE-free.

namespace northlight::plugins::vesper
{

inline constexpr int spaceCount = 3;
inline constexpr int maxEarlyTaps = 8;

// One space: its network's tuning, how hard the send drives it, how much it
// diffuses, how it moves, and its early reflections (none for the plate).
struct Space
{
    fdn::Tuning tuning;
    float inject = 1.0f;
    float diffusion = 0.8f;
    float modSeconds = 0.0003f, modHz = 0.6f;
    int taps = 0;
    std::array<float, maxEarlyTaps> tapMs{}, tapGain{};
};

// The spaces, made once (thread-safe; the first call is not real-time safe).
const std::array<Space, spaceCount>& spaces();

} // namespace northlight::plugins::vesper
