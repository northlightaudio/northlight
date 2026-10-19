#include "hosting/HostFactory.h"

#include "engine/rt/RealtimeGuard.h"
#include "hosting/HostedPlugin.h"

namespace aural::hosting
{

HostFactory::HostFactory(const PluginRegistry& registry) : registry_(registry)
{
    formats_.addFormat(std::make_unique<juce::VST3PluginFormat>());
}

std::vector<std::string> HostFactory::typeNames() const
{
    return builtIns_.typeNames();
}

std::string HostFactory::displayName(const std::string& typeName) const
{
    if (const auto* info = registry_.findByType(typeName))
        return info->name;
    if (PluginRegistry::isHostedType(typeName))
        return "Missing plugin";
    return plugins::BuiltInFactory::displayName(typeName);
}

std::unique_ptr<engine::InsertProcessor> HostFactory::create(const std::string& typeName) const
{
    AURAL_ASSERT_NOT_REALTIME("HostFactory::create");
    lastError_.clear();
    if (!PluginRegistry::isHostedType(typeName))
        return builtIns_.create(typeName);
    const auto* info = registry_.findByType(typeName);
    if (info == nullptr)
    {
        lastError_ = "plugin not in the registry (rescan?)";
        return nullptr;
    }
    juce::PluginDescription description;
    description.name = juce::String::fromUTF8(info->name.c_str());
    description.manufacturerName = juce::String::fromUTF8(info->manufacturer.c_str());
    description.category = juce::String::fromUTF8(info->category.c_str());
    description.version = juce::String::fromUTF8(info->version.c_str());
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = juce::String::fromUTF8(info->path.c_str());
    description.uniqueId = info->uniqueId;
    description.deprecatedUid = info->deprecatedUid;
    description.isInstrument = info->isInstrument;
    description.numInputChannels = info->numInputs;
    description.numOutputChannels = info->numOutputs;

    // The real rate and block size come with prepare(); these are placeholders the
    // format needs to construct the instance.
    juce::String error;
    auto instance = formats_.createPluginInstance(description, 48000.0, 512, error);
    if (instance == nullptr)
    {
        lastError_ = error.toStdString();
        return nullptr;
    }
    return std::make_unique<HostedPlugin>(typeName, std::move(instance));
}

} // namespace aural::hosting
