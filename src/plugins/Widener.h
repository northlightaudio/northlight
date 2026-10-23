#pragma once

#include "engine/Insert.h"

#include <array>
#include <memory>
#include <optional>
#include <string>

// The stereo widener: mid/side width in three bands split by phase-coherent
// Linkwitz-Riley crossovers, a "mono below" high-pass on the side, a spread that
// widens mono or nearly mono sources (an allpass decorrelator, or a Haas delay,
// carried in the side only so it cancels in the mono sum), rotation and balance
// of the image, a correlation guard that narrows the image when it nears out of
// phase, level compensation so the loudness holds as the width changes, and an
// output gain. No latency. JUCE-free.
//
// The signal path, per sample:
//   M = (L + R) / 2, S = (L - R) / 2
//   S += spread * D(M)                      D: the decorrelator or the Haas delay, high-passed
//   S  = HP4(mono below)(S)                 LR4: in phase with the allpass the mid gets
//   S  = wLow * LP4(f1) AP(f2) S + wMid * HP4(f1) LP4(f2) S + wHigh * HP4(f1) HP4(f2) S
//   M  = AP(mono below) AP(f1) AP(f2) M     the same phase as every side band
//   S *= guard; (M, S) *= compensation; rotate (M, S); L = M + S, R = M - S; balance; output
// With every width at 100% and no spread the output is the input through one
// allpass on both channels: flat, the image and the mono sum unchanged. The mono
// sum is 2 M whatever the widths and the spread do.

namespace northlight::plugins::widener
{

inline const std::string type = "northlight.widener";

enum Param
{
    lowWidth,   // %, 0 (mono) .. 200
    midWidth,   // %
    highWidth,  // %
    lowSplit,   // Hz, the low / mid crossover
    highSplit,  // Hz, the mid / high crossover
    monoBelow,  // Hz, the side's high-pass; at monoOffHz it is off
    spread,     // %, how much decorrelated (or delayed) mid goes into the side
    spreadMode, // choice: Diffuse, Haas
    haasTime,   // ms, the Haas delay
    rotation,   // degrees, -45 (towards the left) .. +45 (the right)
    balance,    // %, -100 (left only) .. +100 (right only)
    guard,      // on / off: narrow the image when the correlation falls below guardAt
    guardAt,    // correlation, the guard's threshold
    output,     // dB
    count
};

enum SpreadMode
{
    diffuse = 0,
    haas = 1
};

inline constexpr float minLowSplitHz = 40.0f, maxLowSplitHz = 1000.0f;
inline constexpr float minHighSplitHz = 1000.0f, maxHighSplitHz = 16000.0f;
inline constexpr float maxWidthPercent = 200.0f;
// At or below this Mono below is off (the filter parks at parkedMonoHz, which
// only clears subsonic side content).
inline constexpr float monoOffHz = 20.0f;
inline constexpr float parkedMonoHz = 10.0f;
inline constexpr float maxMonoBelowHz = 500.0f;
// The side gain of the spread signal at 100%: each channel's colour stays within
// about +4.6 / -10.5 dB, the pair's power stays flat.
inline constexpr float spreadMaxGain = 0.7f;
// The spread signal is high-passed here so it never decorrelates the bass.
inline constexpr float spreadHighPassHz = 120.0f;
inline constexpr float minHaasMs = 1.0f, maxHaasMs = 30.0f;
// The correlation the card shows and the guard works from: one-pole averages.
inline constexpr float correlationSeconds = 0.3f;
inline constexpr float guardEnergySeconds = 0.05f;
inline constexpr float guardAttackSeconds = 0.02f;
inline constexpr float guardReleaseSeconds = 0.4f;
inline constexpr float minGuardAt = -0.8f, maxGuardAt = 0.6f;
// Level compensation: the power going in over the power after the width, averaged
// over this, limited to these gains.
inline constexpr float compensationSeconds = 0.3f;
inline constexpr float minCompensationDb = -6.0f, maxCompensationDb = 3.0f;
// The decorrelator: second-order allpasses at these frequencies (Q below).
inline constexpr std::array<float, 8> decorrelatorHz{
    190.0f, 330.0f, 610.0f, 1070.0f, 1930.0f, 3450.0f, 6200.0f, 11300.0f};
inline constexpr float decorrelatorQ = 0.6f;

// A width in percent as the side's gain.
inline float sideGain(float widthPercent) noexcept
{
    return widthPercent * 0.01f;
}

// The guard's largest side gain (<= 1) that keeps the correlation of L = M + aS,
// R = M - aS at or above `threshold`, for mid and side energies (assumes M and S
// uncorrelated: corr = (Em - a^2 Es) / (Em + a^2 Es)).
float guardGainFor(float midEnergy, float sideEnergy, float threshold) noexcept;

// The correlation in a feedback frame's `extra` (output) and `extra2` (input):
// correlation + 2 while there is signal, 0 in silence (a frame keeps the
// largest of its samples' values, so silence must sit below any reading).
inline float encodeCorrelation(float correlation, bool signal) noexcept
{
    return signal ? correlation + 2.0f : 0.0f;
}
inline std::optional<float> decodeCorrelation(float value) noexcept
{
    if (value < 0.5f)
        return std::nullopt;
    return value - 2.0f;
}

// What the processor publishes: each feedback frame carries the input and output
// peaks, the guard's narrowing in percent as `reduction` (0 = not narrowing, 100 =
// mono), and the output and input correlation (encodeCorrelation) as `extra` and
// `extra2`. Its two taps carry the output's left (tap 0) and right (tap 1)
// channels, not the before and after mono sums, for the card's vectorscope and
// width spectrum; read them at the same tap position (both are written per block).
std::unique_ptr<engine::InsertProcessor> create();

} // namespace northlight::plugins::widener
