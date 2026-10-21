#include "ui/strip/UserStripPresets.h"

#include "core/ProjectJson.h"
#include "plugins/ChannelStrip.h"

namespace northlight::ui::strip
{

juce::File userPresetFolder()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Northlight")
        .getChildFile("Strip Presets");
}

std::vector<plugins::strip::Preset> loadUserPresets(const juce::File& folder)
{
    std::vector<plugins::strip::Preset> list;
    for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.json"))
    {
        const auto json = juce::JSON::parse(file.loadFileAsString());
        const auto* modules = json.getProperty("modules", {}).getArray();
        if (modules == nullptr)
            continue;
        plugins::strip::Preset preset;
        preset.name = json.getProperty("name", file.getFileNameWithoutExtension()).toString().toStdString();
        preset.category = "Yours";
        for (const auto& m : *modules)
        {
            core::Insert module;
            module.type = core::currentTypeName(m.getProperty("type", {}).toString().toStdString());
            if (!plugins::strip::isStripType(module.type))
                continue;
            module.bypass = !static_cast<bool>(m.getProperty("on", false));
            if (const auto* params = m.getProperty("params", {}).getArray())
                for (const auto& v : *params)
                    module.params.push_back(static_cast<float>(static_cast<double>(v)));
            preset.modules.push_back(std::move(module));
        }
        if (!preset.modules.empty())
            list.push_back(std::move(preset));
    }
    std::sort(list.begin(),
              list.end(),
              [](const auto& a, const auto& b)
              { return juce::String(a.name).compareNatural(juce::String(b.name)) < 0; });
    return list;
}

std::string
saveUserPreset(const juce::File& folder, const juce::String& name, const std::vector<core::Insert>& modules)
{
    const auto clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty())
        return "a preset needs a name";
    if (!folder.createDirectory().wasOk())
        return "could not create " + folder.getFullPathName().toStdString();
    juce::Array<juce::var> list;
    for (const auto& m : modules)
    {
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty("type", juce::String(m.type));
        object->setProperty("on", !m.bypass);
        juce::Array<juce::var> params;
        for (const float v : m.params)
            params.add(static_cast<double>(v));
        object->setProperty("params", params);
        list.add(juce::var(object.release()));
    }
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("name", name.trim());
    root->setProperty("modules", list);
    const auto file = folder.getChildFile(clean + ".json");
    if (!file.replaceWithText(juce::JSON::toString(juce::var(root.release()))))
        return "could not write " + file.getFullPathName().toStdString();
    return {};
}

} // namespace northlight::ui::strip
