#pragma once

#include "engine/Insert.h"
#include "hosting/PluginEditor.h"
#include "hosting/PluginPresets.h"
#include "hosting/PluginRegistry.h"
#include "plugins/BuiltIns.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace northlight::hosting
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
    // Why a type failed to load (empty: it did not), and forgetting that so the
    // next compile tries it again (Retry).
    std::string failure(const std::string& typeName) const
    {
        const auto it = failures_.find(typeName);
        return it != failures_.end() ? it->second : std::string{};
    }
    void clearFailure(const std::string& typeName) { failures_.erase(typeName); }
    // ----- what the rest of the app needs from a hosted plugin, without knowing
    // its format (message thread)
    // The main-thread service hosted plugins need while they live (pending CLAP
    // callbacks, latency refreshes, GUI closures and, while the engine is not
    // running, parameter flushes): the session calls it from its poll.
    static void serviceHostedPlugins(bool engineRunning);
    // The Presets menu's view of a live hosted plugin; null for a built-in.
    static PluginPresets* presetsFor(engine::InsertProcessor& live) noexcept;
    // The plugin's editor (see PluginEditor); nothing for a built-in.
    static std::optional<PluginEditor> createEditor(engine::InsertProcessor& live);
    // Whether a hosted plugin has a GUI of its own (without making one).
    static bool hasOwnEditor(engine::InsertProcessor& live);

    // How the built-in Sampler decodes its files (the app's media loader).
    void setSampleLoader(plugins::sampler::Loader loader) { builtIns_.setSampleLoader(std::move(loader)); }

private:
    const PluginRegistry& registry_;
    plugins::BuiltInFactory builtIns_;
    juce::AudioPluginFormatManager formats_;
    mutable std::string lastError_;
    mutable std::map<std::string, std::string> failures_;
};

} // namespace northlight::hosting
