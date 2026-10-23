#pragma once

#include <juce_graphics/juce_graphics.h>

#include <string>
#include <vector>

// Design tokens, loaded from tokens.json (docs/design/tokens.json is the source;
// the build ships it beside the app and embeds a copy). Every view reads colours,
// spacing, radii, sizes, type and motion from here and nowhere else, so the
// implementation tracks the design system token for token. A user may override
// tokens with a tokens.json beside the settings file (keys that are present win;
// the rest keep the shipped values).

namespace northlight::ui
{

struct Theme
{
    struct Colours
    {
        // Surfaces (section 2.1)
        juce::Colour canvas, surface0, surface1, surface2, surface3, surfacePressed, overlay;
        // Lines
        juce::Colour lineSubtle, lineStrong, lineFocus;
        // Text
        juce::Colour textPrimary, textSecondary, textTertiary, textDisabled, textOnAccent, textOnColour;
        // Accent
        juce::Colour accent, accentHover, accentPressed, accentSubtle, selectionFill, selectionOutline;
        // The primary button: moonlight, with peat text (brand.md)
        juce::Colour primary, primaryHover, primaryPressed, textOnPrimary;
        // Semantic
        juce::Colour record, recordHover, recordSubtle, danger, warning, success, info;
        juce::Colour mute, solo, arm, monitor;
        // Meters
        juce::Colour meterLow, meterMid, meterHot, meterClip, meterTrack, meterPeakHold, loudnessTarget;
        // Timeline
        juce::Colour playhead, playheadRecording, gridBar, gridBeat, gridSub, rulerText, loopFill, loopEdge,
            punchFill, punchEdge, markerFlag, regionFill, laneAlt, laneHover, laneSelected, dropTarget,
            snapGuide;
        // Data
        juce::Colour automation, midiNote, midiNoteVelocityLow;

        // Names the views used before the token file (each maps onto a token above
        // and goes away as the screens are restyled).
        juce::Colour background, surface, surfaceRaised, surfaceHover, border, borderStrong, accentText,
            focus, meter;
    };

    struct TrackColour
    {
        std::string name;
        juce::Colour value;
    };
    struct Track
    {
        std::vector<TrackColour> palette;
        std::string defaultBus = "Slate";
        // Palette entries offered as accent presets in Preferences, the default first.
        std::vector<std::string> accentPresets;
        float clipBodyAlpha = 0.22f;
        float clipBodySelectedAlpha = 0.32f;
        float clipHeaderAlpha = 0.55f;
        float waveformAlpha = 0.9f;
        float waveformMutedAlpha = 0.35f;
        int headerStripeWidth = 4;
        // The palette entry for a model colour (0 = default: the slot's cycle colour
        // for tracks, the default bus colour for buses), or a custom colour.
        juce::Colour forTrack(std::uint32_t argb, int slot) const;
        juce::Colour forBus(std::uint32_t argb) const;
    };

    struct Fonts
    {
        std::string uiFamily = "Inter", uiFile = "Inter-Variable.ttf";
        std::string monoFamily = "JetBrains Mono", monoFile = "JetBrainsMono-Variable.ttf";
        struct Size
        {
            float caption = 11, small = 12, body = 13, label = 13, title = 16, heading = 20, timeLarge = 22,
                  timeSmall = 13, value = 12;
        } size;
        struct Weight
        {
            int regular = 400, medium = 500, semibold = 600;
        } weight;
        float lineHeight = 1.3f;
        float letterSpacingCaps = 0.04f;
        juce::Typeface::Ptr ui, mono; // bundled typefaces, null when not bundled
    };

    struct Spacing
    {
        int xxs = 2, xs = 4, sm = 8, md = 12, lg = 16, xl = 24, xxl = 32, xxxl = 48;
    };

    struct Radius
    {
        float sm = 3.0f, md = 6.0f, lg = 10.0f, pill = 999.0f;
    };

    // The type scale, as sizes the views pass to juce::FontOptions (kept for the
    // views not yet restyled; new code uses Theme::font()).
    struct Type
    {
        float caption = 11.0f, small = 12.0f, body = 13.0f, label = 13.0f, title = 16.0f, heading = 20.0f,
              timeLarge = 22.0f, timeSmall = 13.0f, value = 12.0f;
    };

    struct Sizes
    {
        int controlCompact = 24, control = 28, controlLarge = 32;
        int iconSmall = 14, icon = 16, iconLarge = 20;
        int headerBar = 48, statusBar = 24, ruler = 32;
        int trackHeaderWidth = 220, trackHeaderMinWidth = 160, trackHeaderMaxWidth = 360;
        int trackRowMin = 28, trackRow = 56, trackRowLarge = 112, trackRowMax = 240, clipHeader = 18;
        int takeLane = 40; // a take lane under an expanded track (screens/comping.md)
        int mixerStripWidth = 84, mixerStripWideWidth = 112;
        int faderTravel = 200, faderCapWidth = 36, faderCapHeight = 14, knob = 28;
        int meterWidth = 6, meterGap = 2, scrollbar = 10, splitter = 5;
        int panelMin = 240, dockMin = 200, focusRing = 2, tooltipMaxWidth = 320;
        // Pre-token names (see Colours).
        int controlHeight = 28, rowHeight = 28, labelWidth = 120, meterHeight = 6;
    };

    struct Motion
    {
        int hoverMs = 100, pressMs = 60, toggleMs = 120, panelMs = 180, menuMs = 120, toastMs = 200,
            tooltipDelayMs = 500, meterRiseMs = 40, peakHoldMs = 1500;
        // How long work runs before its progress sheet appears. A delay, not an
        // animation: reduce-motion leaves it alone.
        int progressDelayMs = 400;
        float meterFallDbPerSec = 20.0f;
        bool reduceMotion = false; // the OS setting, read at load
        // A duration with reduce-motion applied.
        int ms(int duration) const noexcept { return reduceMotion ? 0 : duration; }
    };

    struct MeterZone
    {
        float fromDb = -60.0f, toDb = 0.0f;
        juce::Colour colour;
    };
    struct Meter
    {
        float floorDb = -60.0f;
        std::vector<MeterZone> zones;
        int clipHoldMs = 3000;
        std::vector<float> scaleTicksDb;
        float loudnessTargetLufs = -14.0f;
        float loudnessRangeLow = -36.0f, loudnessRangeHigh = 0.0f;
        // The zone colour for a level.
        juce::Colour colourAt(float db) const;
    };

    // A shadow or glow: "x y blur #colour", "inset x y blur #colour".
    struct Shadow
    {
        float x = 0.0f, y = 0.0f, blur = 0.0f;
        juce::Colour colour;
        bool inset = false;
        bool empty() const noexcept { return colour.isTransparent(); }
    };

    struct Elevation
    {
        float popupShadowRadius = 8.0f;
        juce::Point<float> popupShadowOffset{0.0f, 8.0f};
        juce::Colour popupShadow{0x80000000};
    };

    // The tactile layer (design-system.md 2.4): panels stay flat, controls get
    // a consistent lighting model from these.
    struct Material
    {
        juce::Colour controlTop, controlBottom, controlHighlight;
        Shadow controlShadow, pressedInset;
        juce::Colour capTop, capBottom, capHighlight;
        Shadow capShadow;
        Shadow needleShadow; // a meter needle's shadow on its face
        juce::Colour groove, grooveLip;
        Shadow grooveInset;
        juce::Colour knobBodyLight, knobBodyDark, knobRim, knobHighlight;
        juce::Colour meterWell;
        Shadow meterWellInset;
        int ledPitchPx = 2, ledGapPx = 1;
        Shadow glowAccent, glowRecord, glowSolo, glowMonitor;
        juce::Colour readoutWell, readoutText, readoutRecord;
        bool enabled = false; // false until a material block is loaded
    };

    // A hardware face (design-system.md "Hardware face"): an effect drawn as a
    // piece of studio equipment over its own textures (Patina's rack unit).
    struct Hardware
    {
        juce::Colour panel, panelWear;           // the panel without its texture; the worn edge
        juce::Colour legend, legendShadow;       // printed legends, and the shade under them
        juce::Colour plate, ink;                 // a nameplate without its image; its lettering
        juce::Colour bezel, keyTop, keyBottom;   // a recessed slot; a bakelite key's body
        juce::Colour keyGlyph;                   // the arrow on a key
        juce::Colour displayCentre, displayEdge; // a lit amber display's glass
        juce::Colour displayText, displayDim, displayGlow;
        juce::Colour glassCentre, glassMid, glassEdge; // a lit meter window
        juce::Colour glassOffCentre, glassOffEdge;     // the same, unlit
        juce::Colour glassInk;                         // what is drawn on the window
        juce::Colour glassFrameTop, glassFrameBottom;  // the window's bevelled frame
        juce::Colour led, ledHot, ledOff;
        juce::Colour switchDark, switchLight; // a slide switch's two halves
        juce::Colour knobBody, knobCap, knobPointer;
        juce::Colour lamp; // a pilot lamp's glow
        juce::Colour shadow, highlight;
    };

    std::string name = "Northlight Dark";
    Colours colours;
    Track track;
    Fonts fonts;
    Spacing spacing;
    Radius radius;
    Type type;
    Sizes sizes;
    Motion motion;
    Meter meter;
    Elevation elevation;
    Material material;
    Hardware hardware;

    // ----- fonts ---------------------------------------------------------------------
    enum class Face
    {
        ui,
        mono,
    };
    // A font of the UI or numeric face at a token size and weight (400/500/600).
    juce::Font font(Face face, float size, int weight) const;
    juce::Font caption() const { return font(Face::ui, fonts.size.caption, fonts.weight.medium); }
    juce::Font small() const { return font(Face::ui, fonts.size.small, fonts.weight.regular); }
    juce::Font body() const { return font(Face::ui, fonts.size.body, fonts.weight.regular); }
    juce::Font label() const { return font(Face::ui, fonts.size.label, fonts.weight.medium); }
    juce::Font title() const { return font(Face::ui, fonts.size.title, fonts.weight.semibold); }
    juce::Font heading() const { return font(Face::ui, fonts.size.heading, fonts.weight.semibold); }
    juce::Font timeLarge() const { return font(Face::mono, fonts.size.timeLarge, fonts.weight.medium); }
    juce::Font timeSmall() const { return font(Face::mono, fonts.size.timeSmall, fonts.weight.medium); }
    juce::Font value() const { return font(Face::mono, fonts.size.value, fonts.weight.medium); }
    // Uppercase section captions: caption size, semibold, tracked.
    juce::Font captionCaps() const;

    // ----- loading -------------------------------------------------------------------
    // The shipped tokens (embedded; replaced by assets/tokens.json beside the
    // executable when present), with the user's override file applied on top.
    // Call once before the first window; dark() returns the result.
    static void initialise(const juce::File& userOverride = {});
    // Applies a token document (the full file, or a partial override) to a theme.
    // Unknown keys are ignored; a bad value is reported through `error` and skipped.
    static bool apply(const juce::var& tokens, Theme& theme, std::string& error);
    static bool applyText(const std::string& json, Theme& theme, std::string& error);
    // The theme with every value from the built-in defaults (no file).
    static Theme defaults();
    // The current theme.
    static const Theme& dark();
    // The file the tokens came from, for diagnostics ("embedded" when none).
    static const std::string& source();
    // Parses "x y blur #colour" (optionally led by "inset").
    static bool parseShadow(const juce::String& text, Shadow& out);
};

} // namespace northlight::ui
