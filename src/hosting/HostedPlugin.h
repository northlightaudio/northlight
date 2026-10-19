#pragma once

#include "engine/Insert.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace aural::hosting
{

// A hosted plugin behind the engine's insert interface. The plugin owns its
// parameters and state: parameters() is empty (its own editor edits it) and the
// model keeps an opaque state blob that is restored when the instance is created
// and captured when the project is saved. The chain is a stereo pair, so the main
// bus is set to stereo in and out; extra buses (sidechains) stay disabled for now.
class HostedPlugin final : public engine::InsertProcessor
{
public:
    HostedPlugin(std::string typeName, std::unique_ptr<juce::AudioPluginInstance> instance);
    ~HostedPlugin() override;

    const std::string& typeName() const noexcept override { return typeName_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return noParameters_; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void setParameter(int, float) noexcept override {}
    float parameter(int) const noexcept override { return 0.0f; }
    void process(float* left, float* right, int numSamples) noexcept override;
    // Read live: a plugin may change it (lookahead settings) after prepare.
    int latencySamples() const noexcept override
    {
        return prepared_.load(std::memory_order_acquire) ? instance_->getLatencySamples() : 0;
    }

    std::string state() const override;
    void setState(const std::string& state) override;

    juce::AudioPluginInstance& instance() noexcept { return *instance_; }
    const juce::AudioPluginInstance& instance() const noexcept { return *instance_; }

private:
    std::string typeName_;
    std::vector<engine::ParameterInfo> noParameters_;
    std::unique_ptr<juce::AudioPluginInstance> instance_;
    juce::MidiBuffer midi_;
    std::atomic<bool> prepared_{false};
};

} // namespace aural::hosting
