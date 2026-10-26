#include "hosting/HostFactory.h"

#include "engine/rt/RealtimeGuard.h"
#include "hosting/ClapEditor.h"
#include "hosting/ClapPlugin.h"
#include "hosting/HostedPlugin.h"

namespace northlight::hosting
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

void HostFactory::serviceHostedPlugins(bool engineRunning)
{
    HostedClapPlugin::serviceAll(engineRunning);
}

PluginPresets* HostFactory::presetsFor(engine::InsertProcessor& live) noexcept
{
    if (auto* vst3 = dynamic_cast<HostedPlugin*>(&live))
        return vst3;
    return dynamic_cast<HostedClapPlugin*>(&live);
}

std::optional<PluginEditor> HostFactory::createEditor(engine::InsertProcessor& live)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostFactory::createEditor");
    if (auto* vst3 = dynamic_cast<HostedPlugin*>(&live))
    {
        auto& instance = vst3->instance();
        juce::AudioProcessorEditor* raw =
            instance.hasEditor() ? instance.createEditorAndMakeActive() : nullptr;
        if (raw == nullptr)
            raw = new juce::GenericAudioProcessorEditor(instance);
        PluginEditor editor;
        editor.resizable = raw->isResizable();
        editor.component.reset(raw);
        return editor;
    }
    if (auto* clap = dynamic_cast<HostedClapPlugin*>(&live))
    {
        PluginEditor editor;
        if (clap->hasEditor())
        {
            auto owned = std::make_unique<ClapEditor>(*clap);
            editor.events = &owned->events;
            editor.resizable = owned->isResizable();
            editor.component = std::move(owned);
        }
        return editor;
    }
    return std::nullopt;
}

std::unique_ptr<engine::InsertProcessor> HostFactory::create(const std::string& typeName) const
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostFactory::create");
    lastError_.clear();
    if (!PluginRegistry::isHostedType(typeName))
        return builtIns_.create(typeName);
    if (const auto failed = failures_.find(typeName); failed != failures_.end())
    {
        lastError_ = failed->second;
        return nullptr;
    }
    const auto fail = [&](std::string message)
    {
        lastError_ = message;
        failures_[typeName] = std::move(message);
        return std::unique_ptr<engine::InsertProcessor>{};
    };
    const auto* info = registry_.findByType(typeName);
    if (info == nullptr)
        return fail("plugin not in the registry (rescan?)");
    if (PluginRegistry::isClapType(typeName))
    {
        std::string error;
        auto clap = HostedClapPlugin::create(typeName, info->path, info->id, error);
        if (clap == nullptr)
            return fail(error);
        return clap;
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
        return fail(error.toStdString());
    bool sidechain = false;
    int auxOutputs = 0;
    const auto layout = HostedPlugin::negotiateLayout(*instance, sidechain, auxOutputs);
    if (layout == HostedPlugin::Layout::unsupported)
        return fail("the plugin accepts neither a stereo nor a mono layout");
    return std::make_unique<HostedPlugin>(typeName, std::move(instance), layout, sidechain, auxOutputs);
}

} // namespace northlight::hosting
