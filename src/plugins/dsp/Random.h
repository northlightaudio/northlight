#pragma once

#include <algorithm>
#include <cstdint>

// Cheap, deterministic, allocation-free noise for the built-in processors. Each
// processor keeps its own seed (and so its own sound); the generators and the
// float mappings they use live here once.

namespace northlight::plugins::dsp
{

// xorshift32 (Marsaglia). A zero seed would stick at zero, so it becomes 1.
struct XorShift32
{
    std::uint32_t state = 1u;

    constexpr XorShift32() noexcept = default;
    constexpr explicit XorShift32(std::uint32_t seed) noexcept : state(seed != 0 ? seed : 1u) {}

    constexpr std::uint32_t bits() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    // The top 24 bits as a float in [0, 1), and that mapped to [-1, 1).
    constexpr float uniform() noexcept { return static_cast<float>(bits() >> 8) * (1.0f / 16777216.0f); }
    constexpr float bipolar() noexcept { return 2.0f * uniform() - 1.0f; }

    // The whole word scaled to -1..1 in float (rounding can reach either end), and
    // that mapped to 0..1.
    constexpr float bipolarWord() noexcept
    {
        return static_cast<float>(bits()) * (2.0f / 4294967296.0f) - 1.0f;
    }
    constexpr float unitWord() noexcept { return 0.5f * (bipolarWord() + 1.0f); }
};

// The Numerical Recipes linear congruential generator. Its low bits are poor, so
// the mappings use the top 24.
struct Lcg
{
    std::uint32_t state = 1u;

    constexpr Lcg() noexcept = default;
    constexpr explicit Lcg(std::uint32_t seed) noexcept : state(seed) {}

    constexpr std::uint32_t bits() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    // [0, 1) in double and in float (the same value: 24 bits fit either exactly).
    constexpr double unitDouble() noexcept { return static_cast<double>(bits() >> 8) / 16777216.0; }
    constexpr float unit() noexcept { return static_cast<float>(bits() >> 8) / 16777216.0f; }

    // [-1, 1), worked in double and rounded once.
    constexpr float bipolar() noexcept
    {
        return static_cast<float>(static_cast<double>(bits() >> 8) / 8388608.0 - 1.0);
    }
};

// A smooth random curve in -1..1: a new random point every period (jittered
// +-40%), eased between with a smoothstep, so the drift has no corners.
struct Drift
{
    float from = 0.0f, to = 0.0f;
    int left = 0, length = 1;

    float next(XorShift32& rng, float periodSamples) noexcept
    {
        if (left <= 0)
        {
            from = to;
            to = rng.bipolarWord();
            length = std::max(1, static_cast<int>(periodSamples * (0.6f + 0.8f * rng.unitWord())));
            left = length;
        }
        const float t = 1.0f - static_cast<float>(left) / static_cast<float>(length);
        --left;
        return from + (to - from) * t * t * (3.0f - 2.0f * t);
    }
};

} // namespace northlight::plugins::dsp
