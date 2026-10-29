#pragma once

#include "hosting/PluginRegistry.h"
#include "ui/Icons.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace northlight::ui
{

// What a plugin slot takes: an insert slot takes effects, an instrument slot
// (screens/plugins.md 7.4) takes instruments, a MIDI track's MIDI FX slot
// (7.4h) takes MIDI effects.
enum class PluginKind
{
    effect,
    instrument,
    midiEffect,
};

// Whether a type is an instrument: a built-in synth, or a scanned plugin the
// registry calls one (the registry may be null).
bool isInstrumentType(const std::string& type, const hosting::PluginRegistry* registry);

// The kinds of effect the effects menu groups by (screens/plugins.md 7.1).
enum class EffectCategory
{
    dynamics,   // compressors, limiters, gates
    eq,         // EQ and filters
    space,      // reverb and delay
    modulation, // chorus, flanger, phaser, tremolo
    drive,      // saturation, distortion, colour
    guitar,     // guitar and bass: Corona and the pedals
    stereo,     // stereo and utility: width, gain, metering
    other,
};
constexpr int effectCategoryCount = static_cast<int>(EffectCategory::other) + 1;
// An effect's kind: a built-in's by what it is, a scanned plugin's from the
// category it reports (VST3 subcategories, CLAP features); other when unknown.
EffectCategory effectCategory(const std::string& type, const hosting::PluginRegistry* registry);
// The kind's icon (fx-dynamics, fx-eq, ...; plugin for other).
Icon effectCategoryIcon(EffectCategory category);
// The kind's name in the menu ("Reverb & Delay").
const char* effectCategoryName(EffectCategory category);

// A menu of the plugins of a kind: recent and favourites (when `listSetting`
// gives them), then, for effects, a submenu per kind of effect (the built-ins,
// then the scanned plugins of that kind, each with its kind's icon), and every
// scanned plugin again by manufacturer under By maker; for instruments, the
// built-ins, then the scanned instruments by manufacturer. Item `firstItem + i`
// picks types[i] (a type can appear under more than one item); ids after the
// last are the caller's.
struct PluginMenu
{
    juce::PopupMenu menu;
    std::vector<std::string> types;
    int nextItem() const noexcept { return firstItem + static_cast<int>(types.size()); }
    int firstItem = 1;
    // The type an item picked, or empty for an id outside the menu's.
    std::string typeFor(int item) const
    {
        const int i = item - firstItem;
        return i >= 0 && static_cast<size_t>(i) < types.size() ? types[static_cast<size_t>(i)]
                                                               : std::string{};
    }
};

// Where every view's plugin menus come from: the scanned plugins and the
// recent / favourite lists. The shell owns one and hands it to every view, so
// each menu offers the same plugins (a view without it offers the built-ins).
struct PluginSources
{
    const hosting::PluginRegistry* registry = nullptr;
    std::function<std::vector<std::string>(const std::string&)> listSetting;
};

PluginMenu
buildPluginMenu(PluginKind kind,
                const hosting::PluginRegistry* registry,
                const std::function<std::vector<std::string>(const std::string&)>& listSetting = {},
                int firstItem = 1);
PluginMenu buildPluginMenu(PluginKind kind, const PluginSources* sources, int firstItem = 1);

} // namespace northlight::ui
