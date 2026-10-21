#include "ui/Theme.h"

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <cmath>
#include <map>
#include <memory>

namespace aural::ui
{

namespace
{
// The shipped tokens, embedded at build time from docs/design/tokens.json.
const char* const embeddedTokens =
#include "TokensEmbedded.inc"
    ;

Theme current;
std::string currentSource = "embedded";
std::vector<std::unique_ptr<juce::MemoryBlock>> fontData; // keeps bundled typefaces' bytes alive

// "#RRGGBB" or "#RRGGBBAA".
bool parseColour(const juce::var& v, juce::Colour& out)
{
    if (!v.isString())
        return false;
    const auto text = v.toString().trim();
    if (!text.startsWithChar('#') || (text.length() != 7 && text.length() != 9))
        return false;
    const auto hex = text.substring(1);
    const auto value = static_cast<std::uint32_t>(hex.getHexValue64());
    if (hex.length() == 6)
        out = juce::Colour(0xff000000u | value);
    else
        out = juce::Colour((value >> 8) | ((value & 0xffu) << 24)); // RRGGBBAA -> AARRGGBB
    return true;
}

bool reduceMotionRequested()
{
#if JUCE_WINDOWS
    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
        return animations == FALSE;
#endif
    return false;
}

template <typename T> void readNumber(const juce::var& object, const char* key, T& out, std::string& error)
{
    if (!object.isObject() || !object.hasProperty(key))
        return;
    const auto& v = object[key];
    if (v.isDouble() || v.isInt() || v.isInt64())
        out = static_cast<T>(static_cast<double>(v));
    else
        error += std::string("token ") + key + " is not a number; ";
}

void readString(const juce::var& object, const char* key, std::string& out)
{
    if (object.isObject() && object.hasProperty(key) && object[key].isString())
        out = object[key].toString().toStdString();
}

juce::File executableAssets()
{
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getChildFile("assets");
}

juce::Typeface::Ptr loadTypeface(const juce::File& file)
{
    if (!file.existsAsFile())
        return nullptr;
    auto block = std::make_unique<juce::MemoryBlock>();
    if (!file.loadFileAsData(*block) || block->getSize() == 0)
        return nullptr;
    auto typeface = juce::Typeface::createSystemTypefaceFor(block->getData(), block->getSize());
    if (typeface != nullptr)
        fontData.push_back(std::move(block));
    return typeface;
}
} // namespace

// ----- derived values ------------------------------------------------------------------------

juce::Colour Theme::Track::forTrack(std::uint32_t argb, int slot) const
{
    if (argb != 0)
        return juce::Colour(argb);
    if (palette.empty())
        return juce::Colours::grey;
    // New tracks cycle through the first eleven; the twelfth (Slate) is for buses.
    const auto cycle = std::max<size_t>(1, palette.size() > 1 ? palette.size() - 1 : palette.size());
    return palette[static_cast<size_t>(std::max(0, slot)) % cycle].value;
}

juce::Colour Theme::Track::forBus(std::uint32_t argb) const
{
    if (argb != 0)
        return juce::Colour(argb);
    for (const auto& entry : palette)
        if (entry.name == defaultBus)
            return entry.value;
    return palette.empty() ? juce::Colours::grey : palette.back().value;
}

juce::Colour Theme::Meter::colourAt(float db) const
{
    for (const auto& zone : zones)
        if (db < zone.toDb)
            return zone.colour;
    return zones.empty() ? juce::Colours::white : zones.back().colour;
}

juce::Font Theme::font(Face face, float size, int weight) const
{
    const auto& typeface = face == Face::mono ? fonts.mono : fonts.ui;
    juce::FontOptions options;
    if (typeface != nullptr)
        options = juce::FontOptions(typeface);
    else
        options = juce::FontOptions(face == Face::mono ? "Consolas" : "Segoe UI", size, juce::Font::plain);
    options = options.withHeight(size);
    if (weight >= fonts.weight.semibold)
        options = options.withStyle("Bold");
    return juce::Font(options);
}

juce::Font Theme::captionCaps() const
{
    auto f = font(Face::ui, fonts.size.caption, fonts.weight.semibold);
    f.setExtraKerningFactor(fonts.letterSpacingCaps);
    return f;
}

// ----- loading -------------------------------------------------------------------------------

bool Theme::parseShadow(const juce::String& text, Shadow& out)
{
    auto parts = juce::StringArray::fromTokens(text.trim(), " ", "");
    parts.removeEmptyStrings();
    Shadow shadow;
    if (!parts.isEmpty() && parts[0] == "inset")
    {
        shadow.inset = true;
        parts.remove(0);
    }
    if (parts.size() != 4 || !parseColour(juce::var(parts[3]), shadow.colour))
        return false;
    shadow.x = parts[0].getFloatValue();
    shadow.y = parts[1].getFloatValue();
    shadow.blur = parts[2].getFloatValue();
    out = shadow;
    return true;
}

bool Theme::apply(const juce::var& tokens, Theme& theme, std::string& error)
{
    if (!tokens.isObject() || tokens.getDynamicObject() == nullptr)
    {
        error = "the token file is not a JSON object";
        return false;
    }
    error.clear();
    if (const auto& meta = tokens["meta"]; meta.isObject())
        readString(meta, "name", theme.name);

    // Colours: every key of the colour block onto the matching field.
    if (const auto& colours = tokens["color"]; colours.isObject())
    {
        auto& c = theme.colours;
        const std::map<std::string, juce::Colour*> fields = {
            {"canvas", &c.canvas},
            {"surface0", &c.surface0},
            {"surface1", &c.surface1},
            {"surface2", &c.surface2},
            {"surface3", &c.surface3},
            {"surfacePressed", &c.surfacePressed},
            {"overlay", &c.overlay},
            {"lineSubtle", &c.lineSubtle},
            {"lineStrong", &c.lineStrong},
            {"lineFocus", &c.lineFocus},
            {"textPrimary", &c.textPrimary},
            {"textSecondary", &c.textSecondary},
            {"textTertiary", &c.textTertiary},
            {"textDisabled", &c.textDisabled},
            {"textOnAccent", &c.textOnAccent},
            {"textOnColour", &c.textOnColour},
            {"accent", &c.accent},
            {"accentHover", &c.accentHover},
            {"accentPressed", &c.accentPressed},
            {"accentSubtle", &c.accentSubtle},
            {"selectionFill", &c.selectionFill},
            {"selectionOutline", &c.selectionOutline},
            {"record", &c.record},
            {"recordHover", &c.recordHover},
            {"recordSubtle", &c.recordSubtle},
            {"danger", &c.danger},
            {"warning", &c.warning},
            {"success", &c.success},
            {"info", &c.info},
            {"mute", &c.mute},
            {"solo", &c.solo},
            {"arm", &c.arm},
            {"monitor", &c.monitor},
            {"meterLow", &c.meterLow},
            {"meterMid", &c.meterMid},
            {"meterHot", &c.meterHot},
            {"meterClip", &c.meterClip},
            {"meterTrack", &c.meterTrack},
            {"meterPeakHold", &c.meterPeakHold},
            {"loudnessTarget", &c.loudnessTarget},
            {"playhead", &c.playhead},
            {"playheadRecording", &c.playheadRecording},
            {"gridBar", &c.gridBar},
            {"gridBeat", &c.gridBeat},
            {"gridSub", &c.gridSub},
            {"rulerText", &c.rulerText},
            {"loopFill", &c.loopFill},
            {"loopEdge", &c.loopEdge},
            {"punchFill", &c.punchFill},
            {"punchEdge", &c.punchEdge},
            {"markerFlag", &c.markerFlag},
            {"regionFill", &c.regionFill},
            {"laneAlt", &c.laneAlt},
            {"laneHover", &c.laneHover},
            {"laneSelected", &c.laneSelected},
            {"dropTarget", &c.dropTarget},
            {"snapGuide", &c.snapGuide},
            {"automation", &c.automation},
            {"midiNote", &c.midiNote},
            {"midiNoteVelocityLow", &c.midiNoteVelocityLow},
        };
        if (const auto* object = colours.getDynamicObject())
            for (const auto& property : object->getProperties())
            {
                const auto it = fields.find(property.name.toString().toStdString());
                if (it == fields.end())
                    continue;
                if (!parseColour(property.value, *it->second))
                    error += "colour " + it->first + " is not #RRGGBB or #RRGGBBAA; ";
            }
        // The pre-token names follow their tokens.
        c.background = c.canvas;
        c.surface = c.surface0;
        c.surfaceRaised = c.surface2;
        c.surfaceHover = c.surface3;
        c.border = c.lineSubtle;
        c.borderStrong = c.lineStrong;
        c.accentText = c.textOnAccent;
        c.focus = c.lineFocus;
        c.meter = c.meterLow;
    }

    if (const auto& track = tokens["track"]; track.isObject())
    {
        auto& t = theme.track;
        if (const auto* palette = track["palette"].getArray())
        {
            std::vector<TrackColour> entries;
            for (const auto& item : *palette)
            {
                TrackColour entry;
                readString(item, "name", entry.name);
                if (!parseColour(item["value"], entry.value))
                {
                    error += "track palette entry " + entry.name + " has no colour; ";
                    continue;
                }
                entries.push_back(std::move(entry));
            }
            if (!entries.empty())
                t.palette = std::move(entries);
        }
        readString(track, "defaultBus", t.defaultBus);
        readNumber(track, "clipBodyAlpha", t.clipBodyAlpha, error);
        readNumber(track, "clipBodySelectedAlpha", t.clipBodySelectedAlpha, error);
        readNumber(track, "clipHeaderAlpha", t.clipHeaderAlpha, error);
        readNumber(track, "waveformAlpha", t.waveformAlpha, error);
        readNumber(track, "waveformMutedAlpha", t.waveformMutedAlpha, error);
        readNumber(track, "headerStripeWidth", t.headerStripeWidth, error);
    }

    if (const auto& font = tokens["font"]; font.isObject())
    {
        auto& f = theme.fonts;
        if (const auto& ui = font["ui"]; ui.isObject())
        {
            readString(ui, "family", f.uiFamily);
            readString(ui, "file", f.uiFile);
        }
        if (const auto& mono = font["mono"]; mono.isObject())
        {
            readString(mono, "family", f.monoFamily);
            readString(mono, "file", f.monoFile);
        }
        if (const auto& size = font["size"]; size.isObject())
        {
            readNumber(size, "caption", f.size.caption, error);
            readNumber(size, "small", f.size.small, error);
            readNumber(size, "body", f.size.body, error);
            readNumber(size, "label", f.size.label, error);
            readNumber(size, "title", f.size.title, error);
            readNumber(size, "heading", f.size.heading, error);
            readNumber(size, "timeLarge", f.size.timeLarge, error);
            readNumber(size, "timeSmall", f.size.timeSmall, error);
            readNumber(size, "value", f.size.value, error);
        }
        if (const auto& weight = font["weight"]; weight.isObject())
        {
            readNumber(weight, "regular", f.weight.regular, error);
            readNumber(weight, "medium", f.weight.medium, error);
            readNumber(weight, "semibold", f.weight.semibold, error);
        }
        readNumber(font, "lineHeight", f.lineHeight, error);
        readNumber(font, "letterSpacingCaps", f.letterSpacingCaps, error);
        theme.type.caption = f.size.caption;
        theme.type.small = f.size.small;
        theme.type.body = f.size.body;
        theme.type.label = f.size.label;
        theme.type.title = f.size.title;
        theme.type.heading = f.size.heading;
        theme.type.timeLarge = f.size.timeLarge;
        theme.type.timeSmall = f.size.timeSmall;
        theme.type.value = f.size.value;
    }

    if (const auto& space = tokens["space"]; space.isObject())
    {
        auto& s = theme.spacing;
        readNumber(space, "xxs", s.xxs, error);
        readNumber(space, "xs", s.xs, error);
        readNumber(space, "sm", s.sm, error);
        readNumber(space, "md", s.md, error);
        readNumber(space, "lg", s.lg, error);
        readNumber(space, "xl", s.xl, error);
        readNumber(space, "xxl", s.xxl, error);
        readNumber(space, "xxxl", s.xxxl, error);
    }
    if (const auto& radius = tokens["radius"]; radius.isObject())
    {
        auto& r = theme.radius;
        readNumber(radius, "sm", r.sm, error);
        readNumber(radius, "md", r.md, error);
        readNumber(radius, "lg", r.lg, error);
        readNumber(radius, "pill", r.pill, error);
    }
    if (const auto& size = tokens["size"]; size.isObject())
    {
        auto& s = theme.sizes;
        readNumber(size, "controlCompact", s.controlCompact, error);
        readNumber(size, "control", s.control, error);
        readNumber(size, "controlLarge", s.controlLarge, error);
        readNumber(size, "iconSmall", s.iconSmall, error);
        readNumber(size, "icon", s.icon, error);
        readNumber(size, "iconLarge", s.iconLarge, error);
        readNumber(size, "headerBar", s.headerBar, error);
        readNumber(size, "statusBar", s.statusBar, error);
        readNumber(size, "ruler", s.ruler, error);
        readNumber(size, "trackHeaderWidth", s.trackHeaderWidth, error);
        readNumber(size, "trackHeaderMinWidth", s.trackHeaderMinWidth, error);
        readNumber(size, "trackHeaderMaxWidth", s.trackHeaderMaxWidth, error);
        readNumber(size, "trackRowMin", s.trackRowMin, error);
        readNumber(size, "trackRow", s.trackRow, error);
        readNumber(size, "trackRowLarge", s.trackRowLarge, error);
        readNumber(size, "clipHeader", s.clipHeader, error);
        readNumber(size, "takeLane", s.takeLane, error);
        readNumber(size, "mixerStripWidth", s.mixerStripWidth, error);
        readNumber(size, "mixerStripWideWidth", s.mixerStripWideWidth, error);
        readNumber(size, "faderTravel", s.faderTravel, error);
        if (const auto* cap = size["faderCap"].getArray(); cap != nullptr && cap->size() == 2)
        {
            s.faderCapWidth = static_cast<int>(static_cast<double>((*cap)[0]));
            s.faderCapHeight = static_cast<int>(static_cast<double>((*cap)[1]));
        }
        readNumber(size, "knob", s.knob, error);
        readNumber(size, "meterWidth", s.meterWidth, error);
        readNumber(size, "meterGap", s.meterGap, error);
        readNumber(size, "scrollbar", s.scrollbar, error);
        readNumber(size, "splitter", s.splitter, error);
        readNumber(size, "panelMin", s.panelMin, error);
        readNumber(size, "dockMin", s.dockMin, error);
        readNumber(size, "focusRing", s.focusRing, error);
        readNumber(size, "tooltipMaxWidth", s.tooltipMaxWidth, error);
        s.controlHeight = s.control;
        s.meterHeight = s.meterWidth;
    }
    if (const auto& motion = tokens["motion"]; motion.isObject())
    {
        auto& m = theme.motion;
        readNumber(motion, "hoverMs", m.hoverMs, error);
        readNumber(motion, "pressMs", m.pressMs, error);
        readNumber(motion, "toggleMs", m.toggleMs, error);
        readNumber(motion, "panelMs", m.panelMs, error);
        readNumber(motion, "menuMs", m.menuMs, error);
        readNumber(motion, "toastMs", m.toastMs, error);
        readNumber(motion, "tooltipDelayMs", m.tooltipDelayMs, error);
        readNumber(motion, "meterRiseMs", m.meterRiseMs, error);
        readNumber(motion, "meterFallDbPerSec", m.meterFallDbPerSec, error);
        readNumber(motion, "peakHoldMs", m.peakHoldMs, error);
    }
    if (const auto& meter = tokens["meter"]; meter.isObject())
    {
        auto& m = theme.meter;
        readNumber(meter, "floorDb", m.floorDb, error);
        if (const auto* zones = meter["zones"].getArray())
        {
            std::vector<MeterZone> parsed;
            for (const auto& item : *zones)
            {
                MeterZone zone;
                readNumber(item, "fromDb", zone.fromDb, error);
                readNumber(item, "toDb", zone.toDb, error);
                std::string colourName;
                readString(item, "color", colourName);
                const auto& c = theme.colours;
                zone.colour = colourName == "meterMid"    ? c.meterMid
                              : colourName == "meterHot"  ? c.meterHot
                              : colourName == "meterClip" ? c.meterClip
                                                          : c.meterLow;
                parsed.push_back(zone);
            }
            if (!parsed.empty())
                m.zones = std::move(parsed);
        }
        readNumber(meter, "clipHoldMs", m.clipHoldMs, error);
        if (const auto* ticks = meter["scaleTicksDb"].getArray())
        {
            m.scaleTicksDb.clear();
            for (const auto& t : *ticks)
                m.scaleTicksDb.push_back(static_cast<float>(static_cast<double>(t)));
        }
        readNumber(meter, "loudnessTargetLufs", m.loudnessTargetLufs, error);
        if (const auto* range = meter["loudnessRangeLufs"].getArray(); range != nullptr && range->size() == 2)
        {
            m.loudnessRangeLow = static_cast<float>(static_cast<double>((*range)[0]));
            m.loudnessRangeHigh = static_cast<float>(static_cast<double>((*range)[1]));
        }
    }
    if (const auto& material = tokens["material"]; material.isObject())
    {
        auto& m = theme.material;
        const auto colour = [&](const char* key, juce::Colour& out)
        {
            if (material.hasProperty(key) && !parseColour(material[key], out))
                error += std::string("material ") + key + " is not a colour; ";
        };
        const auto shadow = [&](const char* key, Shadow& out)
        {
            if (material.hasProperty(key) && !parseShadow(material[key].toString(), out))
                error += std::string("material ") + key + " is not a shadow; ";
        };
        colour("controlTop", m.controlTop);
        colour("controlBottom", m.controlBottom);
        colour("controlHighlight", m.controlHighlight);
        shadow("controlShadow", m.controlShadow);
        shadow("pressedInset", m.pressedInset);
        colour("capTop", m.capTop);
        colour("capBottom", m.capBottom);
        colour("capHighlight", m.capHighlight);
        shadow("capShadow", m.capShadow);
        colour("groove", m.groove);
        shadow("grooveInset", m.grooveInset);
        colour("grooveLip", m.grooveLip);
        colour("knobBodyLight", m.knobBodyLight);
        colour("knobBodyDark", m.knobBodyDark);
        colour("knobRim", m.knobRim);
        colour("knobHighlight", m.knobHighlight);
        colour("meterWell", m.meterWell);
        shadow("meterWellInset", m.meterWellInset);
        readNumber(material, "ledPitchPx", m.ledPitchPx, error);
        readNumber(material, "ledGapPx", m.ledGapPx, error);
        shadow("glowAccent", m.glowAccent);
        shadow("glowRecord", m.glowRecord);
        shadow("glowSolo", m.glowSolo);
        shadow("glowMonitor", m.glowMonitor);
        colour("readoutWell", m.readoutWell);
        colour("readoutText", m.readoutText);
        colour("readoutRecord", m.readoutRecord);
        m.enabled = !m.controlTop.isTransparent();
    }
    if (const auto& elevation = tokens["elevation"]; elevation.isObject())
        if (const auto& popup = elevation["popup"]; popup.isObject() && popup["shadow"].isString())
        {
            // "x y blur #colour"
            const auto parts = juce::StringArray::fromTokens(popup["shadow"].toString(), " ", "");
            if (parts.size() == 4)
            {
                theme.elevation.popupShadowOffset = {parts[0].getFloatValue(), parts[1].getFloatValue()};
                theme.elevation.popupShadowRadius = parts[2].getFloatValue();
                parseColour(juce::var(parts[3]), theme.elevation.popupShadow);
            }
        }
    return error.empty();
}

bool Theme::applyText(const std::string& json, Theme& theme, std::string& error)
{
    juce::var parsed;
    const auto result = juce::JSON::parse(juce::String::fromUTF8(json.c_str()), parsed);
    if (result.failed())
    {
        error = result.getErrorMessage().toStdString();
        return false;
    }
    return apply(parsed, theme, error);
}

Theme Theme::defaults()
{
    Theme theme;
    std::string error;
    applyText(embeddedTokens, theme, error);
    jassert(error.empty());
    theme.motion.reduceMotion = reduceMotionRequested();
    return theme;
}

void Theme::initialise(const juce::File& userOverride)
{
    current = defaults();
    currentSource = "embedded";
    std::string error;
    // The shipped file beside the executable (a newer token file without a rebuild).
    const auto shipped = executableAssets().getChildFile("tokens.json");
    if (shipped.existsAsFile())
    {
        applyText(shipped.loadFileAsString().toStdString(), current, error);
        currentSource = shipped.getFullPathName().toStdString();
    }
    // The user's override: only the keys it holds.
    if (userOverride.existsAsFile())
    {
        applyText(userOverride.loadFileAsString().toStdString(), current, error);
        currentSource += " + " + userOverride.getFullPathName().toStdString();
    }
    // Bundled typefaces.
    const auto fonts = executableAssets().getChildFile("fonts");
    current.fonts.ui = loadTypeface(fonts.getChildFile(juce::String(current.fonts.uiFile)));
    current.fonts.mono = loadTypeface(fonts.getChildFile(juce::String(current.fonts.monoFile)));
}

const Theme& Theme::dark()
{
    static const bool initialised = []
    {
        if (current.colours.textPrimary == juce::Colour())
            current = defaults();
        return true;
    }();
    (void)initialised;
    return current;
}

const std::string& Theme::source()
{
    return currentSource;
}

} // namespace aural::ui
