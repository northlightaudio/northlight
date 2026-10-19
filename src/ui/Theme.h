#pragma once

#include <juce_graphics/juce_graphics.h>

// Design tokens. The LookAndFeel and every view read colours, spacing, radii and
// type sizes from here and nowhere else, so the implementation can track the Figma
// design system token for token. Phase 7 replaces the hard-coded values with a
// loader for the exported token file; the struct shape is the contract.

namespace aural::ui
{

struct Theme
{
    struct Colours
    {
        juce::Colour background;    // window background
        juce::Colour surface;       // panels, toolbars
        juce::Colour surfaceRaised; // controls at rest
        juce::Colour surfaceHover;  // controls under the pointer
        juce::Colour surfacePressed;
        juce::Colour border;
        juce::Colour borderStrong;
        juce::Colour textPrimary;
        juce::Colour textSecondary;
        juce::Colour textDisabled;
        juce::Colour accent;
        juce::Colour accentHover;
        juce::Colour accentText; // text on an accent fill
        juce::Colour focus;
        juce::Colour success;
        juce::Colour warning;
        juce::Colour danger;
        juce::Colour meter;
        juce::Colour meterHot;
        juce::Colour meterClip;
    };

    struct Spacing
    {
        int xs = 4;
        int sm = 8;
        int md = 12;
        int lg = 16;
        int xl = 24;
        int xxl = 32;
    };

    struct Radius
    {
        float sm = 4.0f;
        float md = 6.0f;
        float lg = 10.0f;
    };

    struct Type
    {
        float caption = 11.0f;
        float body = 13.0f;
        float title = 15.0f;
        float heading = 20.0f;
    };

    struct Sizes
    {
        int controlHeight = 28;
        int rowHeight = 26;
        int labelWidth = 120;
        int meterHeight = 6;
    };

    Colours colours;
    Spacing spacing;
    Radius radius;
    Type type;
    Sizes sizes;

    static const Theme& dark();
};

} // namespace aural::ui
