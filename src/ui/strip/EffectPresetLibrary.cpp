#include "ui/strip/EffectPresetLibrary.h"

#include "core/ProjectJson.h"
#include "plugins/BuiltIns.h"
#include "plugins/EffectPresets.h"

#include <algorithm>
#include <cmath>

namespace northlight::ui::strip
{

EffectPresetLibrary& EffectPresetLibrary::instance()
{
    static EffectPresetLibrary library;
    return library;
}

EffectPresetLibrary::EffectPresetLibrary()
    : root_(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile("Northlight")
                .getChildFile("Effect Presets"))
{
}

void EffectPresetLibrary::setRoot(const juce::File& root)
{
    root_ = root;
    cache_.clear();
    sendChangeMessage();
}

juce::File EffectPresetLibrary::folderFor(const std::string& type) const
{
    const auto name = juce::String(plugins::BuiltInFactory::displayName(type));
    return root_.getChildFile(juce::File::createLegalFileName(name.isNotEmpty() ? name : juce::String(type)));
}

const std::vector<PresetEntry>& EffectPresetLibrary::presets(const std::string& type)
{
    auto it = cache_.find(type);
    if (it == cache_.end())
        it = cache_.emplace(type, load(type)).first;
    return it->second;
}

void EffectPresetLibrary::reload(const std::string& type)
{
    auto fresh = load(type);
    auto& cached = cache_[type];
    const bool changed =
        fresh.size() != cached.size() ||
        !std::equal(fresh.begin(),
                    fresh.end(),
                    cached.begin(),
                    [](const auto& a, const auto& b) { return a.name == b.name && a.values == b.values; });
    cached = std::move(fresh);
    if (changed)
        sendChangeMessage();
}

int EffectPresetLibrary::match(const std::string& type, const std::vector<float>& params)
{
    const auto& list = presets(type);
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& values = list[i].values;
        if (values.size() != params.size())
            continue;
        bool same = true;
        for (size_t v = 0; same && v < values.size(); ++v)
            same = std::abs(values[v] - params[v]) <= 1.0e-4f * std::max(1.0f, std::abs(values[v]));
        if (same)
            return static_cast<int>(i);
    }
    return -1;
}

std::vector<PresetEntry> EffectPresetLibrary::load(const std::string& type) const
{
    std::vector<PresetEntry> list;
    for (const auto& p : plugins::factoryPresets(type))
        list.push_back({juce::String(p.name), p.values, false, {}});

    const auto& infos = plugins::parameterInfo(type);
    std::vector<PresetEntry> yours;
    for (const auto& file : folderFor(type).findChildFiles(juce::File::findFiles, false, "*.json"))
    {
        const auto json = juce::JSON::parse(file.loadFileAsString());
        const auto savedType = json.getProperty("type", {}).toString().toStdString();
        const auto* params = json.getProperty("params", {}).getDynamicObject();
        if (savedType != type || params == nullptr)
            continue;
        PresetEntry entry;
        entry.name = json.getProperty("name", file.getFileNameWithoutExtension()).toString();
        entry.user = true;
        entry.file = file;
        for (const auto& info : infos)
        {
            const auto key = juce::Identifier(juce::String(info.name));
            const auto v = params->getProperty(key);
            const float value = v.isDouble() || v.isInt() || v.isInt64()
                                    ? static_cast<float>(static_cast<double>(v))
                                    : info.defaultValue;
            entry.values.push_back(std::clamp(value, info.minimum, info.maximum));
        }
        yours.push_back(std::move(entry));
    }
    std::sort(yours.begin(),
              yours.end(),
              [](const auto& a, const auto& b) { return a.name.compareNatural(b.name) < 0; });
    for (auto& entry : yours)
        list.push_back(std::move(entry));
    return list;
}

std::string
EffectPresetLibrary::save(const std::string& type, const juce::String& name, const std::vector<float>& params)
{
    const auto clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty())
        return "a preset needs a name";
    const auto folder = folderFor(type);
    if (!folder.createDirectory().wasOk())
        return "could not create " + folder.getFullPathName().toStdString();
    const auto& infos = plugins::parameterInfo(type);
    auto values = std::make_unique<juce::DynamicObject>();
    for (size_t i = 0; i < infos.size() && i < params.size(); ++i)
        values->setProperty(juce::Identifier(juce::String(infos[i].name)), static_cast<double>(params[i]));
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("name", name.trim());
    root->setProperty("type", juce::String(type));
    root->setProperty("params", juce::var(values.release()));
    const auto file = folder.getChildFile(clean + ".json");
    if (!file.replaceWithText(juce::JSON::toString(juce::var(root.release()))))
        return "could not write " + file.getFullPathName().toStdString();
    reload(type);
    return {};
}

bool EffectPresetLibrary::remove(const std::string& type, const PresetEntry& entry)
{
    if (!entry.user || !entry.file.existsAsFile())
        return false;
    const bool gone = entry.file.moveToTrash() || entry.file.deleteFile();
    reload(type);
    return gone;
}

} // namespace northlight::ui::strip
