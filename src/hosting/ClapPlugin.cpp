#include "hosting/ClapPlugin.h"

#include "engine/rt/RealtimeGuard.h"
#include "hosting/CrashGuard.h"
#include "version/Version.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace northlight::hosting
{

namespace
{
constexpr size_t bypassRingSize = 1 << 16; // samples of dry history kept for a bypassed plugin
constexpr size_t eventCapacity = 2048;
constexpr size_t pendingCapacity = 2048;
} // namespace

// ----- the module: one library and entry point per file -------------------------------------

struct HostedClapPlugin::Module
{
    std::string path;
    juce::DynamicLibrary library;
    const clap_plugin_entry_t* entry = nullptr;
    bool initialised = false;

    ~Module()
    {
        // Last instance of this file gone: the entry point goes with it.
        if (initialised)
            entry->deinit();
    }
};

std::shared_ptr<HostedClapPlugin::Module> HostedClapPlugin::acquireModule(const std::string& path,
                                                                          std::string& error)
{
    static std::mutex mutex;
    static std::map<std::string, std::weak_ptr<Module>> modules;
    const std::lock_guard<std::mutex> lock(mutex);
    if (const auto it = modules.find(path); it != modules.end())
        if (auto existing = it->second.lock())
            return existing;

    auto module = std::make_shared<Module>();
    module->path = path;
    if (!module->library.open(juce::String::fromUTF8(path.c_str())))
    {
        error = "could not load " + path;
        return nullptr;
    }
    module->entry = static_cast<const clap_plugin_entry_t*>(module->library.getFunction("clap_entry"));
    if (module->entry == nullptr)
    {
        error = "not a CLAP plugin (no clap_entry)";
        return nullptr;
    }
    if (!clap_version_is_compatible(module->entry->clap_version))
    {
        error = "incompatible CLAP version";
        return nullptr;
    }
    if (!module->entry->init(path.c_str()))
    {
        error = "the plugin's entry point refused to initialise";
        return nullptr;
    }
    module->initialised = true;
    modules[path] = module;
    return module;
}

// ----- live instances -----------------------------------------------------------------------

std::mutex& HostedClapPlugin::liveMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::vector<HostedClapPlugin*>& HostedClapPlugin::liveInstances()
{
    static std::vector<HostedClapPlugin*> instances;
    return instances;
}

bool HostedClapPlugin::isLive(const HostedClapPlugin* plugin)
{
    const std::lock_guard<std::mutex> lock(liveMutex());
    const auto& live = liveInstances();
    return std::find(live.begin(), live.end(), plugin) != live.end();
}

void HostedClapPlugin::serviceAll(bool engineRunning)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostedClapPlugin::serviceAll");
    std::vector<HostedClapPlugin*> snapshot;
    {
        const std::lock_guard<std::mutex> lock(liveMutex());
        snapshot = liveInstances();
    }
    for (auto* plugin : snapshot)
    {
        plugin->engineRunning_.store(engineRunning, std::memory_order_release);
        plugin->onMainThread();
    }
}

// ----- host-side timers (the timer-support extension) ----------------------------------------

class HostedClapPlugin::Timer final : public juce::Timer
{
public:
    Timer(HostedClapPlugin& owner, clap_id id, uint32_t periodMs) : owner_(owner), id_(id)
    {
        startTimer(static_cast<int>(std::max<uint32_t>(1, periodMs)));
    }
    void timerCallback() override
    {
        if (const auto* ext = static_cast<const clap_plugin_timer_support_t*>(
                owner_.plugin_->get_extension(owner_.plugin_, CLAP_EXT_TIMER_SUPPORT)))
            ext->on_timer(owner_.plugin_, id_);
    }

private:
    HostedClapPlugin& owner_;
    clap_id id_;
};

// ----- event lists ------------------------------------------------------------------------------

uint32_t HostedClapPlugin::InEvents::size(const clap_input_events_t* list) noexcept
{
    return static_cast<const InEvents*>(list->ctx)->count;
}

const clap_event_header_t* HostedClapPlugin::InEvents::get(const clap_input_events_t* list,
                                                           uint32_t index) noexcept
{
    const auto* self = static_cast<const InEvents*>(list->ctx);
    return index < self->count ? &self->events[index].header : nullptr;
}

bool HostedClapPlugin::OutEvents::tryPush(const clap_output_events_t* list,
                                          const clap_event_header_t* event) noexcept
{
    // Parameter changes the plugin makes (its own GUI, automation it generates)
    // update the host's view of the value; everything else is accepted and dropped.
    // The tables are never reallocated while the plugin is active (see rescan).
    auto* self = static_cast<OutEvents*>(list->ctx);
    if (event->space_id != CLAP_CORE_EVENT_SPACE_ID || self->owner == nullptr)
        return true;
    auto& owner = *self->owner;
    const auto indexOf = [&owner](clap_id id)
    {
        for (size_t i = 0; i < owner.paramInfos_.size(); ++i)
            if (owner.paramInfos_[i].id == id)
                return static_cast<int>(i);
        return -1;
    };
    if (event->type == CLAP_EVENT_PARAM_VALUE)
    {
        const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
        if (const int i = indexOf(value->param_id); i >= 0)
        {
            owner.values_[static_cast<size_t>(i)].store(static_cast<float>(value->value),
                                                        std::memory_order_relaxed);
            if (owner.edits_ != nullptr)
                owner.edits_->push({ParameterEdit::Kind::value, i, static_cast<float>(value->value)});
        }
    }
    else if (event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || event->type == CLAP_EVENT_PARAM_GESTURE_END)
    {
        const auto* gesture = reinterpret_cast<const clap_event_param_gesture_t*>(event);
        if (const int i = indexOf(gesture->param_id); i >= 0 && owner.edits_ != nullptr)
            owner.edits_->push({event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ? ParameterEdit::Kind::begin
                                                                              : ParameterEdit::Kind::end,
                                i,
                                0.0f});
    }
    return true;
}

// ----- creation ---------------------------------------------------------------------------------

std::unique_ptr<HostedClapPlugin> HostedClapPlugin::create(const std::string& typeName,
                                                           const std::string& path,
                                                           const std::string& pluginId,
                                                           std::string& error)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostedClapPlugin::create");
    std::unique_ptr<HostedClapPlugin> plugin(new HostedClapPlugin());
    plugin->typeName_ = typeName;
    if (!plugin->initialise(path, pluginId, error))
        return nullptr;
    const std::lock_guard<std::mutex> lock(liveMutex());
    liveInstances().push_back(plugin.get());
    return plugin;
}

HostedClapPlugin& HostedClapPlugin::self(const clap_host_t* host) noexcept
{
    return *static_cast<HostedClapPlugin*>(host->host_data);
}

bool HostedClapPlugin::initialise(const std::string& path, const std::string& pluginId, std::string& error)
{
    module_ = acquireModule(path, error);
    if (module_ == nullptr)
        return false;
    factory_ = static_cast<const clap_plugin_factory_t*>(module_->entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (factory_ == nullptr)
    {
        error = "the plugin has no plugin factory";
        return false;
    }
    const uint32_t count = factory_->get_plugin_count(factory_);
    for (uint32_t i = 0; i < count && descriptor_ == nullptr; ++i)
        if (const auto* d = factory_->get_plugin_descriptor(factory_, i); d != nullptr && pluginId == d->id)
            descriptor_ = d;
    if (descriptor_ == nullptr)
    {
        error = "the file holds no plugin with id " + pluginId;
        return false;
    }
    name_ = descriptor_->name != nullptr ? descriptor_->name : pluginId;
    vendor_ = descriptor_->vendor != nullptr ? descriptor_->vendor : "";
    version_ = descriptor_->version != nullptr ? descriptor_->version : "";

    // The host object the plugin talks back to.
    host_.clap_version = CLAP_VERSION;
    host_.host_data = this;
    host_.name = "Northlight";
    host_.vendor = "Leafshade";
    host_.url = "https://leafshade.io";
    host_.version = northlight::version::number(); // the host's own version, from CMake
    host_.get_extension = &hostGetExtension;
    host_.request_restart = &hostRequestRestart;
    host_.request_process = &hostRequestProcess;
    host_.request_callback = &hostRequestCallback;
    hostLog_.log = &hostLog;
    hostThreadCheck_.is_main_thread = &hostIsMainThread;
    hostThreadCheck_.is_audio_thread = &hostIsAudioThread;
    hostParams_.rescan = &hostParamsRescan;
    hostParams_.clear = &hostParamsClear;
    hostParams_.request_flush = &hostParamsRequestFlush;
    hostState_.mark_dirty = &hostStateMarkDirty;
    hostLatency_.changed = &hostLatencyChanged;
    hostAudioPorts_.is_rescan_flag_supported = &hostAudioPortsIsRescanFlagSupported;
    hostAudioPorts_.rescan = &hostAudioPortsRescan;
    hostNotePorts_.supported_dialects = &hostNotePortsSupportedDialects;
    hostNotePorts_.rescan = &hostNotePortsRescan;
    hostGui_.resize_hints_changed = &hostGuiResizeHintsChanged;
    hostGui_.request_resize = &hostGuiRequestResize;
    hostGui_.request_show = &hostGuiRequestShow;
    hostGui_.request_hide = &hostGuiRequestHide;
    hostGui_.closed = &hostGuiClosed;
    hostTimer_.register_timer = &hostTimerRegister;
    hostTimer_.unregister_timer = &hostTimerUnregister;

    plugin_ = factory_->create_plugin(factory_, &host_, descriptor_->id);
    if (plugin_ == nullptr)
    {
        error = "the factory could not create the plugin";
        return false;
    }
    if (!plugin_->init(plugin_))
    {
        error = "the plugin refused to initialise";
        plugin_->destroy(plugin_);
        plugin_ = nullptr;
        return false;
    }
    params_ = static_cast<const clap_plugin_params_t*>(plugin_->get_extension(plugin_, CLAP_EXT_PARAMS));
    state_ = static_cast<const clap_plugin_state_t*>(plugin_->get_extension(plugin_, CLAP_EXT_STATE));
    latency_ext_ =
        static_cast<const clap_plugin_latency_t*>(plugin_->get_extension(plugin_, CLAP_EXT_LATENCY));
    audioPorts_ =
        static_cast<const clap_plugin_audio_ports_t*>(plugin_->get_extension(plugin_, CLAP_EXT_AUDIO_PORTS));
    gui_ = static_cast<const clap_plugin_gui_t*>(plugin_->get_extension(plugin_, CLAP_EXT_GUI));
    if (gui_ != nullptr && !gui_->is_api_supported(plugin_, CLAP_WINDOW_API_WIN32, false))
        gui_ = nullptr; // only an embedded win32 GUI is hosted

    if (!readPorts(error))
    {
        plugin_->destroy(plugin_);
        plugin_ = nullptr;
        return false;
    }

    // Notes: the first note input port, in its preferred dialect.
    notePorts_ =
        static_cast<const clap_plugin_note_ports_t*>(plugin_->get_extension(plugin_, CLAP_EXT_NOTE_PORTS));
    if (notePorts_ != nullptr && notePorts_->count(plugin_, true) > 0)
    {
        clap_note_port_info_t info{};
        if (notePorts_->get(plugin_, 0, true, &info))
        {
            hasNoteInput_ =
                (info.supported_dialects & (CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI)) != 0;
            noteDialectClap_ = (info.supported_dialects & CLAP_NOTE_DIALECT_CLAP) != 0;
        }
    }
    readParameters();
    pending_ = std::make_unique<rt::SpscQueue<ParamChange>>(pendingCapacity);
    edits_ = std::make_unique<ParameterEditQueue>(pendingCapacity);
    in_.list.ctx = &in_;
    in_.list.size = &InEvents::size;
    in_.list.get = &InEvents::get;
    in_.events.resize(eventCapacity);
    out_.owner = this;
    out_.list.ctx = &out_;
    out_.list.try_push = &OutEvents::tryPush;
    return true;
}

bool HostedClapPlugin::readPorts(std::string& error)
{
    inPorts_.clear();
    outPorts_.clear();
    if (audioPorts_ != nullptr)
    {
        const auto read = [&](bool isInput, std::vector<Port>& ports)
        {
            const uint32_t count = audioPorts_->count(plugin_, isInput);
            for (uint32_t i = 0; i < count; ++i)
            {
                clap_audio_port_info_t info{};
                if (!audioPorts_->get(plugin_, i, isInput, &info))
                    return false;
                Port port;
                port.id = info.id;
                port.channels = static_cast<int>(info.channel_count);
                port.inPlacePair = info.in_place_pair;
                ports.push_back(std::move(port));
            }
            return true;
        };
        if (!read(true, inPorts_) || !read(false, outPorts_))
        {
            error = "the plugin did not describe its audio ports";
            return false;
        }
    }
    if (outPorts_.empty())
    {
        error = "the plugin has no audio output";
        return false;
    }
    mainInChannels_ = inPorts_.empty() ? 0 : inPorts_[0].channels;
    mainOutChannels_ = outPorts_[0].channels;
    if (mainInChannels_ > 2 || mainInChannels_ < 0)
    {
        error = "the plugin's main input has " + std::to_string(mainInChannels_) +
                " channels; only mono and stereo are supported";
        return false;
    }
    if (mainOutChannels_ != 1 && mainOutChannels_ != 2)
    {
        error = "the plugin's main output has " + std::to_string(mainOutChannels_) +
                " channels; only mono and stereo are supported";
        return false;
    }
    hasSidechain_ = inPorts_.size() >= 2 && inPorts_[1].channels == 2;
    // In place only when the plugin pairs its main ports (either side may say so)
    // and both are the chain's stereo pair.
    aliasMain_ = !inPorts_.empty() && mainInChannels_ == 2 && mainOutChannels_ == 2 &&
                 (outPorts_[0].inPlacePair == inPorts_[0].id || inPorts_[0].inPlacePair == outPorts_[0].id);
    inBuffers_.assign(inPorts_.size(), clap_audio_buffer_t{});
    outBuffers_.assign(outPorts_.size(), clap_audio_buffer_t{});
    return true;
}

HostedClapPlugin::~HostedClapPlugin()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostedClapPlugin::~HostedClapPlugin");
    {
        const std::lock_guard<std::mutex> lock(liveMutex());
        std::erase(liveInstances(), this);
    }
    if (plugin_ != nullptr)
    {
        closeEditor();
        timers_.clear();
        if (activated_.load(std::memory_order_acquire))
        {
            ensureProcessing(false);
            plugin_->deactivate(plugin_);
        }
        plugin_->destroy(plugin_);
    }
    // module_ releases the library (and the entry point with the last instance).
}

void HostedClapPlugin::readParameters()
{
    // Only while the plugin cannot be processing: the audio thread walks these
    // tables (OutEvents::tryPush).
    infos_.clear();
    paramInfos_.clear();
    if (params_ == nullptr)
    {
        values_ = std::vector<std::atomic<float>>(0);
        return;
    }
    const uint32_t count = params_->count(plugin_);
    for (uint32_t i = 0; i < count; ++i)
    {
        clap_param_info_t info{};
        if (!params_->get_info(plugin_, i, &info))
            continue;
        paramInfos_.push_back(info);
        engine::ParameterInfo p;
        p.name = info.name;
        p.minimum = static_cast<float>(info.min_value);
        p.maximum = static_cast<float>(info.max_value);
        p.defaultValue = static_cast<float>(info.default_value);
        infos_.push_back(p);
    }
    values_ = std::vector<std::atomic<float>>(paramInfos_.size());
    refreshValues();
}

void HostedClapPlugin::refreshValues()
{
    if (params_ == nullptr)
        return;
    for (size_t i = 0; i < paramInfos_.size(); ++i)
    {
        double value = paramInfos_[i].default_value;
        if (params_->get_value(plugin_, paramInfos_[i].id, &value))
            values_[i].store(static_cast<float>(value), std::memory_order_relaxed);
    }
}

// ----- lifecycle --------------------------------------------------------------------------------

void HostedClapPlugin::prepare(double sampleRate, int maxBlockSize)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostedClapPlugin::prepare");
    if (activated_.load(std::memory_order_acquire))
    {
        ensureProcessing(false);
        plugin_->deactivate(plugin_);
        activated_.store(false, std::memory_order_release);
    }
    sampleRate_ = sampleRate;
    maxBlock_ = std::max(1, maxBlockSize);
    const auto n = static_cast<size_t>(maxBlock_);
    keyLeft_.assign(n, 0.0f);
    keyRight_.assign(n, 0.0f);
    dryLeft_.assign(n, 0.0f);
    dryRight_.assign(n, 0.0f);
    monoIn_.assign(n, 0.0f);
    outLeft_.assign(n, 0.0f);
    outRight_.assign(n, 0.0f);
    bypassRingLeft_.assign(bypassRingSize, 0.0f);
    bypassRingRight_.assign(bypassRingSize, 0.0f);
    bypassWrite_ = 0;
    // Scratch for every port the chain does not feed or take: silent inputs, drained
    // outputs. The main ports and the key get their pointers per block.
    const auto allocate = [&](Port& port, bool isMain, bool isKey)
    {
        port.scratch.clear();
        port.pointers.assign(static_cast<size_t>(std::max(0, port.channels)), nullptr);
        if (isMain || isKey)
            return;
        port.scratch.assign(static_cast<size_t>(std::max(0, port.channels)), std::vector<float>(n, 0.0f));
        for (size_t c = 0; c < port.scratch.size(); ++c)
            port.pointers[c] = port.scratch[c].data();
    };
    for (size_t i = 0; i < inPorts_.size(); ++i)
        allocate(inPorts_[i], i == 0, i == 1 && hasSidechain_);
    for (size_t i = 0; i < outPorts_.size(); ++i)
        allocate(outPorts_[i], i == 0, false);
    bypassStep_ = 1.0f / static_cast<float>(std::max(1.0, sampleRate * 0.010));
    bypassMix_ = isBypassed() ? 1.0f : 0.0f;
    restartRequested_.store(false, std::memory_order_release);
    if (!plugin_->activate(plugin_, sampleRate, 1, static_cast<uint32_t>(maxBlock_)))
    {
        latency_.store(0, std::memory_order_relaxed); // passing through: no delay to compensate
        return;                                       // stays inactive: process() passes audio through
    }
    activated_.store(true, std::memory_order_release);
    refreshLatency();
}

void HostedClapPlugin::refreshLatency()
{
    latency_.store(latency_ext_ != nullptr && activated_.load(std::memory_order_acquire)
                       ? static_cast<int>(latency_ext_->get(plugin_))
                       : 0,
                   std::memory_order_relaxed);
    latencyDirty_.store(false, std::memory_order_relaxed);
}

void HostedClapPlugin::ensureProcessing(bool on) noexcept
{
    const bool now = processing_.load(std::memory_order_acquire);
    if (now == on || !activated_.load(std::memory_order_acquire))
        return;
    // start/stop_processing are audio-thread calls; when no audio runs they come
    // from the message thread and the thread check vouches for it.
    const bool pretend = !rt::isRealtimeThread();
    if (pretend)
        pretendAudioThread_.store(true, std::memory_order_release);
    if (on)
        plugin_->start_processing(plugin_);
    else
        plugin_->stop_processing(plugin_);
    if (pretend)
        pretendAudioThread_.store(false, std::memory_order_release);
    processing_.store(on, std::memory_order_release);
}

// ----- parameters -------------------------------------------------------------------------------

void HostedClapPlugin::setParameter(int index, float value) noexcept
{
    if (index < 0 || static_cast<size_t>(index) >= paramInfos_.size())
        return;
    const auto& info = paramInfos_[static_cast<size_t>(index)];
    const double clamped = std::clamp(static_cast<double>(value), info.min_value, info.max_value);
    values_[static_cast<size_t>(index)].store(static_cast<float>(clamped), std::memory_order_relaxed);
    pending_->push(ParamChange{ParamChange::Kind::value, info.id, info.cookie, clamped});
    flushRequested_.store(true, std::memory_order_release); // delivered by process(), else by the poll
}

void HostedClapPlugin::beginParameterGesture(int index) noexcept
{
    if (index < 0 || static_cast<size_t>(index) >= paramInfos_.size())
        return;
    const auto& info = paramInfos_[static_cast<size_t>(index)];
    pending_->push(ParamChange{ParamChange::Kind::gestureBegin, info.id, info.cookie, 0.0});
    flushRequested_.store(true, std::memory_order_release);
}

void HostedClapPlugin::endParameterGesture(int index) noexcept
{
    if (index < 0 || static_cast<size_t>(index) >= paramInfos_.size())
        return;
    const auto& info = paramInfos_[static_cast<size_t>(index)];
    pending_->push(ParamChange{ParamChange::Kind::gestureEnd, info.id, info.cookie, 0.0});
    flushRequested_.store(true, std::memory_order_release);
}

float HostedClapPlugin::parameter(int index) const noexcept
{
    if (index < 0 || static_cast<size_t>(index) >= values_.size())
        return 0.0f;
    return values_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
}

void HostedClapPlugin::collectEvents(const engine::MidiEventList* midi) noexcept
{
    // Pending host parameter changes and gestures become sample-0 events.
    in_.count = 0;
    ParamChange change;
    while (in_.count < in_.events.size() && pending_->pop(change))
    {
        auto& slot = in_.events[in_.count++];
        if (change.kind == ParamChange::Kind::value)
        {
            auto& e = slot.param;
            e.header.size = sizeof(clap_event_param_value_t);
            e.header.time = 0;
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = CLAP_EVENT_PARAM_VALUE;
            e.header.flags = 0;
            e.param_id = change.id;
            e.cookie = change.cookie;
            e.note_id = -1;
            e.port_index = -1;
            e.channel = -1;
            e.key = -1;
            e.value = change.value;
        }
        else
        {
            auto& e = slot.gesture;
            e.header.size = sizeof(clap_event_param_gesture_t);
            e.header.time = 0;
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = static_cast<uint16_t>(change.kind == ParamChange::Kind::gestureBegin
                                                      ? CLAP_EVENT_PARAM_GESTURE_BEGIN
                                                      : CLAP_EVENT_PARAM_GESTURE_END);
            e.header.flags = 0;
            e.param_id = change.id;
        }
    }
    flushRequested_.store(false, std::memory_order_release);
    // Then the block's notes, in time order (the list arrives sorted).
    if (midi == nullptr || !hasNoteInput_)
        return;
    for (const auto& m : *midi)
    {
        if (in_.count >= in_.events.size())
            break;
        auto& slot = in_.events[in_.count++];
        // Notes in the CLAP dialect when the port prefers it; everything else
        // (controllers, pitch bend, pressure) as MIDI 1.0 events.
        if (noteDialectClap_ && (m.isNote() || m.type == engine::MidiEvent::Type::allNotesOff))
        {
            auto& e = slot.note;
            e.header.size = sizeof(clap_event_note_t);
            e.header.time = static_cast<uint32_t>(std::max(0, m.sampleOffset));
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type =
                static_cast<uint16_t>(m.type == engine::MidiEvent::Type::noteOn    ? CLAP_EVENT_NOTE_ON
                                      : m.type == engine::MidiEvent::Type::noteOff ? CLAP_EVENT_NOTE_OFF
                                                                                   : CLAP_EVENT_NOTE_CHOKE);
            e.header.flags = 0;
            e.note_id = -1;
            e.port_index = 0;
            e.channel = m.type == engine::MidiEvent::Type::allNotesOff ? -1 : static_cast<int16_t>(m.channel);
            e.key = m.type == engine::MidiEvent::Type::allNotesOff ? -1 : static_cast<int16_t>(m.note);
            e.velocity = m.type == engine::MidiEvent::Type::noteOn ? m.velocity / 127.0 : 0.0;
        }
        else
        {
            auto& e = slot.midi;
            e.header.size = sizeof(clap_event_midi_t);
            e.header.time = static_cast<uint32_t>(std::max(0, m.sampleOffset));
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.header.type = CLAP_EVENT_MIDI;
            e.header.flags = 0;
            e.port_index = 0;
            std::uint8_t bytes[3] = {};
            const int size = m.toBytes(bytes);
            e.data[0] = bytes[0];
            e.data[1] = bytes[1];
            e.data[2] = size > 2 ? bytes[2] : 0;
        }
    }
}

// ----- processing -------------------------------------------------------------------------------

void HostedClapPlugin::process(float* left, float* right, int numSamples) noexcept
{
    run(left, right, nullptr, nullptr, numSamples);
}

void HostedClapPlugin::processWithSidechain(
    float* left, float* right, const float* keyLeft, const float* keyRight, int numSamples) noexcept
{
    run(left, right, keyLeft, keyRight, numSamples);
}

void HostedClapPlugin::processMidi(const engine::MidiEventList& events,
                                   float* left,
                                   float* right,
                                   int numSamples) noexcept
{
    pendingMidi_ = &events;
    run(left, right, nullptr, nullptr, numSamples);
    pendingMidi_ = nullptr;
}

bool HostedClapPlugin::run(
    float* left, float* right, const float* keyLeft, const float* keyRight, int numSamples) noexcept
{
    if (!activated_.load(std::memory_order_acquire) || numSamples > maxBlock_ ||
        crashed_.load(std::memory_order_relaxed))
        return false;
    const Claim claim(owner_, audioThread);
    if (!claim.held)
        return false; // a main-thread flush is in the plugin: this block passes through
    ensureProcessing(true);
    collectEvents(pendingMidi_);

    // The input is kept: the bypass path and the error path need it, and an
    // in-place plugin overwrites it.
    const auto n = static_cast<size_t>(numSamples);
    std::copy(left, left + numSamples, dryLeft_.data());
    std::copy(right, right + numSamples, dryRight_.data());
    // The dry history for bypass, delayed by the latency so a bypassed plugin
    // keeps the delay the graph compensates for.
    const size_t delay = std::min<size_t>(
        static_cast<size_t>(std::max(0, latency_.load(std::memory_order_relaxed))), bypassRingSize - 1);
    for (size_t i = 0; i < n; ++i)
    {
        bypassRingLeft_[bypassWrite_] = left[i];
        bypassRingRight_[bypassWrite_] = right[i];
        bypassWrite_ = (bypassWrite_ + 1) & (bypassRingSize - 1);
    }
    const auto delayedDry = [&](size_t i, bool rightSide)
    {
        const size_t index = (bypassWrite_ + bypassRingSize - n + i - delay) & (bypassRingSize - 1);
        return rightSide ? bypassRingRight_[index] : bypassRingLeft_[index];
    };

    // Buffers: the main input is the chain's pair (or its sum for a mono port), the
    // key goes on port 1, every other port sees silent scratch; the main output is
    // the pair itself when the plugin pairs the ports, else a separate pair (or a
    // mono scratch) copied back afterwards.
    if (!inPorts_.empty())
    {
        auto& main = inPorts_[0];
        if (mainInChannels_ == 2)
        {
            main.pointers[0] = left;
            main.pointers[1] = right;
        }
        else if (mainInChannels_ == 1)
        {
            for (size_t i = 0; i < n; ++i)
                monoIn_[i] = 0.5f * (left[i] + right[i]);
            main.pointers[0] = monoIn_.data();
        }
    }
    if (hasSidechain_)
    {
        if (keyLeft != nullptr && keyRight != nullptr)
        {
            std::copy(keyLeft, keyLeft + numSamples, keyLeft_.data());
            std::copy(keyRight, keyRight + numSamples, keyRight_.data());
        }
        else
        {
            std::fill(keyLeft_.begin(), keyLeft_.begin() + numSamples, 0.0f);
            std::fill(keyRight_.begin(), keyRight_.begin() + numSamples, 0.0f);
        }
        inPorts_[1].pointers[0] = keyLeft_.data();
        inPorts_[1].pointers[1] = keyRight_.data();
    }
    auto& mainOut = outPorts_[0];
    if (mainOutChannels_ == 2)
    {
        mainOut.pointers[0] = aliasMain_ ? left : outLeft_.data();
        mainOut.pointers[1] = aliasMain_ ? right : outRight_.data();
    }
    else
        mainOut.pointers[0] = outLeft_.data();
    for (size_t p = 0; p < inPorts_.size(); ++p)
    {
        auto& port = inPorts_[p];
        inBuffers_[p] =
            clap_audio_buffer_t{port.pointers.data(), nullptr, static_cast<uint32_t>(port.channels), 0, 0};
        // Scratch ports are silent: say so, so the plugin can skip them.
        if (!port.scratch.empty())
            inBuffers_[p].constant_mask = port.channels >= 64 ? ~0ull : ((1ull << port.channels) - 1);
    }
    for (size_t p = 0; p < outPorts_.size(); ++p)
        outBuffers_[p] = clap_audio_buffer_t{
            outPorts_[p].pointers.data(), nullptr, static_cast<uint32_t>(outPorts_[p].channels), 0, 0};

    clap_process_t process{};
    process.steady_time = -1;
    process.frames_count = static_cast<uint32_t>(numSamples);
    {
        // The song's position and tempo at the block's start (no loop reported).
        const auto& h = playHead_;
        auto& t = transport_;
        t = clap_event_transport_t{};
        t.header.size = sizeof(t);
        t.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        t.header.type = CLAP_EVENT_TRANSPORT;
        t.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
                  CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE |
                  (h.playing ? CLAP_TRANSPORT_IS_PLAYING : 0u);
        t.tempo = h.tempoBpm;
        t.song_pos_beats = static_cast<clap_beattime>(std::llround(h.quarterNotes * CLAP_BEATTIME_FACTOR));
        t.song_pos_seconds = static_cast<clap_sectime>(
            std::llround(static_cast<double>(h.samples) / h.sampleRate * CLAP_SECTIME_FACTOR));
        t.bar_start = static_cast<clap_beattime>(std::llround(h.barStartQuarters * CLAP_BEATTIME_FACTOR));
        t.bar_number = static_cast<int32_t>(h.bar);
        t.tsig_num = static_cast<uint16_t>(h.numerator);
        t.tsig_denom = static_cast<uint16_t>(h.denominator);
    }
    process.transport = &transport_;
    process.audio_inputs = inBuffers_.data();
    process.audio_inputs_count = static_cast<uint32_t>(inBuffers_.size());
    process.audio_outputs = outBuffers_.data();
    process.audio_outputs_count = static_cast<uint32_t>(outBuffers_.size());
    process.in_events = &in_.list;
    process.out_events = &out_.list;
    clap_process_status status = CLAP_PROCESS_ERROR;
    auto call = [&] { status = plugin_->process(plugin_, &process); };
    if (!callGuarded(call))
    {
        // It crashed: stop calling it; this block (and every later one) passes
        // the input through.
        crashed_.store(true, std::memory_order_release);
        std::copy(dryLeft_.begin(), dryLeft_.begin() + numSamples, left);
        std::copy(dryRight_.begin(), dryRight_.begin() + numSamples, right);
        return false;
    }
    if (status == CLAP_PROCESS_ERROR)
    {
        // Pass the input through untouched.
        std::copy(dryLeft_.begin(), dryLeft_.begin() + numSamples, left);
        std::copy(dryRight_.begin(), dryRight_.begin() + numSamples, right);
        return false;
    }
    // The plugin's output onto the chain's pair.
    if (mainOutChannels_ == 1)
    {
        std::copy(outLeft_.begin(), outLeft_.begin() + numSamples, left);
        std::copy(outLeft_.begin(), outLeft_.begin() + numSamples, right);
    }
    else if (!aliasMain_)
    {
        std::copy(outLeft_.begin(), outLeft_.begin() + numSamples, left);
        std::copy(outRight_.begin(), outRight_.begin() + numSamples, right);
    }

    // Bypass: a 10 ms crossfade to the delayed dry input, then that alone (the
    // plugin keeps processing so its state follows the audio, and the delay keeps
    // the graph's compensation right).
    const float target = isBypassed() ? 1.0f : 0.0f;
    const bool settled = bypassMix_ == target;
    if (settled && target == 1.0f)
    {
        for (size_t i = 0; i < n; ++i)
        {
            left[i] = delayedDry(i, false);
            right[i] = delayedDry(i, true);
        }
        return true;
    }
    if (!settled)
        for (size_t i = 0; i < n; ++i)
        {
            bypassMix_ = target > bypassMix_ ? std::min(target, bypassMix_ + bypassStep_)
                                             : std::max(target, bypassMix_ - bypassStep_);
            left[i] = left[i] * (1.0f - bypassMix_) + delayedDry(i, false) * bypassMix_;
            right[i] = right[i] * (1.0f - bypassMix_) + delayedDry(i, true) * bypassMix_;
        }
    return true;
}

// ----- main-thread housekeeping -----------------------------------------------------------------

void HostedClapPlugin::requestMainThread() noexcept
{
    // From the main thread: service soon (a message); from the audio thread: the
    // session's poll picks it up. The message checks the instance is still alive.
    callbackRequested_.store(true, std::memory_order_release);
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr && !rt::isRealtimeThread())
    {
        auto* instance = this;
        juce::MessageManager::callAsync(
            [instance]
            {
                if (isLive(instance))
                    instance->onMainThread();
            });
    }
}

void HostedClapPlugin::flushPendingOnMainThread()
{
    // Spec: params.flush is a main-thread call while the plugin is inactive, an
    // audio-thread call while active but not processing. The engine not running
    // is what guarantees the latter.
    if (params_ == nullptr)
        return;
    const Claim claim(owner_, mainThread);
    if (!claim.held)
        return; // a block is processing: it delivers the queue
    if (!activated_.load(std::memory_order_acquire))
    {
        collectEvents(nullptr);
        params_->flush(plugin_, &in_.list, &out_.list);
        return;
    }
    if (engineRunning_.load(std::memory_order_acquire))
        return;              // the next process() delivers the queue
    ensureProcessing(false); // the device stopped mid-run: leave the processing state first
    pretendAudioThread_.store(true, std::memory_order_release);
    collectEvents(nullptr);
    params_->flush(plugin_, &in_.list, &out_.list);
    pretendAudioThread_.store(false, std::memory_order_release);
}

void HostedClapPlugin::onMainThread()
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostedClapPlugin::onMainThread");
    if (plugin_ == nullptr)
        return;
    if (callbackRequested_.exchange(false, std::memory_order_acq_rel))
        plugin_->on_main_thread(plugin_);
    if (latencyDirty_.load(std::memory_order_acquire))
        refreshLatency();
    if (flushRequested_.load(std::memory_order_acquire))
        flushPendingOnMainThread();
    if (guiClosedByPlugin_.exchange(false, std::memory_order_acq_rel))
    {
        // The plugin closed its own window: without was_destroyed the GUI still
        // exists and the host must destroy it.
        if (editorOpen_.load(std::memory_order_acquire) && gui_ != nullptr &&
            !guiWasDestroyed_.load(std::memory_order_acquire))
            gui_->destroy(plugin_);
        editorOpen_.store(false, std::memory_order_release);
    }
}

// ----- state ------------------------------------------------------------------------------------

namespace
{
struct ByteSink
{
    std::vector<std::uint8_t> bytes;
    static int64_t write(const clap_ostream_t* stream, const void* buffer, uint64_t size) noexcept
    {
        auto* self = static_cast<ByteSink*>(stream->ctx);
        const auto* p = static_cast<const std::uint8_t*>(buffer);
        self->bytes.insert(self->bytes.end(), p, p + size);
        return static_cast<int64_t>(size);
    }
};

struct ByteSource
{
    const std::vector<std::uint8_t>* bytes = nullptr;
    size_t position = 0;
    static int64_t read(const clap_istream_t* stream, void* buffer, uint64_t size) noexcept
    {
        auto* self = static_cast<ByteSource*>(stream->ctx);
        const auto remaining = self->bytes->size() - self->position;
        const auto n = static_cast<size_t>(std::min<uint64_t>(size, remaining));
        std::memcpy(buffer, self->bytes->data() + self->position, n);
        self->position += n;
        return static_cast<int64_t>(n);
    }
};
} // namespace

std::string HostedClapPlugin::state() const
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostedClapPlugin::state");
    if (state_ == nullptr)
        return {};
    ByteSink sink;
    clap_ostream_t stream{&sink, &ByteSink::write};
    if (!state_->save(plugin_, &stream) || sink.bytes.empty())
        return {};
    return juce::Base64::toBase64(sink.bytes.data(), sink.bytes.size()).toStdString();
}

void HostedClapPlugin::setState(const std::string& state)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostedClapPlugin::setState");
    if (state_ == nullptr || state.empty())
        return;
    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, juce::String(state)))
        return;
    std::vector<std::uint8_t> bytes(static_cast<const std::uint8_t*>(decoded.getData()),
                                    static_cast<const std::uint8_t*>(decoded.getData()) +
                                        decoded.getDataSize());
    ByteSource source{&bytes, 0};
    clap_istream_t stream{&source, &ByteSource::read};
    state_->load(plugin_, &stream);
    // The plugin's values may have changed under the host's view.
    refreshValues();
}

// ----- presets ----------------------------------------------------------------------------------

bool HostedClapPlugin::loadPresetFile(const juce::File& file)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostedClapPlugin::loadPresetFile");
    const auto text = file.loadFileAsString();
    if (text.isEmpty())
        return false;
    setState(text.toStdString()); // the file holds the base64 state blob
    return true;
}

bool HostedClapPlugin::savePresetFile(const juce::File& file) const
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostedClapPlugin::savePresetFile");
    const auto blob = state(); // base64: plain ASCII
    return writePresetFile(file, blob.data(), blob.size());
}

// ----- editor -----------------------------------------------------------------------------------

bool HostedClapPlugin::openEditor(void* parentHwnd, double scale, int& width, int& height)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("HostedClapPlugin::openEditor");
    if (gui_ == nullptr || editorOpen_.load(std::memory_order_acquire))
        return false;
    if (!gui_->create(plugin_, CLAP_WINDOW_API_WIN32, false))
        return false;
    gui_->set_scale(plugin_, scale);
    uint32_t w = 0, h = 0;
    if (!gui_->get_size(plugin_, &w, &h) || w == 0 || h == 0)
    {
        w = 400;
        h = 300;
    }
    clap_window_t window{};
    window.api = CLAP_WINDOW_API_WIN32;
    window.win32 = parentHwnd;
    if (!gui_->set_parent(plugin_, &window))
    {
        gui_->destroy(plugin_);
        return false;
    }
    gui_->show(plugin_);
    guiClosedByPlugin_.store(false, std::memory_order_release);
    editorOpen_.store(true, std::memory_order_release);
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

void HostedClapPlugin::closeEditor()
{
    if (!editorOpen_.load(std::memory_order_acquire) || gui_ == nullptr)
        return;
    gui_->hide(plugin_);
    gui_->destroy(plugin_);
    editorOpen_.store(false, std::memory_order_release);
}

void HostedClapPlugin::editorResized(int width, int height)
{
    if (editorOpen_.load(std::memory_order_acquire) && gui_ != nullptr && width > 0 && height > 0)
        gui_->set_size(plugin_, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
}

bool HostedClapPlugin::takeResizeRequest(int& width, int& height)
{
    if (!resizeRequested_.exchange(false, std::memory_order_acq_rel))
        return false;
    width = static_cast<int>(requestedWidth_.load(std::memory_order_relaxed));
    height = static_cast<int>(requestedHeight_.load(std::memory_order_relaxed));
    return true;
}

bool HostedClapPlugin::editorIsResizable() const noexcept
{
    return gui_ != nullptr && editorOpen_.load(std::memory_order_acquire) && gui_->can_resize(plugin_);
}

// ----- host callbacks ---------------------------------------------------------------------------

const void* HostedClapPlugin::hostGetExtension(const clap_host_t* host, const char* id) noexcept
{
    auto& s = self(host);
    if (std::strcmp(id, CLAP_EXT_LOG) == 0)
        return &s.hostLog_;
    if (std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0)
        return &s.hostThreadCheck_;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &s.hostParams_;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0)
        return &s.hostState_;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0)
        return &s.hostLatency_;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
        return &s.hostAudioPorts_;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
        return &s.hostNotePorts_;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0)
        return &s.hostGui_;
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0)
        return &s.hostTimer_;
    return nullptr;
}

void HostedClapPlugin::hostRequestRestart(const clap_host_t* host) noexcept
{
    // Honoured by the insert cache: a fresh instance with this one's state takes
    // its place in the next snapshot (the real-time thread never sees a
    // deactivation).
    self(host).restartRequested_.store(true, std::memory_order_release);
}

void HostedClapPlugin::hostRequestProcess(const clap_host_t*) noexcept {}

void HostedClapPlugin::hostRequestCallback(const clap_host_t* host) noexcept
{
    self(host).requestMainThread();
}

void HostedClapPlugin::hostLog(const clap_host_t*, clap_log_severity, const char*) noexcept {}

bool HostedClapPlugin::hostIsMainThread(const clap_host_t*) noexcept
{
    const auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm == nullptr || mm->isThisTheMessageThread();
}

bool HostedClapPlugin::hostIsAudioThread(const clap_host_t* host) noexcept
{
    return rt::isRealtimeThread() || self(host).pretendAudioThread_.load(std::memory_order_acquire);
}

void HostedClapPlugin::hostParamsRescan(const clap_host_t* host, clap_param_rescan_flags flags) noexcept
{
    // Main thread (spec). While the plugin is active the audio thread may be
    // walking the parameter tables, so they are never reallocated then: values
    // and texts are refreshed in place, anything that changes the set of
    // parameters (or RESCAN_ALL, which is only legal when inactive) becomes a
    // restart, which the cache answers with a fresh instance.
    auto& s = self(host);
    if (!hostIsMainThread(host) || s.params_ == nullptr)
        return;
    if (!s.activated_.load(std::memory_order_acquire))
    {
        s.readParameters();
        return;
    }
    const bool sameCount = s.params_->count(s.plugin_) == static_cast<uint32_t>(s.paramInfos_.size());
    if ((flags & CLAP_PARAM_RESCAN_ALL) != 0 || !sameCount)
    {
        s.restartRequested_.store(true, std::memory_order_release);
        return;
    }
    if ((flags & (CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_TEXT)) != 0)
        for (uint32_t i = 0; i < static_cast<uint32_t>(s.paramInfos_.size()); ++i)
        {
            clap_param_info_t info{};
            if (!s.params_->get_info(s.plugin_, i, &info) || info.id != s.paramInfos_[i].id)
            {
                s.restartRequested_.store(true, std::memory_order_release); // the set changed after all
                return;
            }
            auto& stored = s.paramInfos_[i];
            std::memcpy(stored.name, info.name, sizeof(stored.name));
            std::memcpy(stored.module, info.module, sizeof(stored.module));
            stored.flags = info.flags;
            stored.min_value = info.min_value;
            stored.max_value = info.max_value;
            stored.default_value = info.default_value;
            stored.cookie = info.cookie;
            auto& p = s.infos_[i];
            p.name = info.name;
            p.minimum = static_cast<float>(info.min_value);
            p.maximum = static_cast<float>(info.max_value);
            p.defaultValue = static_cast<float>(info.default_value);
        }
    if ((flags & CLAP_PARAM_RESCAN_VALUES) != 0)
        s.refreshValues();
}

void HostedClapPlugin::hostParamsClear(const clap_host_t*, clap_id, clap_param_clear_flags) noexcept {}

void HostedClapPlugin::hostParamsRequestFlush(const clap_host_t* host) noexcept
{
    self(host).flushRequested_.store(true, std::memory_order_release);
    self(host).requestMainThread();
}

void HostedClapPlugin::hostStateMarkDirty(const clap_host_t* host) noexcept
{
    self(host).stateDirty_.store(true, std::memory_order_release);
}

void HostedClapPlugin::hostLatencyChanged(const clap_host_t* host) noexcept
{
    self(host).latencyDirty_.store(true, std::memory_order_release);
    self(host).requestMainThread();
}

bool HostedClapPlugin::hostAudioPortsIsRescanFlagSupported(const clap_host_t*, uint32_t) noexcept
{
    return false;
}

void HostedClapPlugin::hostAudioPortsRescan(const clap_host_t* host, uint32_t) noexcept
{
    // A port change needs a new negotiation: a fresh instance does it.
    self(host).restartRequested_.store(true, std::memory_order_release);
}

uint32_t HostedClapPlugin::hostNotePortsSupportedDialects(const clap_host_t*) noexcept
{
    return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
}

void HostedClapPlugin::hostNotePortsRescan(const clap_host_t*, uint32_t) noexcept {}

void HostedClapPlugin::hostGuiResizeHintsChanged(const clap_host_t*) noexcept {}

bool HostedClapPlugin::hostGuiRequestResize(const clap_host_t* host, uint32_t width, uint32_t height) noexcept
{
    auto& s = self(host);
    s.requestedWidth_.store(width, std::memory_order_relaxed);
    s.requestedHeight_.store(height, std::memory_order_relaxed);
    s.resizeRequested_.store(true, std::memory_order_release);
    return true;
}

bool HostedClapPlugin::hostGuiRequestShow(const clap_host_t*) noexcept
{
    return false;
}

bool HostedClapPlugin::hostGuiRequestHide(const clap_host_t*) noexcept
{
    return false;
}

void HostedClapPlugin::hostGuiClosed(const clap_host_t* host, bool wasDestroyed) noexcept
{
    auto& s = self(host);
    s.guiWasDestroyed_.store(wasDestroyed, std::memory_order_relaxed);
    s.guiClosedByPlugin_.store(true, std::memory_order_release);
    s.requestMainThread();
}

bool HostedClapPlugin::hostTimerRegister(const clap_host_t* host,
                                         uint32_t periodMs,
                                         clap_id* timerId) noexcept
{
    auto& s = self(host);
    if (!hostIsMainThread(host) || timerId == nullptr)
        return false;
    const clap_id id = s.nextTimerId_++;
    s.timers_[id] = std::make_unique<Timer>(s, id, periodMs);
    *timerId = id;
    return true;
}

bool HostedClapPlugin::hostTimerUnregister(const clap_host_t* host, clap_id timerId) noexcept
{
    return self(host).timers_.erase(timerId) > 0;
}

} // namespace northlight::hosting
