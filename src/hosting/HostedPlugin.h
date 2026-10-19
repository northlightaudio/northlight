#pragma once

#include "engine/Insert.h"
#include "hosting/PluginPresets.h"

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
// and captured when the project is saved. The chain is a stereo pair: the main bus
// is set to stereo in and out (a mono-only plugin gets the pair summed and its
// output on both sides), a second input bus becomes the stereo key, other buses
// stay disabled (an instrument has no input bus and its first output pair is
// taken). Bypass crossfades to the plugin's own bypassed path
// (processBlockBypassed keeps the latency), so toggling it never re-solves the
// graph or freezes a tail.
class HostedPlugin final : public engine::InsertProcessor, public PluginPresets
{
public:
    // Which channel layout the plugin accepted (decided once, at creation).
    enum class Layout
    {
        stereo,
        mono,
        unsupported,
    };
    // Negotiates the main bus (stereo, else mono) and a stereo key on the second
    // input bus; disables the rest, except that an instrument (no input bus) keeps
    // every further stereo output bus it has as an aux output. Returns what was
    // accepted and how many aux pairs.
    static Layout negotiateLayout(juce::AudioPluginInstance& instance, bool& sidechain, int& auxOutputs);

    HostedPlugin(std::string typeName,
                 std::unique_ptr<juce::AudioPluginInstance> instance,
                 Layout layout,
                 bool sidechain,
                 int auxOutputs = 0);
    ~HostedPlugin() override;
    Layout layout() const noexcept { return layout_; }

    const std::string& typeName() const noexcept override { return typeName_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return noParameters_; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void setParameter(int, float) noexcept override {}
    float parameter(int) const noexcept override { return 0.0f; }
    bool ownsParameters() const noexcept override { return true; }
    int auxOutputCount() const noexcept override { return auxOutputs_; }
    const float* auxOutput(int index, int channel) const noexcept override
    {
        const auto i = static_cast<size_t>(index) * 2 + static_cast<size_t>(channel & 1);
        return index >= 0 && index < auxOutputs_ && i < aux_.size() ? aux_[i].data() : nullptr;
    }
    void process(float* left, float* right, int numSamples) noexcept override;
    bool acceptsSidechain() const noexcept override { return sidechain_.load(std::memory_order_acquire); }
    // Instruments (and MIDI effects): the block's events go into the plugin's
    // MIDI buffer at their offsets.
    bool acceptsMidi() const noexcept override { return acceptsMidi_; }
    void processMidi(const engine::MidiEventList& events,
                     float* left,
                     float* right,
                     int numSamples) noexcept override;
    void processWithSidechain(float* left,
                              float* right,
                              const float* keyLeft,
                              const float* keyRight,
                              int numSamples) noexcept override;
    // Read live: a plugin may change it (lookahead settings) after prepare.
    int latencySamples() const noexcept override
    {
        return prepared_.load(std::memory_order_acquire) ? instance_->getLatencySamples() : 0;
    }

    std::string state() const override;
    void setState(const std::string& state) override;

    juce::AudioPluginInstance& instance() noexcept { return *instance_; }
    const juce::AudioPluginInstance& instance() const noexcept { return *instance_; }

    // ----- presets (message thread) -----------------------------------------------
    std::string displayName() const override { return instance_->getPluginDescription().name.toStdString(); }
    // The plugin's built-in programs (empty when it has none worth listing).
    std::vector<std::string> factoryPrograms() const override;
    int currentProgram() const override;
    void setProgram(int index) override;
    // .vstpreset files: the standard user folder for this plugin
    // (Documents/VST3 Presets/<manufacturer>/<plugin>), its files, and load/save.
    bool supportsPresetFiles() const override;
    juce::File userPresetFolder() const override;
    std::vector<juce::File> userPresetFiles() const override;
    bool loadPresetFile(const juce::File& file) override;
    bool savePresetFile(const juce::File& file) const override;
    std::string presetFileExtension() const override { return "vstpreset"; }

private:
    std::string typeName_;
    std::vector<engine::ParameterInfo> noParameters_;
    std::unique_ptr<juce::AudioPluginInstance> instance_;
    void run(float* left, float* right, const float* keyLeft, const float* keyRight, int numSamples) noexcept;
    void processBuffer(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept;

    Layout layout_;
    bool acceptsMidi_ = false;
    juce::MidiBuffer midi_;
    const engine::MidiEventList* pendingMidi_ = nullptr; // this block's events, while running
    std::vector<float> keyLeft_, keyRight_;              // the sidechain bus's channels for one block
    std::vector<float> mono_;                            // the summed pair for a mono plugin
    int auxOutputs_ = 0;                                 // extra stereo output buses (instruments)
    std::vector<std::vector<float>> aux_;                // their channels, two per pair
    std::vector<float*> pointers_;                       // the buffer's channel pointers
    std::vector<float> dryLeft_, dryRight_;              // the input, for the bypass crossfade
    float bypassMix_ = 0.0f;                             // 0 = processed, 1 = bypassed (real-time)
    float bypassStep_ = 0.0f;                            // per-sample ramp (10 ms)
    std::atomic<bool> prepared_{false};
    std::atomic<bool> sidechain_{false}; // the plugin's second input bus is live
};

} // namespace aural::hosting
