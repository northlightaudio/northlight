#pragma once

// Lumen's face (screens/plugins.md 7.4h): the front of a 1970s suitcase electric
// piano. Its layout and arithmetic, apart from the drawing so the tests can hold
// them. The positions mirror scripts/render/lumen/face_render.py (50 px to its
// unit): move a part in both.

#include "ui/components/HardwareParts.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace northlight::ui::strip::lumenface
{

// The face as drawn, in pixels at 100%.
inline constexpr float width = 1160.0f, height = 600.0f;

// ----- where things are ------------------------------------------------------------------------

// The black tolex case above the chrome trim, the brushed preamp plate below it.
inline constexpr float trimTop = 254.0f, plateTop = 264.0f;

// The harp window: its chrome bezel, and the glass inside it through which four
// tine assemblies are seen (each a tone bar over a tine, its tip at a pickup).
inline const juce::Rectangle<float> windowBezel{30.0f, 22.0f, 590.0f, 222.0f};
inline const juce::Rectangle<float> windowGlass{44.0f, 36.0f, 562.0f, 194.0f};

// The tines' swing frames: rendered at even steps from full swing down to full
// swing up (frame tineFrames / 2 is at rest, which the still face shows).
inline constexpr int tineFrames = 9;

// The two frames to draw for the tines deflected `swing` (-1 down .. 1 up), and
// how far towards the second (the first drawn whole, the second over it).
struct FramePair
{
    int first = 0, second = 0;
    float blend = 0.0f;
};
inline FramePair framesFor(float swing) noexcept
{
    const float at = (std::clamp(swing, -1.0f, 1.0f) + 1.0f) * 0.5f * static_cast<float>(tineFrames - 1);
    FramePair f;
    f.first = std::clamp(static_cast<int>(std::floor(at)), 0, tineFrames - 1);
    f.second = std::min(f.first + 1, tineFrames - 1);
    f.blend = at - static_cast<float>(f.first);
    return f;
}
// At rest, nothing over the still face.
inline bool atRest(float swing) noexcept
{
    return std::abs(swing) < 0.5f / static_cast<float>(tineFrames - 1);
}

// The tines swing as wide as the output is loud, slowed to a few swings a second
// (the real ones would be a blur): `level` 0..1.
inline constexpr float swingsPerSecond = 3.0f;
inline float swingFor(float level, float phase) noexcept
{
    return std::clamp(level, 0.0f, 1.0f) * std::sin(phase);
}

// The nameplate on the case's right, carrying the name, the kind and the model line.
inline const juce::Rectangle<float> nameplate{650.0f, 28.0f, 480.0f, 96.0f};

// The pilot lamp, lit while the insert is on.
inline constexpr juce::Point<float> pilotLamp{1090.0f, 186.0f};
inline constexpr float lampPatch = 40.0f;

// The vibrato's two jewel lamps, lit as each side's level: the light moves between
// them with the pan, pulses in both with the amplitude tremolo.
inline constexpr std::array<juce::Point<float>, 2> vibratoLamps{{{566.0f, 478.0f}, {598.0f, 478.0f}}};

// ----- the knobs ---------------------------------------------------------------------------

inline constexpr float skirt = 40.0f, serviceSkirt = 34.0f;
inline constexpr float pointerImage = 48.0f, servicePointerImage = 42.0f;
inline constexpr float rowA = 348.0f, rowB = 478.0f, serviceRow = 186.0f;
inline constexpr float nameBelow = 42.0f, valueBelow = 58.0f; // under a knob's centre
inline constexpr float serviceNameBelow = 34.0f, serviceValueBelow = 48.0f;

// Each knob on the face, in the order the card binds them.
struct KnobPlace
{
    juce::Point<float> centre;
    bool service; // the small trims on the case
    const char* name;
};
inline constexpr std::array<KnobPlace, 23> knobs{{
    // HARP
    {{83.0f, rowA}, false, "BARK"},
    {{159.0f, rowA}, false, "SYMMETRY"},
    {{235.0f, rowA}, false, "BELL"},
    {{83.0f, rowB}, false, "HARDNESS"},
    {{159.0f, rowB}, false, "DECAY"},
    {{235.0f, rowB}, false, "DAMPER"},
    // TOUCH
    {{325.0f, rowA}, false, "VELOCITY"},
    {{325.0f, rowB}, false, "NOISE"},
    // PREAMP
    {{415.0f, rowA}, false, "BASS"},
    {{491.0f, rowA}, false, "TREBLE"},
    {{415.0f, rowB}, false, "DRIVE"},
    {{491.0f, rowB}, false, "VOLUME"},
    // VIBRATO
    {{657.0f, rowA}, false, "RATE"},
    {{733.0f, rowA}, false, "SYNC"},
    {{657.0f, rowB}, false, "DEPTH"},
    {{733.0f, rowB}, false, "SHAPE"},
    // PHASE
    {{823.0f, rowA}, false, "RATE"},
    {{899.0f, rowA}, false, "DEPTH"},
    {{823.0f, rowB}, false, "FEEDBACK"},
    {{899.0f, rowB}, false, "MIX"},
    // CHORUS
    {{989.0f, rowA}, false, "RATE"},
    {{1065.0f, rowA}, false, "DEPTH"},
    {{1027.0f, rowB}, false, "MIX"},
}};
// The service trims on the case, under the nameplate.
inline constexpr std::array<KnobPlace, 3> serviceKnobs{{
    {{700.0f, serviceRow}, true, "TUNE"},
    {{790.0f, serviceRow}, true, "AGE"},
    {{880.0f, serviceRow}, true, "VOICES"},
}};

// Where a knob's cap (and the pointer turning on it) lands in the render: seen
// from 40 units in front, a cap standing proud of the plate sits a little further
// from the face's centre than its base.
inline juce::Point<float> onCap(const KnobPlace& k) noexcept
{
    constexpr float camera = 40.0f;
    const float standing = k.service ? 0.3f : 0.36f; // units (50 px)
    const float scale = camera / (camera - standing);
    return {width * 0.5f + (k.centre.x - width * 0.5f) * scale, 300.0f + (k.centre.y - 300.0f) * scale};
}

// The vibrato's lever: OFF in the middle, PAN up, AMP down (the tape echo's
// rendered bat handle, drawn at leverScale of its size, its nut at leverNut).
inline constexpr juce::Point<float> leverNut{581.0f, rowA};
inline constexpr float leverScale = 0.6f;
// The lever's positions (up, centre, down) against the tremolo's mode
// (0 off, 1 pan, 2 amplitude).
inline constexpr std::array<int, 3> leverModes{1, 0, 2};
inline int leverPositionFor(int mode) noexcept
{
    for (int p = 0; p < 3; ++p)
        if (leverModes[static_cast<size_t>(p)] == mode)
            return p;
    return 1;
}

// ----- the captions ----------------------------------------------------------------------

// Each section's caption over its columns, with a rule either side to its width.
struct Caption
{
    const char* text;
    float centreX, columnWidth;
};
inline constexpr float captionY = 290.0f;
inline constexpr std::array<Caption, 6> captions{{
    {"HARP", 159.0f, 228.0f},
    {"TOUCH", 325.0f, 76.0f},
    {"PREAMP", 453.0f, 152.0f},
    {"VIBRATO", 657.0f, 228.0f},
    {"PHASE", 861.0f, 152.0f},
    {"CHORUS", 1027.0f, 152.0f},
}};

} // namespace northlight::ui::strip::lumenface
