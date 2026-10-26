#pragma once

#include "engine/Insert.h"
#include "hosting/ParameterEditQueue.h"
#include "hosting/PluginPresets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace northlight::hosting
{

// A hosted plugin behind the engine's insert interface. The plugin owns its
// parameters and state: parameters() lists the plugin's parameters as
// normalised 0..1 values (for automation and the lane menu; the plugin's own
// editor edits them, and setParameter/parameter go through JUCE's parameter
// objects on the message thread, never the audio thread), and the model keeps
// an opaque state blob that is restored when the instance is created and
// captured when the project is saved. The chain is a stereo pair: the main bus
// is set to stereo in and out (a mono-only plugin gets the pair summed and its
// output on both sides), a second input bus becomes the stereo key, other buses
// stay disabled (an instrument has no input bus and its first output pair is
// taken). Bypass crossfades to the plugin's own bypassed path
// (processBlockBypassed keeps the latency), so toggling it never re-solves the
// graph or freezes a tail.
class HostedPlugin final : public engine::InsertProcessor,
                           public PluginPresets,
                           private juce::AudioProcessorParameter::Listener,
                           private juce::AudioProcessorListener
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
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return parameterInfos_; }

    void prepare(double sampleRate, int maxBlockSize) override;
    void setParameter(int index, float value) noexcept override;
    float parameter(int index) const noexcept override;
    bool takeParameterEdit(ParameterEdit& edit) noexcept override;
    bool takeStateChanged() noexcept override
    {
        return stateChanged_.exchange(false, std::memory_order_acq_rel);
    }
    bool ownsParameters() const noexcept override { return true; }
    bool crashed() const noexcept override { return crashed_.load(std::memory_order_acquire); }
    // The VST3 ParamID (through JUCE's parameter id): stable across versions.
    std::string parameterKey(int index) const override;
    int auxOutputCount() const noexcept override { return auxOutputs_; }
    const float* auxOutput(int index, int channel) const noexcept override
    {
        const auto i = static_cast<size_t>(index) * 2 + static_cast<size_t>(channel & 1);
        return index >= 0 && index < auxOutputs_ && i < aux_.size() ? aux_[i].data() : nullptr;
    }
    void process(float* left, float* right, int numSamples) noexcept override;
    void setPlayHead(const engine::PlayHead& head) noexcept override { playHead_.head = head; }
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
    // .vstpreset files in the standard user folder for this plugin
    // (Documents/VST3 Presets/<manufacturer>/<plugin>).
    bool supportsPresetFiles() const override;
    PresetLocation presetLocation() const override;
    bool loadPresetFile(const juce::File& file) override;
    bool savePresetFile(const juce::File& file) const override;

private:
    std::string typeName_;
    std::vector<engine::ParameterInfo> parameterInfos_;
    std::vector<juce::AudioProcessorParameter*> parameterObjects_; // owned by the instance
    std::vector<int> indexOfProcessorParameter_;                   // processor index -> ours (-1 = none)
    // Edits the plugin made (its editor): the listener runs on the message
    // thread, or sometimes the audio thread; the queue never blocks either.
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void pushEdit(const ParameterEdit& edit) noexcept;
    // The plugin marking its state dirty (VST3 IComponentHandler2::setDirty).
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override;
    std::atomic<bool> stateChanged_{false};
    ParameterEditQueue edits_;
    static thread_local bool settingFromHost_; // our own setValue: not an edit
    std::unique_ptr<juce::AudioPluginInstance> instance_;
    void run(float* left, float* right, const float* keyLeft, const float* keyRight, int numSamples) noexcept;
    void processBuffer(juce::AudioBuffer<float>& buffer, bool bypassed) noexcept;

    Layout layout_;
    bool acceptsMidi_ = false;
    juce::MidiBuffer midi_;
    std::atomic<bool> crashed_{false};         // the plugin crashed while processing: never called again
    static constexpr int maxMidiEvents = 1024; // engine::MidiEventList's capacity
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

    // The transport the plugin asks for during processBlock (tempo, position).
    struct PlayHead final : juce::AudioPlayHead
    {
        engine::PlayHead head;
        juce::Optional<PositionInfo> getPosition() const override;
    };
    PlayHead playHead_;
};

} // namespace northlight::hosting
