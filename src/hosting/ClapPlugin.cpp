#include "hosting/ClapPlugin.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <cstring>

namespace aural::hosting
{

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
    auto* self = static_cast<OutEvents*>(list->ctx);
    if (event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE &&
        self->owner != nullptr)
    {
        const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
        auto& owner = *self->owner;
        for (size_t i = 0; i < owner.paramInfos_.size(); ++i)
            if (owner.paramInfos_[i].id == value->param_id)
                owner.values_[i].store(static_cast<float>(value->value), std::memory_order_relaxed);
    }
    return true;
}

// ----- creation ---------------------------------------------------------------------------------

std::unique_ptr<HostedClapPlugin> HostedClapPlugin::create(const std::string& typeName,
                                                           const std::string& path,
                                                           const std::string& pluginId,
                                                           std::string& error)
{
    AURAL_ASSERT_NOT_REALTIME("HostedClapPlugin::create");
    std::unique_ptr<HostedClapPlugin> plugin(new HostedClapPlugin());
    plugin->typeName_ = typeName;
    if (!plugin->initialise(path, pluginId, error))
        return nullptr;
    return plugin;
}

HostedClapPlugin& HostedClapPlugin::self(const clap_host_t* host) noexcept
{
    return *static_cast<HostedClapPlugin*>(host->host_data);
}

bool HostedClapPlugin::initialise(const std::string& path, const std::string& pluginId, std::string& error)
{
    if (!library_.open(juce::String::fromUTF8(path.c_str())))
    {
        error = "could not load " + path;
        return false;
    }
    entry_ = static_cast<const clap_plugin_entry_t*>(library_.getFunction("clap_entry"));
    if (entry_ == nullptr)
    {
        error = "not a CLAP plugin (no clap_entry)";
        return false;
    }
    if (!clap_version_is_compatible(entry_->clap_version))
    {
        error = "incompatible CLAP version";
        return false;
    }
    if (!entry_->init(path.c_str()))
    {
        error = "the plugin's entry point refused to initialise";
        entry_ = nullptr;
        return false;
    }
    factory_ = static_cast<const clap_plugin_factory_t*>(entry_->get_factory(CLAP_PLUGIN_FACTORY_ID));
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
    host_.name = "Aural";
    host_.vendor = "Leafshade";
    host_.url = "https://leafshade.io";
    host_.version = "0.1";
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

    // Ports: the main stereo pair in and out, a second input port as the key.
    if (audioPorts_ != nullptr)
    {
        inputPorts_ = static_cast<int>(audioPorts_->count(plugin_, true));
        outputPorts_ = static_cast<int>(audioPorts_->count(plugin_, false));
        clap_audio_port_info_t info{};
        if (inputPorts_ >= 2 && audioPorts_->get(plugin_, 1, true, &info))
            hasSidechain_ = info.channel_count == 2;
    }
    readParameters();
    pending_ = std::make_unique<rt::SpscQueue<ParamChange>>(1024);
    in_.list.ctx = &in_;
    in_.list.size = &InEvents::size;
    in_.list.get = &InEvents::get;
    in_.events.resize(1024);
    out_.owner = this;
    out_.list.ctx = &out_;
    out_.list.try_push = &OutEvents::tryPush;
    return true;
}

HostedClapPlugin::~HostedClapPlugin()
{
    AURAL_ASSERT_NOT_REALTIME("HostedClapPlugin::~HostedClapPlugin");
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
    if (entry_ != nullptr)
        entry_->deinit();
}

void HostedClapPlugin::readParameters()
{
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
    for (size_t i = 0; i < paramInfos_.size(); ++i)
    {
        double value = paramInfos_[i].default_value;
        params_->get_value(plugin_, paramInfos_[i].id, &value);
        values_[i].store(static_cast<float>(value), std::memory_order_relaxed);
    }
}

// ----- lifecycle --------------------------------------------------------------------------------

void HostedClapPlugin::prepare(double sampleRate, int maxBlockSize)
{
    AURAL_ASSERT_NOT_REALTIME("HostedClapPlugin::prepare");
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
    inChannels0_.assign(2, nullptr);
    inChannels1_ = {keyLeft_.data(), keyRight_.data()};
    outChannels0_.assign(2, nullptr);
    bypassStep_ = 1.0f / static_cast<float>(std::max(1.0, sampleRate * 0.010));
    bypassMix_ = isBypassed() ? 1.0f : 0.0f;
    if (!plugin_->activate(plugin_, sampleRate, 1, static_cast<uint32_t>(maxBlock_)))
        return; // stays inactive: process() passes audio through
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
    pending_->push(ParamChange{info.id, info.cookie, clamped});
    if (!processing_.load(std::memory_order_acquire))
        flushRequested_.store(true, std::memory_order_release);
}

float HostedClapPlugin::parameter(int index) const noexcept
{
    if (index < 0 || static_cast<size_t>(index) >= values_.size())
        return 0.0f;
    return values_[static_cast<size_t>(index)].load(std::memory_order_relaxed);
}

void HostedClapPlugin::runEvents(const clap_input_events_t& in, const clap_output_events_t& out) noexcept
{
    (void)in;
    (void)out;
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

bool HostedClapPlugin::run(
    float* left, float* right, const float* keyLeft, const float* keyRight, int numSamples) noexcept
{
    if (!activated_.load(std::memory_order_acquire) || numSamples > maxBlock_)
        return false;
    ensureProcessing(true);

    // Pending host parameter changes become sample-0 events of this block.
    in_.count = 0;
    ParamChange change;
    while (in_.count < in_.events.size() && pending_->pop(change))
    {
        auto& e = in_.events[in_.count++];
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

    const float target = isBypassed() ? 1.0f : 0.0f;
    const bool settled = bypassMix_ == target;
    if (!settled || target == 1.0f)
    {
        std::copy(left, left + numSamples, dryLeft_.data());
        std::copy(right, right + numSamples, dryRight_.data());
    }

    // Buffers: in-place on the chain's pair, the key on port 1 when present.
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
    inChannels0_[0] = left;
    inChannels0_[1] = right;
    outChannels0_[0] = left;
    outChannels0_[1] = right;
    inputs_[0] = clap_audio_buffer_t{inChannels0_.data(), nullptr, 2, 0, 0};
    inputs_[1] = clap_audio_buffer_t{inChannels1_.data(), nullptr, 2, 0, 0};
    outputs_[0] = clap_audio_buffer_t{outChannels0_.data(), nullptr, 2, 0, 0};

    clap_process_t process{};
    process.steady_time = -1;
    process.frames_count = static_cast<uint32_t>(numSamples);
    process.transport = nullptr;
    process.audio_inputs = inputs_;
    process.audio_inputs_count = static_cast<uint32_t>(std::min(inputPorts_, hasSidechain_ ? 2 : 1));
    process.audio_outputs = outputs_;
    process.audio_outputs_count = outputPorts_ > 0 ? 1 : 0;
    process.in_events = &in_.list;
    process.out_events = &out_.list;
    const auto status = plugin_->process(plugin_, &process);
    if (status == CLAP_PROCESS_ERROR)
    {
        std::copy(dryLeft_.begin(), dryLeft_.begin() + numSamples, left);
        std::copy(dryRight_.begin(), dryRight_.begin() + numSamples, right);
        return false;
    }

    // Bypass: a 10 ms crossfade to the dry input, then the dry input alone (the
    // plugin keeps processing so its state follows the audio).
    if (settled && target == 1.0f)
    {
        std::copy(dryLeft_.begin(), dryLeft_.begin() + numSamples, left);
        std::copy(dryRight_.begin(), dryRight_.begin() + numSamples, right);
        return true;
    }
    if (!settled)
        for (int i = 0; i < numSamples; ++i)
        {
            bypassMix_ = target > bypassMix_ ? std::min(target, bypassMix_ + bypassStep_)
                                             : std::max(target, bypassMix_ - bypassStep_);
            left[i] = left[i] * (1.0f - bypassMix_) + dryLeft_[static_cast<size_t>(i)] * bypassMix_;
            right[i] = right[i] * (1.0f - bypassMix_) + dryRight_[static_cast<size_t>(i)] * bypassMix_;
        }
    return true;
}

// ----- main-thread housekeeping -----------------------------------------------------------------

void HostedClapPlugin::onMainThread()
{
    AURAL_ASSERT_NOT_REALTIME("HostedClapPlugin::onMainThread");
    if (plugin_ == nullptr)
        return;
    if (callbackRequested_.exchange(false, std::memory_order_acq_rel))
        plugin_->on_main_thread(plugin_);
    if (latencyDirty_.load(std::memory_order_acquire))
        refreshLatency();
    if (flushRequested_.exchange(false, std::memory_order_acq_rel) && params_ != nullptr &&
        !processing_.load(std::memory_order_acquire))
    {
        // Not processing: deliver the pending changes through params.flush.
        in_.count = 0;
        ParamChange change;
        while (in_.count < in_.events.size() && pending_->pop(change))
        {
            auto& e = in_.events[in_.count++];
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
        params_->flush(plugin_, &in_.list, &out_.list);
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
    AURAL_ASSERT_NOT_REALTIME("HostedClapPlugin::state");
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
    AURAL_ASSERT_NOT_REALTIME("HostedClapPlugin::setState");
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
    for (size_t i = 0; i < paramInfos_.size() && params_ != nullptr; ++i)
    {
        double value = 0.0;
        if (params_->get_value(plugin_, paramInfos_[i].id, &value))
            values_[i].store(static_cast<float>(value), std::memory_order_relaxed);
    }
}

// ----- presets ----------------------------------------------------------------------------------

juce::File HostedClapPlugin::userPresetFolder() const
{
    const auto safe = [](const std::string& s)
    { return juce::File::createLegalFileName(juce::String::fromUTF8(s.c_str())).trim(); };
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("CLAP Presets")
        .getChildFile(safe(vendor_).isEmpty() ? "Unknown" : safe(vendor_))
        .getChildFile(safe(name_).isEmpty() ? "Plugin" : safe(name_));
}

std::vector<juce::File> HostedClapPlugin::userPresetFiles() const
{
    std::vector<juce::File> files;
    const auto folder = userPresetFolder();
    if (!folder.isDirectory())
        return files;
    for (const auto& f :
         folder.findChildFiles(juce::File::findFiles, true, juce::String("*.") + presetExtension))
        files.push_back(f);
    std::sort(files.begin(),
              files.end(),
              [](const juce::File& a, const juce::File& b)
              { return a.getFullPathName().compareIgnoreCase(b.getFullPathName()) < 0; });
    return files;
}

bool HostedClapPlugin::loadPresetFile(const juce::File& file)
{
    AURAL_ASSERT_NOT_REALTIME("HostedClapPlugin::loadPresetFile");
    const auto text = file.loadFileAsString();
    if (text.isEmpty())
        return false;
    setState(text.toStdString()); // the file holds the base64 state blob
    return true;
}

bool HostedClapPlugin::savePresetFile(const juce::File& file) const
{
    AURAL_ASSERT_NOT_REALTIME("HostedClapPlugin::savePresetFile");
    const auto blob = state();
    if (blob.empty())
        return false;
    file.getParentDirectory().createDirectory();
    return file.replaceWithText(juce::String(blob));
}

// ----- editor -----------------------------------------------------------------------------------

bool HostedClapPlugin::openEditor(void* parentHwnd, double scale, int& width, int& height)
{
    AURAL_ASSERT_NOT_REALTIME("HostedClapPlugin::openEditor");
    if (gui_ == nullptr || editorOpen_)
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
    editorOpen_ = true;
    width = static_cast<int>(w);
    height = static_cast<int>(h);
    return true;
}

void HostedClapPlugin::closeEditor()
{
    if (!editorOpen_ || gui_ == nullptr)
        return;
    gui_->hide(plugin_);
    gui_->destroy(plugin_);
    editorOpen_ = false;
}

void HostedClapPlugin::editorResized(int width, int height)
{
    if (editorOpen_ && gui_ != nullptr && width > 0 && height > 0)
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
    return gui_ != nullptr && gui_->can_resize(plugin_);
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
    self(host).restartRequested_.store(true, std::memory_order_release);
    self(host).callbackRequested_.store(true, std::memory_order_release);
}

void HostedClapPlugin::hostRequestProcess(const clap_host_t*) noexcept {}

void HostedClapPlugin::hostRequestCallback(const clap_host_t* host) noexcept
{
    auto& s = self(host);
    s.callbackRequested_.store(true, std::memory_order_release);
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr && !rt::isRealtimeThread())
    {
        juce::MessageManager::callAsync([&s] { s.onMainThread(); });
    }
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

void HostedClapPlugin::hostParamsRescan(const clap_host_t* host, clap_param_rescan_flags) noexcept
{
    auto& s = self(host);
    if (hostIsMainThread(host))
        s.readParameters();
}

void HostedClapPlugin::hostParamsClear(const clap_host_t*, clap_id, clap_param_clear_flags) noexcept {}

void HostedClapPlugin::hostParamsRequestFlush(const clap_host_t* host) noexcept
{
    self(host).flushRequested_.store(true, std::memory_order_release);
    hostRequestCallback(host);
}

void HostedClapPlugin::hostStateMarkDirty(const clap_host_t*) noexcept {}

void HostedClapPlugin::hostLatencyChanged(const clap_host_t* host) noexcept
{
    self(host).latencyDirty_.store(true, std::memory_order_release);
    hostRequestCallback(host);
}

bool HostedClapPlugin::hostAudioPortsIsRescanFlagSupported(const clap_host_t*, uint32_t) noexcept
{
    return false;
}

void HostedClapPlugin::hostAudioPortsRescan(const clap_host_t*, uint32_t) noexcept {}

uint32_t HostedClapPlugin::hostNotePortsSupportedDialects(const clap_host_t*) noexcept
{
    return 0;
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

void HostedClapPlugin::hostGuiClosed(const clap_host_t* host, bool) noexcept
{
    self(host).editorOpen_ = false;
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

} // namespace aural::hosting
