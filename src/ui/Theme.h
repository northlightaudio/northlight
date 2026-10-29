#pragma once

#include <juce_graphics/juce_graphics.h>

#include <string>
#include <utility>
#include <vector>

// Design tokens, loaded from tokens.json (docs/design/tokens.json is the source;
// the build ships it beside the app and embeds a copy). Every view reads colours,
// spacing, radii, sizes, type and motion from here and nowhere else, so the
// implementation tracks the design system token for token. A user may override
// tokens with a tokens.json beside the settings file (keys that are present win;
// the rest keep the shipped values).

namespace northlight::core
{
struct Project;
}

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
        juce::Colour accent, accentHover, accentSubtle, selectionFill, selectionOutline;
        // The primary button: moonlight, with peat text (brand.md)
        juce::Colour primary, primaryHover, primaryPressed, textOnPrimary;
        // Semantic
        juce::Colour record, recordHover, danger, warning, success, info;
        juce::Colour mute, solo, arm, monitor;
        // Meters
        juce::Colour meterLow, meterMid, meterHot, meterClip, meterTrack, meterPeakHold, loudnessTarget;
        // Timeline
        juce::Colour playhead, playheadRecording, gridBar, gridBeat, gridSub, rulerText, loopFill, loopEdge,
            punchFill, punchEdge, markerFlag, regionFill, laneAlt, laneHover, laneSelected, dropTarget,
            snapGuide;
        // Data
        juce::Colour automation, midiNote, midiNoteVelocityLow;
    };

    struct TrackColour
    {
        std::string name;
        juce::Colour value;
    };
    struct Track
    {
        std::vector<TrackColour> palette;
        // Palette names: an uncoloured group bus's and FX track's, and the master's.
        std::string defaultBus = "Overcast", defaultFxTrack = "Lichen", master = "Slate";
        // Palette entries offered as accent presets in Preferences, the default first.
        std::vector<std::string> accentPresets;
        float clipBodyAlpha = 0.22f;
        float clipBodySelectedAlpha = 0.32f;
        float clipHeaderAlpha = 0.55f;
        float waveformAlpha = 0.9f;
        float waveformMutedAlpha = 0.35f;
        int headerStripeWidth = 4;
        // The least width a track header keeps for the name; narrower, the header
        // moves R, monitor and A to its second row (screens/arrange.md).
        int headerNameMinWidth = 64;
        // The palette entry for a track's model colour (0 = default: the slot's
        // cycle colour), or a custom colour.
        juce::Colour forTrack(std::uint32_t argb, int slot) const;
        // A channel's colour as every view shows it (screens/arrange.md "Buses and
        // the master"): a track's by forTrack; a bus its own, else defaultFxTrack
        // for an FX track and defaultBus for a group bus; the master always master.
        juce::Colour forChannel(const core::Project& project, std::uint64_t channel) const;
        juce::Colour named(const std::string& name) const; // a palette entry by name
    };

    struct Fonts
    {
        // The bundled typefaces load by file name (tokens.json's family names are documentation).
        std::string uiFile = "Inter-Variable.ttf";
        std::string monoFile = "JetBrainsMono-Variable.ttf";
        struct Size
        {
            // micro: a display's axis labels and corner chips, never running text.
            float micro = 10, caption = 11, small = 12, body = 13, label = 13, title = 16, heading = 20,
                  timeLarge = 22, timeSmall = 13, value = 12;
        } size;
        struct Weight
        {
            int regular = 400, medium = 500, semibold = 600;
        } weight;
        float lineHeight = 1.3f;
        float letterSpacingCaps = 0.04f;
        // Legends printed on a hardware face (the tape echo's console), never the interface.
        std::string legendFile = "BarlowCondensed-SemiBold.ttf", legendBoldFile = "BarlowCondensed-Bold.ttf";
        // The brand's serif: wordmarks set large and sparingly (Vesper's), never controls.
        std::string displayFile = "Newsreader-Variable.ttf";
        std::string displayItalicFile = "Newsreader-Italic-Variable.ttf";
        juce::Typeface::Ptr ui, mono;               // bundled typefaces, null when not bundled
        juce::Typeface::Ptr legend, legendBold;     // the same, for Face::legend
        juce::Typeface::Ptr display, displayItalic; // the same, for displayFont()
    };

    struct Spacing
    {
        int xxs = 2, xs = 4, sm = 8, md = 12, lg = 16, xl = 24, xxl = 32, xxxl = 48;
    };

    struct Radius
    {
        float sm = 3.0f, md = 6.0f, lg = 10.0f, pill = 999.0f;
    };

    struct Sizes
    {
        int controlCompact = 24, control = 28, controlLarge = 32;
        int iconSmall = 14, icon = 16, iconLarge = 20;
        int headerBar = 48, statusBar = 24, ruler = 32;
        int trackHeaderWidth = 260, trackHeaderMinWidth = 160, trackHeaderMaxWidth = 360;
        int trackRowMin = 28, trackRow = 56, trackRowLarge = 112, trackRowMax = 240, clipHeader = 18;
        int takeLane = 40; // a take lane under an expanded track (screens/comping.md)
        // An automation lane under its track, and one collapsed to a strip (and the
        // "Add lane" row under a track's lanes) (screens/arrange.md "Automation").
        int automationLane = 48, automationLaneCollapsed = 20;
        int mixerStripWidth = 92, mixerStripWideWidth = 112;
        // A track header's compact selects (input, output) and volume slider at their narrowest.
        int compactSelectMinWidth = 76, compactVolumeMinWidth = 48;
        int faderTravel = 200, faderCapWidth = 36, faderCapHeight = 14, knob = 28;
        int meterWidth = 6, meterGap = 2, scrollbar = 10, splitter = 5;
        int panelMin = 240, dockMin = 200, focusRing = 2, tooltipMaxWidth = 320;
        // Pre-token names (see Colours).
        int controlHeight = 28, rowHeight = 28, labelWidth = 120;
    };

    struct Motion
    {
        int hoverMs = 100, toggleMs = 120, panelMs = 180, menuMs = 120, toastMs = 200, tooltipDelayMs = 500,
            meterRiseMs = 40, peakHoldMs = 1500;
        // How long work runs before its progress sheet appears. A delay, not an
        // animation: reduce-motion leaves it alone.
        int progressDelayMs = 400;
        float meterFallDbPerSec = 20.0f;
        bool reduceMotion = false; // Preferences' Reduce motion, or Windows' when it follows the system
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
        std::vector<float> scaleTicksDb;
        float loudnessTargetLufs = -14.0f;
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
        juce::Colour panel;                      // the panel without its texture
        juce::Colour legend, legendShadow;       // printed legends, and the shade under them
        juce::Colour bezel, keyTop, keyBottom;   // a recessed slot; a bakelite key's body
        juce::Colour keyGlyph;                   // the arrow on a key
        juce::Colour displayCentre, displayEdge; // a lit amber display's glass
        juce::Colour displayText, displayDim, displayGlow;
        juce::Colour glassCentre, glassMid, glassEdge; // a lit meter window
        juce::Colour glassOffCentre, glassOffEdge;     // the same, unlit
        juce::Colour glassInk;                         // what is drawn on the window
        juce::Colour led, ledHot, ledOff;
        juce::Colour knobBody, knobCap, knobPointer;
        juce::Colour shadow, highlight;
        // The tape echo's console (screens/plugins.md 7.2a): its enamel panels without
        // their textures, the ink printed on each, the meter's dial and needle, the
        // aluminium of its frames, the transport's well and a neon's glow.
        juce::Colour creamPanel, creamInk, creamInkSoft;
        juce::Colour sagePanel, sageInk, sageInkSoft;
        juce::Colour dialFace, dialInk, dialRed, needle;
        juce::Colour aluminium, tapeWell, neon;
        // Corona's amp (screens/plugins.md 7.2a): its tolex, the champagne control
        // plate and its inks, the grille cloth and badge, the graphite utility
        // strip and its inks, and the cabinet page's baffle and speakers.
        juce::Colour coronaTolex, coronaPanel, coronaInk, coronaInkSoft, coronaGrille, coronaBadge;
        juce::Colour coronaStrip, coronaStripInk, coronaStripInkSoft;
        juce::Colour coronaBaffle, coronaCone, coronaSpeakerRim;
        juce::Colour coronaJewel;                  // the pilot lamp's glow
        juce::Colour coronaMarkerA, coronaMarkerB; // the two mics' markers on the baffle
        // Patina's face: the hammertone panel and the enamel plates, the ink on each,
        // the nameplate's aluminium and the windows' black bezels (flat fallbacks
        // for the renders).
        juce::Colour patinaPanel, patinaPlate, patinaInk, patinaInkSoft, patinaPanelInkSoft;
        juce::Colour patinaNameplate, patinaBezel;
        // The guitar pedals: each enclosure's paint (flat fallbacks for the renders)
        // and the ink printed on it; the tuner's display.
        juce::Colour pedalGravity, pedalGravityInk, pedalGravityInkSoft;
        juce::Colour pedalFlare, pedalFlareInk, pedalFlareInkSoft;
        juce::Colour pedalComet, pedalCometInk, pedalCometInkSoft;
        juce::Colour pedalTide, pedalTideInk, pedalTideInkSoft;
        juce::Colour pedalPolaris, pedalPolarisInk, pedalPolarisInkSoft;
        juce::Colour tunerGlass, tunerBezel, tunerSegmentOff, tunerInTune, tunerOff, tunerText;
        // Orbit's face: the black nameplate, the transport plate in shadow, the control
        // panel and where the rim light reaches it (without their renders) and the inks
        // printed on each, the heads' block, the meters' dials, the
        // counter, the keys (unlit and lit, and their labels), the reels and the
        // pilot lamp (flat fallbacks for the renders).
        juce::Colour orbitPlate, orbitPlateDark, orbitPlateInk, orbitPlateInkSoft;
        juce::Colour orbitPanel, orbitPanelRim, orbitPanelInk, orbitPanelInkSoft, orbitRule;
        juce::Colour orbitHead, orbitHeadInk;
        juce::Colour orbitDial, orbitDialInk, orbitDialRed, orbitNeedle;
        juce::Colour orbitCounter, orbitCounterWell;
        juce::Colour orbitKey, orbitKeyLit, orbitKeyInk, orbitKeyInkLit;
        juce::Colour orbitReel, orbitPack, orbitLamp;
        // Lumen's suitcase piano (screens/plugins.md 7.4h).
        juce::Colour lumenCase, lumenCaseInk, lumenCaseInkSoft;
        juce::Colour lumenPlate, lumenPlateDark, lumenPlateInk, lumenPlateInkSoft, lumenRule;
        juce::Colour lumenNameplate, lumenNameplateInk, lumenNameplateInkSoft;
        juce::Colour lumenWindow, lumenTine, lumenToneBar, lumenPickup, lumenLamp, lumenAmber;
    };

    std::string name = "Northlight Dark";
    Colours colours;
    Track track;
    Fonts fonts;
    Spacing spacing;
    Radius radius;
    Sizes sizes;
    Motion motion;
    Meter meter;
    Material material;
    Hardware hardware;
    // The High contrast variant is applied (Preferences, or a Windows contrast
    // theme). Colours already carry it; a view reads this only for what colour
    // cannot say, such as a clip square's outline.
    bool highContrast = false;

    // ----- fonts ---------------------------------------------------------------------
    enum class Face
    {
        ui,
        mono,
        legend, // printed on a hardware face; 700 and up is the bold cut
    };
    // A font of the UI or numeric face at a token size and weight (400/500/600).
    // The size is the em square (its CSS meaning). A legend's size is JUCE's
    // height (ascent plus descent) in a face's design pixels, as the rendered
    // faces were laid out.
    juce::Font font(Face face, float size, int weight) const;
    // One line of text at an em size, in whole px: the face's ascent plus descent,
    // the taller of the interface and mono faces. Size a text box with this, not
    // the size plus a guess, since a line is about 1.2 em.
    int lineHeight(float size) const;
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
    // The display serif at an em size, for a wordmark; the italic cut when asked
    // for and bundled, else the upright (and Georgia without either).
    juce::Font displayFont(float size, bool italic) const;

    // ----- loading -------------------------------------------------------------------
    // The shipped tokens (embedded; replaced by assets/tokens.json beside the
    // executable when present), then the user's hand-edited override file, then the
    // file Preferences writes (the accent), then the High contrast variant when
    // asked for. Call once before the first window; dark() returns the result.
    static void initialise(const juce::File& userOverride = {},
                           const juce::File& preferencesOverride = {},
                           bool highContrast = false);
    // Loads the colours (and the hardware inks) again from the same sources, into
    // the current theme (whose address every view holds), leaving everything else:
    // the accent and contrast apply while the app runs. The owner then repaints.
    static void
    reloadColours(const juce::File& userOverride, const juce::File& preferencesOverride, bool highContrast);
    // Turns reduce-motion on or off in the current theme; views read it as they animate.
    static void setReduceMotion(bool reduce);
    // What Windows asks for: a contrast theme is on, and Show animations is off.
    static bool systemHighContrast();
    static bool systemReduceMotion();
    // The names of the two override files, beside the settings file.
    static constexpr const char* userOverrideName = "tokens.json";
    static constexpr const char* preferencesOverrideName = "appearance-tokens.json";
    // The colour tokens Preferences overrides: an accent and its derived states.
    // Key and colour pairs, for the override file.
    static std::vector<std::pair<std::string, juce::Colour>> accentTokens(juce::Colour accent);
    // Applies the token file's highContrast variant (a partial token file) to a
    // theme and marks it. Always the shipped values, applied last, so they never
    // compound with an accent or with themselves.
    static void applyHighContrast(Theme& theme);
    // Applies a token document (the full file, or a partial override) to a theme.
    // Unknown keys are ignored; a bad value is reported through `error` and skipped.
    static bool apply(const juce::var& tokens, Theme& theme, std::string& error);
    static bool applyText(const std::string& json, Theme& theme, std::string& error);
    // The theme with every value from the built-in defaults (no file).
    static Theme defaults();
    // The current theme.
    static const Theme& dark();
    // Parses "x y blur #colour" (optionally led by "inset").
    static bool parseShadow(const juce::String& text, Shadow& out);
};

} // namespace northlight::ui
