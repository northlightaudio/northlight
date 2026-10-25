#include "ui/PluginMenu.h"

#include "plugins/Amp.h"
#include "plugins/Bloom.h"
#include "plugins/BuiltIns.h"
#include "plugins/Exciter.h"
#include "plugins/Glue.h"
#include "plugins/Orbit.h"
#include "plugins/Patina.h"
#include "plugins/Pedals.h"
#include "plugins/TapeEcho.h"
#include "plugins/Vesper.h"
#include "plugins/Widener.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <algorithm>
#include <array>
#include <map>

namespace northlight::ui
{

bool isInstrumentType(const std::string& type, const hosting::PluginRegistry* registry)
{
    if (!hosting::PluginRegistry::isHostedType(type))
        return plugins::BuiltInFactory::isInstrument(type);
    const auto* info = registry != nullptr ? registry->findByType(type) : nullptr;
    return info != nullptr && info->isInstrument;
}

namespace
{
// The built-in effects by kind.
EffectCategory builtInCategory(const std::string& type)
{
    static const std::map<std::string, EffectCategory> kinds{
        {plugins::types::compressor, EffectCategory::dynamics},
        {plugins::types::opto, EffectCategory::dynamics},
        {plugins::glue::type, EffectCategory::dynamics},
        {plugins::types::limiter, EffectCategory::dynamics},
        {plugins::types::eq, EffectCategory::eq},
        {plugins::types::reverb, EffectCategory::space},
        {plugins::types::delay, EffectCategory::space},
        {plugins::tapeecho::type, EffectCategory::space},
        {plugins::bloom::type, EffectCategory::space},
        {plugins::vesper::type, EffectCategory::space},
        {plugins::amp::type, EffectCategory::guitar},
        {plugins::gravity::type, EffectCategory::guitar},
        {plugins::flare::type, EffectCategory::guitar},
        {plugins::comet::type, EffectCategory::guitar},
        {plugins::tide::type, EffectCategory::guitar},
        {plugins::polaris::type, EffectCategory::guitar},
        {plugins::exciter::type, EffectCategory::drive},
        {plugins::patina::type, EffectCategory::drive},
        {plugins::orbit::type, EffectCategory::drive},
        {plugins::widener::type, EffectCategory::stereo},
        {plugins::types::gain, EffectCategory::stereo},
    };
    const auto it = kinds.find(type);
    return it != kinds.end() ? it->second : EffectCategory::other;
}

// What a guitar built-in is, as a quiet hint beside its name (the names say little).
juce::String builtInHint(const std::string& type)
{
    static const std::map<std::string, const char*> hints{
        {plugins::amp::type, "Amp"},
        {plugins::gravity::type, "Compressor"},
        {plugins::flare::type, "Distortion"},
        {plugins::comet::type, "Delay"},
        {plugins::tide::type, "Chorus"},
        {plugins::polaris::type, "Tuner"},
    };
    const auto it = hints.find(type);
    return it != hints.end() ? juce::String(it->second) : juce::String();
}

// A scanned plugin's kind from the words in its category ("Fx|Dynamics",
// "audio-effect|reverb"), the first that matches in this order.
EffectCategory reportedCategory(const std::string& category)
{
    const auto text = juce::String(category).toLowerCase();
    const auto any = [&](std::initializer_list<const char*> words)
    {
        for (const auto* w : words)
            if (text.contains(w))
                return true;
        return false;
    };
    if (any({"guitar", "bass amp", "stomp", "pedal"}))
        return EffectCategory::guitar;
    if (any({"dynamics", "compressor", "limiter", "gate", "expander", "transient", "de-esser"}))
        return EffectCategory::dynamics;
    if (any({"reverb", "delay", "echo"}))
        return EffectCategory::space;
    if (any({"eq", "filter"}))
        return EffectCategory::eq;
    if (any({"modulation", "chorus", "flanger", "phaser", "tremolo", "vibrato", "rotary", "pitch"}))
        return EffectCategory::modulation;
    if (any({"distortion", "saturat", "overdrive", "amp", "tape", "lo-fi", "exciter"}))
        return EffectCategory::drive;
    if (any({"spatial",
             "stereo",
             "surround",
             "tools",
             "utility",
             "analyzer",
             "analyser",
             "mastering",
             "restoration"}))
        return EffectCategory::stereo;
    return EffectCategory::other;
}

Icon iconFor(EffectCategory category)
{
    switch (category)
    {
        case EffectCategory::dynamics:
            return Icon::fxDynamics;
        case EffectCategory::eq:
            return Icon::fxEq;
        case EffectCategory::space:
            return Icon::fxSpace;
        case EffectCategory::modulation:
            return Icon::fxModulation;
        case EffectCategory::drive:
            return Icon::fxDrive;
        case EffectCategory::guitar:
            return Icon::fxGuitar;
        case EffectCategory::stereo:
            return Icon::fxStereo;
        default:
            return Icon::plugin;
    }
}

// An icon for a menu row, drawn as the app's icons are (a 1.5 px stroke on the
// 16 px grid, the grid's padding kept so every icon sits the same size).
std::unique_ptr<juce::Drawable> menuIcon(Icon icon)
{
    bool filled = false;
    auto path = Icons::path(icon, filled);
    path.startNewSubPath(0.0f, 0.0f); // the grid's corners, so the padding scales with it
    path.startNewSubPath(16.0f, 16.0f);
    auto d = std::make_unique<juce::DrawablePath>();
    d->setPath(path);
    const auto colour = Theme::dark().colours.textSecondary;
    if (filled)
        d->setFill(colour);
    else
    {
        d->setFill(juce::Colours::transparentBlack);
        d->setStrokeFill(colour);
        d->setStrokeType(
            juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    return d;
}
} // namespace

EffectCategory effectCategory(const std::string& type, const hosting::PluginRegistry* registry)
{
    if (!hosting::PluginRegistry::isHostedType(type))
        return builtInCategory(type);
    const auto* info = registry != nullptr ? registry->findByType(type) : nullptr;
    return info != nullptr ? reportedCategory(info->category) : EffectCategory::other;
}

const char* effectCategoryName(EffectCategory category)
{
    switch (category)
    {
        case EffectCategory::dynamics:
            return "Dynamics";
        case EffectCategory::eq:
            return "EQ & Filter";
        case EffectCategory::space:
            return "Reverb & Delay";
        case EffectCategory::modulation:
            return "Modulation";
        case EffectCategory::drive:
            return "Drive & Colour";
        case EffectCategory::guitar:
            return "Guitar";
        case EffectCategory::stereo:
            return "Stereo & Utility";
        default:
            return "Other";
    }
}

PluginMenu buildPluginMenu(PluginKind kind,
                           const hosting::PluginRegistry* registry,
                           const std::function<std::vector<std::string>(const std::string&)>& listSetting,
                           int firstItem)
{
    PluginMenu result;
    result.firstItem = firstItem;
    auto& menu = result.menu;
    const bool instruments = kind == PluginKind::instrument;
    const auto builtIns = plugins::BuiltInFactory().typeNames(); // a list, not an instance per check
    const auto wanted = [&](const std::string& type)
    {
        if (!hosting::PluginRegistry::isHostedType(type))
            return std::find(builtIns.begin(), builtIns.end(), type) != builtIns.end() &&
                   plugins::BuiltInFactory::isInstrument(type) == instruments;
        const auto* info = registry != nullptr ? registry->findByType(type) : nullptr;
        return info != nullptr && info->isInstrument == instruments;
    };
    const auto nameOf = [&](const std::string& type)
    {
        if (!hosting::PluginRegistry::isHostedType(type))
            return juce::String(plugins::BuiltInFactory::displayName(type));
        const auto* info = registry != nullptr ? registry->findByType(type) : nullptr;
        return info != nullptr ? juce::String::fromUTF8(info->name.c_str()) : juce::String(type);
    };
    // An item: an effect carries its kind's icon; `maker` shows faintly at the
    // row's right (a scanned plugin's manufacturer and format).
    const auto add = [&](juce::PopupMenu& into,
                         const std::string& type,
                         const juce::String& text,
                         const juce::String& maker = {})
    {
        // The maker after a tab: the menu draws it as a quiet hint at the right.
        juce::PopupMenu::Item item(maker.isEmpty() ? text : text + "\t" + maker);
        item.itemID = result.nextItem();
        if (!instruments)
            item.image = menuIcon(iconFor(effectCategory(type, registry)));
        into.addItem(std::move(item));
        result.types.push_back(type);
    };
    const auto makerOf = [&](const hosting::PluginInfo& plugin)
    { return juce::String::fromUTF8(plugin.manufacturer.c_str()) + "  " + juce::String(plugin.format); };
    const auto section = [&](const char* title, const std::vector<std::string>& list)
    {
        bool any = false;
        for (const auto& type : list)
        {
            if (!wanted(type))
                continue;
            if (!any)
                menu.addSectionHeader(title);
            any = true;
            add(menu, type, nameOf(type));
        }
    };
    if (listSetting)
        section("Recent", listSetting("plugins.recent"));
    if (instruments)
    {
        menu.addSectionHeader("Built-in");
        for (const auto& type : builtIns)
            if (plugins::BuiltInFactory::isInstrument(type))
                add(menu, type, plugins::BuiltInFactory::displayName(type));
    }
    else
    {
        // A submenu per kind: the built-ins, then the scanned plugins of that kind.
        std::array<juce::PopupMenu, effectCategoryCount> kinds;
        for (const auto& type : builtIns)
            if (!plugins::BuiltInFactory::isInstrument(type))
                add(kinds[static_cast<size_t>(builtInCategory(type))],
                    type,
                    plugins::BuiltInFactory::displayName(type),
                    builtInHint(type));
        if (registry != nullptr)
        {
            std::array<bool, effectCategoryCount> divided{};
            for (const auto& plugin : registry->plugins())
            {
                if (plugin.isInstrument)
                    continue;
                const auto at = static_cast<size_t>(reportedCategory(plugin.category));
                if (!divided[at] && kinds[at].getNumItems() > 0)
                    kinds[at].addSeparator();
                divided[at] = true;
                add(kinds[at],
                    hosting::PluginRegistry::typeFor(plugin),
                    juce::String::fromUTF8(plugin.name.c_str()),
                    makerOf(plugin));
            }
        }
        menu.addSectionHeader("Effects");
        for (int k = 0; k < effectCategoryCount; ++k)
        {
            auto& sub = kinds[static_cast<size_t>(k)];
            if (sub.getNumItems() == 0)
                continue;
            const auto category = static_cast<EffectCategory>(k);
            menu.addSubMenu(effectCategoryName(category), sub, true, menuIcon(iconFor(category)));
        }
    }
    if (listSetting)
        section("Favourites", listSetting("plugins.favourites"));
    if (registry != nullptr)
    {
        std::map<std::string, juce::PopupMenu> byManufacturer;
        for (const auto& plugin : registry->plugins())
            if (plugin.isInstrument == instruments)
                add(byManufacturer[plugin.manufacturer.empty() ? "Other" : plugin.manufacturer],
                    hosting::PluginRegistry::typeFor(plugin),
                    juce::String::fromUTF8(plugin.name.c_str()),
                    juce::String(plugin.format));
        if (!byManufacturer.empty())
        {
            menu.addSeparator();
            if (instruments)
                for (auto& [manufacturer, sub] : byManufacturer)
                    menu.addSubMenu(juce::String::fromUTF8(manufacturer.c_str()), sub);
            else
            {
                juce::PopupMenu makers;
                for (auto& [manufacturer, sub] : byManufacturer)
                    makers.addSubMenu(juce::String::fromUTF8(manufacturer.c_str()), sub);
                menu.addSubMenu("By maker", makers, true, menuIcon(Icon::plugin));
            }
        }
    }
    return result;
}

PluginMenu buildPluginMenu(PluginKind kind, const PluginSources* sources, int firstItem)
{
    return sources != nullptr ? buildPluginMenu(kind, sources->registry, sources->listSetting, firstItem)
                              : buildPluginMenu(kind, nullptr, {}, firstItem);
}

} // namespace northlight::ui
