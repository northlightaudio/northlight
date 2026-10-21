#include "ui/PluginMenu.h"

#include "plugins/BuiltIns.h"

#include <algorithm>
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
    const auto add = [&](juce::PopupMenu& into, const std::string& type, const juce::String& text)
    {
        into.addItem(result.nextItem(), text);
        result.types.push_back(type);
    };
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
    menu.addSectionHeader("Built-in");
    for (const auto& type : plugins::BuiltInFactory().typeNames())
        if (plugins::BuiltInFactory::isInstrument(type) == instruments)
            add(menu, type, plugins::BuiltInFactory::displayName(type));
    if (listSetting)
        section("Favourites", listSetting("plugins.favourites"));
    if (registry != nullptr)
    {
        std::map<std::string, juce::PopupMenu> byManufacturer;
        for (const auto& plugin : registry->plugins())
            if (plugin.isInstrument == instruments)
                add(byManufacturer[plugin.manufacturer.empty() ? "Other" : plugin.manufacturer],
                    hosting::PluginRegistry::typeFor(plugin),
                    juce::String::fromUTF8(plugin.name.c_str()) + "  [" + juce::String(plugin.format) + "]");
        if (!byManufacturer.empty())
        {
            menu.addSeparator();
            for (auto& [manufacturer, sub] : byManufacturer)
                menu.addSubMenu(juce::String::fromUTF8(manufacturer.c_str()), sub);
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
