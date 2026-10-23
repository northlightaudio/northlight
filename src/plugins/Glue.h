#pragma once

#include "engine/Insert.h"

#include <array>
#include <memory>
#include <string>

// The Glue compressor: a VCA bus compressor in the spirit of the classic console
// bus compressors, for holding a mix or a group together. Feed-forward, stereo
// linked, a soft knee, stepped ratio, attack and release like the hardware's
// switches, an Auto release that recovers fast from a transient and slowly from
// sustained compression, a high-pass on the detector so the low end does not pump
// it, make-up and a parallel mix. No lookahead, no latency. JUCE-free.

namespace northlight::plugins::glue
{

inline const std::string type = "northlight.glue";

enum Param
{
    threshold,   // dB, -40..0
    ratio,       // choice: ratios
    attack,      // choice: attacksMs
    release,     // choice: releasesSeconds, then Auto
    makeup,      // dB, 0..+20
    keyHighPass, // Hz on the detector, 20 (off) .. 400
    mix,         // % wet
    count
};

inline constexpr std::array<float, 4> ratios{1.5f, 2.0f, 4.0f, 10.0f};
inline constexpr std::array<float, 6> attacksMs{0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f};
inline constexpr std::array<float, 4> releasesSeconds{0.1f, 0.3f, 0.6f, 1.2f};
inline constexpr int autoRelease = static_cast<int>(releasesSeconds.size()); // the last release choice
// Auto release: a fast stage releasing over the first, a slow stage that charges
// from it over autoChargeSeconds and releases over the last.
inline constexpr float autoFastSeconds = 0.1f;
inline constexpr float autoSlowSeconds = 1.2f;
inline constexpr float autoChargeSeconds = 0.4f;
inline constexpr float kneeDb = 4.0f;
// At or below this the key high-pass is off.
inline constexpr float keyHighPassOffHz = 20.0f;
// The detector levels the card reads are RMS over this time constant.
inline constexpr float rmsSeconds = 0.05f;

// Gain reduction (dB, >= 0) for a detector level, with the soft knee.
float gainReductionDb(float levelDb, float thresholdDb, float ratio) noexcept;
// The ratio, attack (ms) and release (seconds; Auto: the slow stage's) for the
// choice values of the parameters.
float ratioFor(float choice) noexcept;
float attackMsFor(float choice) noexcept;
bool isAutoRelease(float choice) noexcept;
float releaseSecondsFor(float choice) noexcept;

// What the card reads from each feedback frame: input and output peaks, the
// reduction, and the input and output RMS (`extra` and `extra2`).
std::unique_ptr<engine::InsertProcessor> create();

} // namespace northlight::plugins::glue
