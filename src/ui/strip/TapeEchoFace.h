#pragma once

// The tape echo's console face (screens/plugins.md 7.2a): its arithmetic, apart
// from the drawing so the tests can hold it. The face is drawn at one size and
// scaled to fit its card's body; its meter is a VU, its level lamps neon bulbs,
// and its rollers turn with the tape.

#include "engine/Gain.h"
#include "ui/components/HardwareParts.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui::strip::tapeface
{

// The face as drawn, in pixels at 100%.
inline constexpr float width = 1160.0f, height = 600.0f;

// The scale at which the face fits a body (as large as fits, never larger than
// drawn), and the face's width at a body height, so the card narrows with it.
inline float fitScale(float bodyWidth, float bodyHeight) noexcept
{
    return hardware::fitScale(bodyWidth, bodyHeight, width, height);
}

inline int widthForHeight(int bodyHeight) noexcept
{
    return hardware::widthForHeight(bodyHeight, width, height);
}

// ----- the meter -----------------------------------------------------------------------------

// The meter reads the tape's level at the record head (each frame's peak). The
// tape saturates towards -6 dBFS and cannot go past it; 0 VU sits 4 dB under that,
// where the saturation begins to bite, so a healthy record level reads about 0 and
// only a hot, saturating tape reaches the red.
inline constexpr float vuReferenceDbfs = -10.0f;

// A level (linear, full scale 1) in VU.
inline float vuOf(float linear) noexcept
{
    return engine::gainToDb(linear, -120.0f) - vuReferenceDbfs;
}

// The needle's angle from upright, in degrees, for a reading in VU. The scale is
// linear in voltage, as a VU's is: -20 near the left end, 0 about 71% across, +3
// at the right; 50 degrees each side, and the needle stops 3 degrees past them.
inline float vuAngle(float vu) noexcept
{
    const float share = std::pow(10.0f, vu / 20.0f) / std::pow(10.0f, 3.0f / 20.0f);
    return std::clamp(-50.0f + 100.0f * share, -53.0f, 53.0f);
}

// The needle's movement: a VU's 300 ms to 99% of a step, as two equal lags (a
// heavy needle, critically damped, so it never jumps).
struct VuNeedle
{
    float level = 0.0f; // what the needle shows, linear
    float inner = 0.0f;

    void advance(float target, double seconds) noexcept
    {
        constexpr double lag = 0.045; // two of these reach 99% of a step in 0.3 s
        const auto k = static_cast<float>(1.0 - std::exp(-std::max(0.0, seconds) / lag));
        inner += (target - inner) * k;
        level += (inner - level) * k;
    }
};

// ----- the loop gain lamp ---------------------------------------------------------------

// Dark while the loop is steady; amber as it nears the edge of oscillation; red
// once it sings.
inline constexpr float lampNearGain = 0.85f;
enum class Lamp
{
    dark,
    near,
    singing,
};
inline Lamp lampFor(float loopGain, bool on) noexcept
{
    if (!on || loopGain < lampNearGain)
        return Lamp::dark;
    return loopGain >= 1.0f ? Lamp::singing : Lamp::near;
}

// ----- the neons -------------------------------------------------------------------------------

// A neon's brightness, 0..1, for the level passing its head (linear): it strikes
// 42 dB down and is full at full scale.
inline float neonBrightness(float linear) noexcept
{
    const float db = engine::gainToDb(linear, -120.0f);
    return std::clamp((db + 42.0f) / 42.0f, 0.0f, 1.0f);
}

// The glow lights at once and dies away over about 120 ms, as a neon's does.
inline float neonFollow(float shown, float target, double seconds) noexcept
{
    if (target >= shown)
        return target;
    return target + (shown - target) * static_cast<float>(std::exp(-std::max(0.0, seconds) / 0.12));
}

// An inactive head's neon still shows the tape passing it, dimmer.
inline constexpr float inactiveNeon = 0.35f;

// ----- the rollers --------------------------------------------------------------------------

// The tape travels one head spacing (1.2 units in the rendered transport) in head
// 1's time. A roller of `radius` units turns at the tape's speed over its
// radius, in radians a second; `cap` keeps the fastest roller turning slowly
// enough to be seen (a fast one would strobe), both slowed alike.
inline constexpr double headSpacingUnits = 1.2, pinchRadius = 0.28, tensionRadius = 0.15;
inline constexpr double rollerCap = 2.0 * 3.14159265358979323846 * 3.0; // three turns a second

inline double tapeSpeed(double motorSeconds) noexcept
{
    return headSpacingUnits / std::max(0.005, motorSeconds);
}

inline double rollerSpeed(double motorSeconds, double radius) noexcept
{
    const double fastest = tapeSpeed(motorSeconds) / tensionRadius;
    const double slow = fastest > rollerCap ? rollerCap / fastest : 1.0;
    return tapeSpeed(motorSeconds) / radius * slow;
}

} // namespace northlight::ui::strip::tapeface
