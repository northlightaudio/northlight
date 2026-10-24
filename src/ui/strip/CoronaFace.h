#pragma once

// Corona's face (screens/plugins.md 7.2a): its arithmetic, apart from the drawing
// so the tests can hold it. The face is drawn at one size and scaled to fit its
// card's body. It has two pages, the amp and its cabinet; the utility strip
// along the foot is on both.

#include "plugins/Cabinet.h"
#include "ui/components/HardwareParts.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace northlight::ui::strip::coronaface
{

// The face as drawn, in pixels at 100%.
inline constexpr float width = 1160.0f, height = 600.0f;

inline float fitScale(float bodyWidth, float bodyHeight) noexcept
{
    return hardware::fitScale(bodyWidth, bodyHeight, width, height);
}

inline int widthForHeight(int bodyHeight) noexcept
{
    return hardware::widthForHeight(bodyHeight, width, height);
}

// ----- the cabinet page's baffle ---------------------------------------------------------------

// The baffle's area on the face, and the margin kept round the speakers.
struct Rect
{
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    float centreX() const noexcept { return x + w * 0.5f; }
    float centreY() const noexcept { return y + h * 0.5f; }
};
inline constexpr Rect baffle{20.0f, 24.0f, 680.0f, 464.0f};
inline constexpr float baffleMargin = 36.0f;
// At most a metre to 1000 px (a 12" speaker is then 300 px across).
inline constexpr float maxPixelsPerMetre = 1000.0f;

struct Speaker
{
    float x = 0.0f, y = 0.0f; // centre on the face
    float diameter = 0.0f;
};

// Where a cabinet's speakers sit on the baffle: the layout scaled to fit, centred.
inline std::vector<Speaker> speakers(int model)
{
    const auto lay = plugins::cabinet::layout(model);
    float minX = 1.0e9f, maxX = -1.0e9f, minY = 1.0e9f, maxY = -1.0e9f;
    const auto d = static_cast<float>(lay.diameter);
    for (const auto& s : lay.speakers)
    {
        minX = std::min(minX, static_cast<float>(s[0]) - d * 0.5f);
        maxX = std::max(maxX, static_cast<float>(s[0]) + d * 0.5f);
        minY = std::min(minY, static_cast<float>(s[1]) - d * 0.5f);
        maxY = std::max(maxY, static_cast<float>(s[1]) + d * 0.5f);
    }
    const float scale = std::min({maxPixelsPerMetre,
                                  (baffle.w - 2.0f * baffleMargin) / (maxX - minX),
                                  (baffle.h - 2.0f * baffleMargin) / (maxY - minY)});
    const float originX = baffle.centreX() - (minX + maxX) * 0.5f * scale;
    const float originY = baffle.centreY() - (minY + maxY) * 0.5f * scale;
    std::vector<Speaker> out;
    for (const auto& s : lay.speakers)
        out.push_back({originX + static_cast<float>(s[0]) * scale,
                       originY + static_cast<float>(s[1]) * scale,
                       d * scale});
    return out;
}

// Each mic works the first speaker along its own ray from the dust cap: A up and
// to the right, B down and to the right (degrees from the positive x axis,
// clockwise on screen), so the two never cover each other.
inline constexpr std::array<float, 2> micRay{-35.0f, 35.0f};
// How far out along the ray 100% (the cone's edge) is, as a share of the radius.
inline constexpr float edgeReach = 0.92f;

struct Point
{
    float x = 0.0f, y = 0.0f;
};

// Where a mic's capsule is drawn for its place on the cone (0..1).
inline Point micPoint(const Speaker& s, int mic, float position) noexcept
{
    const float a = micRay[static_cast<size_t>(std::clamp(mic, 0, 1))] * 3.14159265f / 180.0f;
    const float r = std::clamp(position, 0.0f, 1.0f) * edgeReach * s.diameter * 0.5f;
    return {s.x + r * std::cos(a), s.y + r * std::sin(a)};
}

// The place on the cone (0..1) nearest a point on the face: the point projected
// onto the mic's ray.
inline float positionAt(const Speaker& s, int mic, Point p) noexcept
{
    const float a = micRay[static_cast<size_t>(std::clamp(mic, 0, 1))] * 3.14159265f / 180.0f;
    const float along = (p.x - s.x) * std::cos(a) + (p.y - s.y) * std::sin(a);
    return std::clamp(along / (edgeReach * s.diameter * 0.5f), 0.0f, 1.0f);
}

// A capsule's drawn size for its distance: full size up close, smaller as it
// backs off (to 45% at a metre), as it would look from the speaker.
inline float capsuleScale(float distanceCm) noexcept
{
    const float d = std::clamp(distanceCm, 1.0f, 100.0f);
    return 1.0f - 0.275f * std::log10(d);
}

// The capsules' diameters at full size, by type (dynamic, ribbon, condenser).
inline constexpr std::array<float, 3> capsuleSize{58.0f, 64.0f, 76.0f};

// ----- the amp page's lamps and meter ----------------------------------------------------------

// The pilot jewel's brightness for how hard the power amp is driven (1 = where it
// starts to clip): a glow at idle, full once it clips.
inline float jewelBrightness(float drive, bool on) noexcept
{
    if (!on)
        return 0.0f;
    return 0.3f + 0.7f * std::clamp(drive / 1.2f, 0.0f, 1.0f);
}

// The input meter: a ladder of LEDs, each lit at its level (dBFS); the top two
// warn.
inline constexpr std::array<float, 10> inputSteps{
    -48.0f, -42.0f, -36.0f, -30.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f};
inline constexpr int inputWarnFrom = 8;

inline int litSteps(float inputDb) noexcept
{
    int n = 0;
    for (const float step : inputSteps)
        n += inputDb >= step ? 1 : 0;
    return n;
}

} // namespace northlight::ui::strip::coronaface
