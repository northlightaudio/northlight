#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// The parts of an FM (phase modulation) voice that do not depend on Cirrus's
// parameters: the operator waves, a 32-bit phase, and the four-operator
// algorithms (which operator modulates which, which are heard). JUCE-free and
// real-time safe; the wave tables are built once at static initialisation.

namespace northlight::plugins::fm
{

constexpr int operatorCount = 4;
constexpr int waveCount = 8;
constexpr int tableBits = 11;
constexpr int tableSize = 1 << tableBits; // points per cycle

// Sine, then the half and rectified sines of four-operator FM, a soft square
// and a triangle. Each has its mean removed and peaks at 1.
enum class Wave : std::uint8_t
{
    sine,
    half,       // the first half of a sine, then silence
    rectified,  // both halves of a sine, upwards
    quarters,   // the rising quarters of a sine, then silence
    doubleSine, // a sine at twice the speed for half a cycle, then silence
    doubleHalf, // the rectified twice-speed sine for half a cycle, then silence
    softSquare, // a sine pushed into a rounded square
    triangle,
};

// The waves' tables: tableSize points each plus one guard point (the first
// again), so interpolation never wraps.
const std::array<std::array<float, tableSize + 1>, waveCount>& waveTables() noexcept;

// One cycle of a wave at phase 0..1, for drawing it (not the audio path).
float waveAt(Wave wave, float phase) noexcept;

// A phase as a 32-bit fraction of a cycle: it wraps by itself.
inline std::uint32_t phaseStep(double cyclesPerSample) noexcept
{
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(cyclesPerSample * 4294967296.0));
}
// A modulation in cycles (any size, either sign) as a phase offset.
inline std::uint32_t phaseOffset(float cycles) noexcept
{
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(static_cast<double>(cycles) * 4294967296.0));
}

// One wave's value at a 32-bit phase, linearly interpolated.
inline float lookup(const std::array<float, tableSize + 1>& table, std::uint32_t phase) noexcept
{
    constexpr int fractionBits = 32 - tableBits;
    const auto index = static_cast<size_t>(phase >> fractionBits);
    const float fraction = static_cast<float>(phase & ((1u << fractionBits) - 1u)) *
                           (1.0f / static_cast<float>(1u << fractionBits));
    const float a = table[index];
    return a + (table[index + 1] - a) * fraction;
}

// An algorithm: for each operator (0 is operator 1), the operators that
// modulate it as a bit mask, and the operators heard. In every algorithm an
// operator is only modulated by higher-numbered ones, so a voice runs them from
// operator 4 down to 1 and each sees its modulators' current outputs.
struct Algorithm
{
    std::array<std::uint8_t, operatorCount> modulators{};
    std::uint8_t carriers = 0;

    bool isCarrier(int op) const noexcept { return ((carriers >> op) & 1u) != 0; }
    int carrierCount() const noexcept
    {
        int n = 0;
        for (int op = 0; op < operatorCount; ++op)
            n += isCarrier(op) ? 1 : 0;
        return n;
    }
};

constexpr int algorithmCount = 8;

namespace detail
{
constexpr std::uint8_t op(int n) noexcept
{
    return static_cast<std::uint8_t>(1u << (n - 1));
}
} // namespace detail

// The eight four-operator algorithms (docs/plans/cirrus.md), ">" meaning
// "modulates".
inline constexpr std::array<Algorithm, algorithmCount> algorithms{{
    // 1: 4 > 3 > 2 > 1
    {{detail::op(2), detail::op(3), detail::op(4), 0}, detail::op(1)},
    // 2: (3 + 4) > 2 > 1
    {{detail::op(2), static_cast<std::uint8_t>(detail::op(3) | detail::op(4)), 0, 0}, detail::op(1)},
    // 3: 3 > 2 > 1, 4 > 1
    {{static_cast<std::uint8_t>(detail::op(2) | detail::op(4)), detail::op(3), 0, 0}, detail::op(1)},
    // 4: 4 > 3 > 1, 2 > 1
    {{static_cast<std::uint8_t>(detail::op(2) | detail::op(3)), 0, detail::op(4), 0}, detail::op(1)},
    // 5: 2 > 1, 4 > 3
    {{detail::op(2), 0, detail::op(4), 0}, static_cast<std::uint8_t>(detail::op(1) | detail::op(3))},
    // 6: 4 > 1, 4 > 2, 4 > 3
    {{detail::op(4), detail::op(4), detail::op(4), 0},
     static_cast<std::uint8_t>(detail::op(1) | detail::op(2) | detail::op(3))},
    // 7: 4 > 3; 1, 2 and 3 heard
    {{0, 0, detail::op(4), 0}, static_cast<std::uint8_t>(detail::op(1) | detail::op(2) | detail::op(3))},
    // 8: all four heard
    {{0, 0, 0, 0}, static_cast<std::uint8_t>(detail::op(1) | detail::op(2) | detail::op(3) | detail::op(4))},
}};

// A modulator at full level shifts its target's phase by this many cycles
// (about 12.6 radians: a very bright sound).
constexpr float maxIndexCycles = 2.0f;
// Feedback at full: operator 4 shifts its own phase by up to this (about 1.9
// radians: past a saw, into noise).
constexpr float maxFeedbackCycles = 0.3f;

// An operator level 0..99 as a gain: 0.75 dB a step, 99 at 0 dB, 0 silent.
inline float levelGain(float level) noexcept
{
    if (level <= 0.0f)
        return 0.0f;
    // 2^((level - 99) / 8): 8 steps double the gain.
    return std::exp2((level - 99.0f) * 0.125f);
}

// The Bessel function of the first kind, J_n(x): the amplitude of the nth
// sideband of a sine phase-modulated with index x (radians). Any integer n; x
// up to a few hundred. For displays and tests, not the audio thread's hot path.
double besselJ(int n, double x) noexcept;

} // namespace northlight::plugins::fm
