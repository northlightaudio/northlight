#pragma once

// Orbit's face (screens/plugins.md, Orbit): a studio reel-to-reel in a dim
// mastering room. Its layout and arithmetic, apart from the drawing so the tests
// can hold them. The positions mirror scripts/render/orbit/face_render.py (50 px
// to its unit): move a part in both.

#include "ui/components/HardwareParts.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace northlight::ui::strip::orbitface
{

// The face as drawn, in pixels at 100%.
inline constexpr float width = 1160.0f, height = 600.0f;

// ----- where things are ------------------------------------------------------------------------

// The brushed transport plate above the trim, the control panel below.
inline constexpr float plateBottom = 330.0f, panelTop = 337.0f;

// The reels: each turning frame is a 300 px square round its centre.
inline constexpr float reelFrame = 300.0f;
inline constexpr juce::Point<float> leftReel{190.0f, 165.0f}, rightReel{970.0f, 165.0f};
inline juce::Rectangle<float> reelArea(juce::Point<float> centre) noexcept
{
    return juce::Rectangle<float>(reelFrame, reelFrame).withCentre(centre);
}

// The black nameplate at the top centre, carrying the name and the model line.
inline const juce::Rectangle<float> nameplate{390.0f, 20.0f, 380.0f, 92.0f};

// The counter's window and the pilot lamp beside it.
inline const juce::Rectangle<float> counterWindow{526.0f, 127.0f, 108.0f, 26.0f};
inline constexpr juce::Point<float> lampCentre{496.0f, 140.0f};
inline constexpr float lampPatch = 34.0f;

// The meters: each dial's face, its pivot below the face (hidden by the hood).
inline constexpr std::array<juce::Point<float>, 2> meterCentres{{{127.0f, 414.0f}, {333.0f, 414.0f}}};
inline constexpr float dialWidth = 178.0f, dialHeight = 106.0f;
inline constexpr float pivotBelowCentre = 77.0f; // the pivot, this far under the dial's centre
inline constexpr float scaleRadius = 88.0f;
inline juce::Rectangle<float> dialArea(int meter) noexcept
{
    return juce::Rectangle<float>(dialWidth, dialHeight).withCentre(meterCentres[static_cast<size_t>(meter)]);
}
inline juce::Point<float> pivot(int meter) noexcept
{
    return meterCentres[static_cast<size_t>(meter)].translated(0.0f, pivotBelowCentre);
}
// The hood and glass layers cover the dial and its frame.
inline juce::Rectangle<float> meterCover(int meter) noexcept
{
    return dialArea(meter).expanded(4.0f);
}

// The knobs: the big two's skirts 56 px across, the small four's 38.
inline constexpr float bigSkirt = 56.0f, smallSkirt = 38.0f;
inline constexpr float bigImage = 64.0f, smallImage = 44.0f; // their pointers' squares
struct KnobPlace
{
    juce::Point<float> centre;
    bool big;
    float nameY; // the name's centre line; the value prints under it
};
// Where a knob's cap (and the pointer turning on it) lands in the render: seen from
// 40 units in front, a cap standing proud of the panel sits a little further from
// the face's centre than its base.
inline juce::Point<float> onCap(const KnobPlace& k) noexcept
{
    constexpr float camera = 40.0f;
    const float standing = k.big ? 0.465f : 0.385f; // units (50 px)
    const float scale = camera / (camera - standing);
    return {width * 0.5f + (k.centre.x - width * 0.5f) * scale, 300.0f + (k.centre.y - 300.0f) * scale};
}
// INPUT, BIAS, HISS, FLUTTER, OUTPUT, MIX.
inline constexpr std::array<KnobPlace, 6> knobs{{
    {{515.0f, 415.0f}, true, 465.0f},
    {{515.0f, 520.0f}, false, 553.0f},
    {{934.0f, 410.0f}, false, 443.0f},
    {{934.0f, 497.5f}, false, 530.5f},
    {{1063.0f, 415.0f}, true, 465.0f},
    {{1027.0f, 520.0f}, false, 553.0f},
}};
inline constexpr float valueBelowName = 16.0f;

// The captions over the columns, with a rule either side to the column's width.
struct Caption
{
    const char* text;
    float centreX, columnWidth;
};
inline constexpr float captionY = 364.0f;
inline constexpr std::array<Caption, 4> captions{{
    {"RECORD", 515.0f, 130.0f},
    {"MACHINE", 736.0f, 268.0f},
    {"WEAR", 934.0f, 84.0f},
    {"REPRO", 1063.0f, 130.0f},
}};

// The keys: 64 x 30, in rows. A lit key draws its patch of the lit render, which
// reaches keyMargin round it for the glow on the panel.
inline constexpr float keyWidth = 64.0f, keyHeight = 30.0f, keyMargin = 6.0f;
inline juce::Rectangle<float> keyArea(juce::Point<float> centre) noexcept
{
    return juce::Rectangle<float>(keyWidth, keyHeight).withCentre(centre);
}
inline constexpr std::array<float, 3> machineKeyX{694.0f, 766.0f, 838.0f};
inline constexpr std::array<float, 4> machineRowY{402.0f, 444.0f, 486.0f, 528.0f}; // speed, eq, level, tape
inline constexpr float machineLabelRight = 650.0f;
inline constexpr std::array<float, 3> meterKeyX{172.0f, 244.0f, 316.0f};
inline constexpr float meterKeyY = 500.0f, meterLabelX = 110.0f;
inline constexpr juce::Point<float> linkKey{1095.0f, 520.0f};

// ----- the meters --------------------------------------------------------------------------

// The needle's angle from upright, in degrees, for a reading in VU: linear in
// voltage as a VU's scale is, -20 near the left end, 0 about 71% across, +3 at
// the right, 47 degrees each side; the needle stops 3 degrees past the ends.
inline float vuAngle(float vu) noexcept
{
    const float share = std::pow(10.0f, vu / 20.0f) / std::pow(10.0f, 3.0f / 20.0f);
    return std::clamp(-47.0f + 94.0f * share, -50.0f, 50.0f);
}

// A meter's reading in VU from the processor's level (a rectified average
// calibrated to a sine's rms): 0 VU is the processor's reference level.
inline float vuOf(float level, float referenceDbfs) noexcept
{
    return 20.0f * std::log10(std::max(level, 1.0e-6f)) - referenceDbfs;
}

// The needle's movement: a VU's 300 ms to 99% of a step, as two equal lags.
struct VuNeedle
{
    float level = 0.0f;
    float inner = 0.0f;
    void advance(float target, double seconds) noexcept
    {
        constexpr double lag = 0.045;
        const auto k = static_cast<float>(1.0 - std::exp(-std::max(0.0, seconds) / lag));
        inner += (target - inner) * k;
        level += (inner - level) * k;
    }
};

// ----- the reels ---------------------------------------------------------------------------

// The reels' frames: 30 of them, 4 degrees apart, span the 120 degrees after which
// a three-windowed reel looks the same again.
inline constexpr int reelFrames = 30;
inline constexpr float reelStep = 4.0f, reelSymmetry = 120.0f;

// The two frames to draw for a reel turned `degrees` anticlockwise, and how far
// towards the second (the first drawn whole, the second over it at that opacity).
struct FramePair
{
    int first = 0, second = 1;
    float blend = 0.0f;
};
inline FramePair framesFor(float degrees) noexcept
{
    float a = std::fmod(degrees, reelSymmetry);
    if (a < 0.0f)
        a += reelSymmetry;
    const float position = a / reelStep;
    FramePair f;
    f.first = static_cast<int>(position) % reelFrames;
    f.second = (f.first + 1) % reelFrames;
    f.blend = position - std::floor(position);
    return f;
}

// Whether a reel at `degrees` is at rest (where the still render shows it).
inline bool atRest(float degrees) noexcept
{
    float a = std::fmod(degrees, reelSymmetry);
    if (a < 0.0f)
        a += reelSymmetry;
    return a < 1.0e-3f || reelSymmetry - a < 1.0e-3f;
}

// The packs' radii (inches on a 10.5-inch reel): the supply reel full, the
// take-up a third wound. A reel turns at the tape's speed over its pack's radius.
inline constexpr double supplyPackInches = 4.26, takeUpPackInches = 3.33;
inline double reelDegreesPerSecond(double ips, double packInches) noexcept
{
    return ips / packInches * 180.0 / 3.14159265358979323846;
}

// Stopped, a reel coasts on to its next rest position, slowing as it nears it
// (never slower than coastFloor degrees a second, so it arrives).
inline constexpr double coastFloor = 20.0, coastRate = 6.0; // degrees a second; per second
inline double coast(double degrees, double seconds) noexcept
{
    const double rest = std::ceil(degrees / reelSymmetry - 1.0e-9) * reelSymmetry;
    const double remaining = rest - degrees;
    if (remaining <= 0.0)
        return degrees;
    const double step = std::max(coastFloor, remaining * coastRate) * std::max(0.0, seconds);
    return std::min(rest, degrees + step);
}

// The counter: the song's position as hh:mm:ss.
inline juce::String counterText(double seconds)
{
    const auto total = static_cast<int>(std::max(0.0, seconds));
    return juce::String::formatted("%02d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
}

} // namespace northlight::ui::strip::orbitface
