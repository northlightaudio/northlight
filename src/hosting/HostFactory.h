#pragma once

#include "engine/Insert.h"
#include "hosting/PluginRegistry.h"
#include "plugins/BuiltIns.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <string>
#include <vector>

namespace aural::hosting
{

// The app's insert factory: the built-ins by their type names, and every plugin
// in the registry as "vst3:<id>". Creating a hosted plugin loads it in this
// process on the calling (message) thread; a load failure yields nullptr and the
// chain simply skips that slot. The registry is owned by whoever owns the factory
// and may be rescanned at any time.
class HostFactory final : public engine::InsertFactory
{
public:
    explicit HostFactory(const PluginRegistry& registry);

    std::vector<std::string> typeNames() const override; // built-ins only (plugins come from the registry)
    std::unique_ptr<engine::InsertProcessor> create(const std::string& typeName) const override;

    const PluginRegistry& registry() const noexcept { return registry_; }
    // Human-readable name for any insert type (built-in or hosted).
    std::string displayName(const std::string& typeName) const;
    // The last error from create(), for the UI.
    const std::string& lastError() const noexcept { return lastError_; }

private:
    const PluginRegistry& registry_;
    plugins::BuiltInFactory builtIns_;
    juce::AudioPluginFormatManager formats_;
    mutable std::string lastError_;
};

} // namespace aural::hosting
