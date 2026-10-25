#pragma once

// Patina's face (screens/plugins.md 7.2a): where things are on it, in pixels at
// 100% of the face's 1160 x 680, and what its card and its stage windows share.
// The face is drawn at that size and scaled to fit the card's body.

#include "plugins/Patina.h"

#include <juce_graphics/juce_graphics.h>

#include <array>

namespace northlight::ui::strip::patinaface
{

// The stages' names, and the telemetry history a card keeps for their windows.
constexpr int historyFrames = 600; // three seconds of 5 ms frames
constexpr std::array<const char*, plugins::patina::stageCount> stageNames{
    "Heat", "Chamber", "Drift", "Grain", "Wear", "Crush"};

// ----- where things are on the face (pixels at 100%) ----------------------------------------

constexpr float faceWidth = 1160.0f, faceHeight = 680.0f;
constexpr float shadowMargin = 12.0f; // round each plate's and bezel's image

// Along the top, on the hammertone.
const juce::Rectangle<float> nameplateArea{24.0f, 14.0f, 300.0f, 56.0f};
// On the nameplate: the maker's mark etched in a square, then the name and the
// maker's line beside it.
const juce::Rectangle<float> nameplateMark{48.0f, 25.0f, 34.0f, 34.0f};
const juce::Rectangle<float> nameplateName{96.0f, 21.0f, 204.0f, 28.0f};
const juce::Rectangle<float> nameplateMaker{97.0f, 48.0f, 204.0f, 13.0f};
const juce::Rectangle<float> modelLine{344.0f, 33.0f, 340.0f, 18.0f};
const juce::Rectangle<float> previousKey{704.0f, 26.0f, 38.0f, 32.0f};
const juce::Rectangle<float> displayBezel{750.0f, 20.0f, 300.0f, 44.0f};
const juce::Rectangle<float> nextKey{1058.0f, 26.0f, 38.0f, 32.0f};
const juce::Point<float> lampCentre{1124.0f, 42.0f};

// The six stage modules and the master plate.
constexpr float moduleLeft = 20.0f, moduleTop = 92.0f, moduleWidth = 142.0f, moduleGap = 8.0f;
constexpr float moduleHeight = 568.0f;
const juce::Rectangle<float> masterArea{924.0f, moduleTop, 216.0f, moduleHeight};

inline float moduleX(int stage) noexcept
{
    return moduleLeft + static_cast<float>(stage) * (moduleWidth + moduleGap);
}
inline float moduleCentre(int stage) noexcept
{
    return moduleX(stage) + moduleWidth * 0.5f;
}

// Rows down a module.
constexpr float nameRow = 118.0f;
constexpr float buttonWidth = 36.0f, buttonHeight = 24.0f;
const juce::Rectangle<float> windowBezelAt{8.0f, 138.0f, 126.0f, 96.0f}; // x from the module's left
constexpr float openingRadius = 3.0f; // the bezels' openings' corners, which the glass follows
constexpr float bezelFrame = 7.0f;    // the glass opening is inset this far
constexpr float ledRow = 241.0f, ledHeight = 4.0f, ledWidth = 10.0f, ledGap = 3.0f;
constexpr int ledCount = 8;
constexpr float selectorRow = 300.0f, selectorCaptionRow = 337.0f, selectorSkirt = 30.0f;
constexpr float selectorStep = 52.0f; // degrees between a selector's positions
constexpr float mainRow = 394.0f, mainSkirt = 52.0f;
constexpr std::array<float, 2> smallRows{500.0f, 590.0f};
constexpr float smallSkirt = 30.0f, smallSpread = 34.0f;

// The master plate.
const juce::Point<float> ageCentre{1032.0f, 212.0f};
constexpr float ageSkirt = 96.0f;
const juce::Point<float> paceCentre{1032.0f, 380.0f};
constexpr float paceSkirt = 34.0f;
constexpr float outputCaptionRow = 452.0f;
constexpr std::array<float, 3> outputX{966.0f, 1032.0f, 1098.0f};

// A knob's image is this many times its skirt (the skirt is 56% of the render, the
// rest room for its shadow).
constexpr float knobImageRatio = 3.4f / 1.9f;
// The button's image is 60 x 45 round its 36 x 24 bezel.
constexpr float buttonImageWidth = 60.0f, buttonImageHeight = 45.0f;

inline juce::Rectangle<float> glassOf(int stage) noexcept
{
    return windowBezelAt.translated(moduleX(stage), 0.0f).reduced(bezelFrame);
}

} // namespace northlight::ui::strip::patinaface
