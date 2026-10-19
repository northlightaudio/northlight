#pragma once

#include "engine/Insert.h"
#include "engine/rt/SpscQueue.h"
#include "hosting/PluginPresets.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <clap/clap.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace aural::hosting
{

// A CLAP plugin behind the engine's insert interface, hosted directly against the
// CLAP C ABI (JUCE does not host CLAP). One instance per insert slot; the .clap
// library is loaded once per instance and released with it.
//
// Threads, as the CLAP spec divides them: main-thread calls (init, activate, state,
// params flush when idle, gui) happen on the message thread; audio-thread calls
// (start/stop_processing, process) on the real-time thread, or on the message
// thread while no audio runs (the host's thread-check then answers "audio thread"
// for that stretch, which the spec allows as long as nothing runs concurrently).
// Parameter changes from the host travel as events: queued on an SPSC queue and
// delivered in the next process() call, or flushed through clap.params when the
// plugin is not processing.
class HostedClapPlugin final : public engine::InsertProcessor, public PluginPresets
{
public:
    // Loads the library and creates the plugin with the given id. Returns null
    // (with `error` set) when the file is not a CLAP, the id is unknown, or the
    // plugin refuses to initialise.
    static std::unique_ptr<HostedClapPlugin> create(const std::string& typeName,
                                                    const std::string& path,
                                                    const std::string& pluginId,
                                                    std::string& error);
    ~HostedClapPlugin() override;

    // ----- engine::InsertProcessor ------------------------------------------------
    const std::string& typeName() const noexcept override { return typeName_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return infos_; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void setParameter(int index, float value) noexcept override;
    float parameter(int index) const noexcept override;
    void process(float* left, float* right, int numSamples) noexcept override;
    bool acceptsSidechain() const noexcept override { return hasSidechain_; }
    // A plugin with a note input port (an instrument, a MIDI effect) gets the
    // block's events as CLAP note events, or MIDI 1.0 events when that is the
    // only dialect it takes.
    bool acceptsMidi() const noexcept override { return hasNoteInput_; }
    void processMidi(const engine::MidiEventList& events,
                     float* left,
                     float* right,
                     int numSamples) noexcept override;
    void processWithSidechain(float* left,
                              float* right,
                              const float* keyLeft,
                              const float* keyRight,
                              int numSamples) noexcept override;
    int latencySamples() const noexcept override { return latency_.load(std::memory_order_relaxed); }
    std::string state() const override;
    void setState(const std::string& state) override;

    // ----- PluginPresets ----------------------------------------------------------
    std::string displayName() const override { return name_; }
    std::vector<std::string> factoryPrograms() const override { return {}; }
    int currentProgram() const override { return -1; }
    void setProgram(int) override {}
    bool supportsPresetFiles() const override { return state_ != nullptr; }
    juce::File userPresetFolder() const override;
    std::vector<juce::File> userPresetFiles() const override;
    bool loadPresetFile(const juce::File& file) override;
    bool savePresetFile(const juce::File& file) const override;
    static constexpr const char* presetExtension = "clappreset";
    std::string presetFileExtension() const override { return presetExtension; }

    // ----- editor -----------------------------------------------------------------
    bool hasEditor() const noexcept { return gui_ != nullptr; }
    // Opens the plugin's GUI as a child of `parent` (a native window handle) at the
    // plugin's own size, reported through `size`. False when it has no GUI or the
    // GUI refused.
    bool openEditor(void* parentHwnd, double scale, int& width, int& height);
    void closeEditor();
    bool isEditorOpen() const noexcept { return editorOpen_; }
    // Called by the window when the host resized the plugin's area.
    void editorResized(int width, int height);
    // The plugin asked for a new size (from the gui extension); the window polls this.
    bool takeResizeRequest(int& width, int& height);
    bool editorIsResizable() const noexcept;

    const clap_plugin_descriptor_t* descriptor() const noexcept { return descriptor_; }

private:
    HostedClapPlugin() = default;
    bool initialise(const std::string& path, const std::string& pluginId, std::string& error);
    void readParameters();
    void refreshLatency();
    void runEvents(const clap_input_events_t& in, const clap_output_events_t& out) noexcept;
    bool run(float* left, float* right, const float* keyLeft, const float* keyRight, int numSamples) noexcept;
    void ensureProcessing(bool on) noexcept;
    void onMainThread();

    // ----- the clap_host we hand the plugin ---------------------------------------
    static HostedClapPlugin& self(const clap_host_t* host) noexcept;
    static const void* hostGetExtension(const clap_host_t* host, const char* id) noexcept;
    static void hostRequestRestart(const clap_host_t* host) noexcept;
    static void hostRequestProcess(const clap_host_t* host) noexcept;
    static void hostRequestCallback(const clap_host_t* host) noexcept;
    static void hostLog(const clap_host_t* host, clap_log_severity severity, const char* msg) noexcept;
    static bool hostIsMainThread(const clap_host_t* host) noexcept;
    static bool hostIsAudioThread(const clap_host_t* host) noexcept;
    static void hostParamsRescan(const clap_host_t* host, clap_param_rescan_flags flags) noexcept;
    static void hostParamsClear(const clap_host_t* host, clap_id id, clap_param_clear_flags flags) noexcept;
    static void hostParamsRequestFlush(const clap_host_t* host) noexcept;
    static void hostStateMarkDirty(const clap_host_t* host) noexcept;
    static void hostLatencyChanged(const clap_host_t* host) noexcept;
    static bool hostAudioPortsIsRescanFlagSupported(const clap_host_t* host, uint32_t flag) noexcept;
    static void hostAudioPortsRescan(const clap_host_t* host, uint32_t flags) noexcept;
    static uint32_t hostNotePortsSupportedDialects(const clap_host_t* host) noexcept;
    static void hostNotePortsRescan(const clap_host_t* host, uint32_t flags) noexcept;
    static void hostGuiResizeHintsChanged(const clap_host_t* host) noexcept;
    static bool hostGuiRequestResize(const clap_host_t* host, uint32_t width, uint32_t height) noexcept;
    static bool hostGuiRequestShow(const clap_host_t* host) noexcept;
    static bool hostGuiRequestHide(const clap_host_t* host) noexcept;
    static void hostGuiClosed(const clap_host_t* host, bool wasDestroyed) noexcept;
    static bool hostTimerRegister(const clap_host_t* host, uint32_t periodMs, clap_id* timerId) noexcept;
    static bool hostTimerUnregister(const clap_host_t* host, clap_id timerId) noexcept;

    // ----- event lists over preallocated storage ----------------------------------
    struct ParamChange
    {
        clap_id id;
        void* cookie;
        double value;
    };
    union EventSlot
    {
        clap_event_header_t header;
        clap_event_param_value_t param;
        clap_event_note_t note;
        clap_event_midi_t midi;
    };
    struct InEvents
    {
        std::vector<EventSlot> events; // preallocated
        uint32_t count = 0;
        static uint32_t size(const clap_input_events_t* list) noexcept;
        static const clap_event_header_t* get(const clap_input_events_t* list, uint32_t index) noexcept;
        clap_input_events_t list{};
    };
    struct OutEvents
    {
        static bool tryPush(const clap_output_events_t* list, const clap_event_header_t* event) noexcept;
        clap_output_events_t list{};
        HostedClapPlugin* owner = nullptr;
    };

    class Timer;

    std::string typeName_, name_, vendor_, version_;
    juce::DynamicLibrary library_;
    const clap_plugin_entry_t* entry_ = nullptr;
    const clap_plugin_factory_t* factory_ = nullptr;
    const clap_plugin_descriptor_t* descriptor_ = nullptr;
    const clap_plugin_t* plugin_ = nullptr;
    clap_host_t host_{};
    clap_host_log_t hostLog_{};
    clap_host_thread_check_t hostThreadCheck_{};
    clap_host_params_t hostParams_{};
    clap_host_state_t hostState_{};
    clap_host_latency_t hostLatency_{};
    clap_host_audio_ports_t hostAudioPorts_{};
    clap_host_note_ports_t hostNotePorts_{};
    clap_host_gui_t hostGui_{};
    clap_host_timer_support_t hostTimer_{};

    const clap_plugin_params_t* params_ = nullptr;
    const clap_plugin_state_t* state_ = nullptr;
    const clap_plugin_latency_t* latency_ext_ = nullptr;
    const clap_plugin_audio_ports_t* audioPorts_ = nullptr;
    const clap_plugin_note_ports_t* notePorts_ = nullptr;
    const clap_plugin_gui_t* gui_ = nullptr;

    std::vector<engine::ParameterInfo> infos_;
    std::vector<clap_param_info_t> paramInfos_;
    std::vector<std::atomic<float>> values_;              // last value the host sent or read, per parameter
    std::unique_ptr<rt::SpscQueue<ParamChange>> pending_; // host -> audio thread
    InEvents in_;
    OutEvents out_;
    bool hasSidechain_ = false;
    bool hasNoteInput_ = false;
    bool noteDialectClap_ = true;                        // else MIDI 1.0 events
    const engine::MidiEventList* pendingMidi_ = nullptr; // this block's events, while running
    int inputPorts_ = 0, outputPorts_ = 0;
    std::vector<float> keyLeft_, keyRight_, dryLeft_, dryRight_;
    std::vector<float*> inChannels0_, inChannels1_, outChannels0_;
    clap_audio_buffer_t inputs_[2]{}, outputs_[1]{};
    float bypassMix_ = 0.0f, bypassStep_ = 0.0f;
    std::atomic<bool> activated_{false};
    std::atomic<bool> processing_{false}; // start_processing has been called (audio side)
    std::atomic<bool> pretendAudioThread_{false};
    std::atomic<int> latency_{0};
    std::atomic<bool> latencyDirty_{false};
    std::atomic<bool> flushRequested_{false};
    std::atomic<bool> callbackRequested_{false};
    std::atomic<bool> restartRequested_{false};
    double sampleRate_ = 48000.0;
    int maxBlock_ = 0;
    bool editorOpen_ = false;
    std::atomic<bool> resizeRequested_{false};
    std::atomic<uint32_t> requestedWidth_{0}, requestedHeight_{0};
    std::map<clap_id, std::unique_ptr<Timer>> timers_;
    clap_id nextTimerId_ = 1;
};

} // namespace aural::hosting
