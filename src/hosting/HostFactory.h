#pragma once

#include "engine/Insert.h"
#include "hosting/PluginRegistry.h"
#include "plugins/BuiltIns.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
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
    // Errors by type for plugins that failed to load; a failed type is not retried
    // until clearFailures() (after a rescan), so a recompile does not reload DLLs.
    const std::map<std::string, std::string>& failures() const noexcept { return failures_; }
    void clearFailures() { failures_.clear(); }
    // How the built-in Sampler decodes its files (the app's media loader).
    void setSampleLoader(plugins::sampler::Loader loader) { builtIns_.setSampleLoader(std::move(loader)); }

private:
    const PluginRegistry& registry_;
    plugins::BuiltInFactory builtIns_;
    juce::AudioPluginFormatManager formats_;
    mutable std::string lastError_;
    mutable std::map<std::string, std::string> failures_;
};

} // namespace aural::hosting
