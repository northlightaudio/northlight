#pragma once

#include "hosting/PluginRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace aural::ui
{

// What a plugin slot takes: an insert slot takes effects, an instrument slot
// (07-plugins.md 7.4) takes instruments.
enum class PluginKind
{
    effect,
    instrument,
};

// Whether a type is an instrument: a built-in synth, or a scanned plugin the
// registry calls one (the registry may be null).
bool isInstrumentType(const std::string& type, const hosting::PluginRegistry* registry);

// A menu of the plugins of a kind: recent and favourites (when `listSetting`
// gives them), the built-ins, then every scanned plugin by manufacturer. Item
// `firstItem + i` picks types[i]; ids after the last are the caller's.
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

} // namespace aural::ui
