// A tiny CLAP effect built alongside the tests so the host can be exercised
// without a third-party plugin: a stereo gain with one parameter, a latency of
// 32 samples (it delays by that), an optional stereo key input that, when
// present, replaces the audio (so a test can see what the host fed it), and a
// state of one double. No GUI.

#include <clap/clap.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
constexpr int latencyFrames = 32;
constexpr clap_id gainParam = 1;

struct Plugin
{
    clap_plugin_t plugin{};
    const clap_host_t* host = nullptr;
    double gain = 1.0;
    bool keyed = false;
    std::vector<float> delayL, delayR;
    size_t delayPos = 0;
    bool processing = false;

    static Plugin& self(const clap_plugin_t* p) { return *static_cast<Plugin*>(p->plugin_data); }
};

const char* const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

const clap_plugin_descriptor_t descriptor = {
    CLAP_VERSION_INIT,
    "io.leafshade.aural.testgain",
    "Aural Test Gain",
    "Leafshade",
    "https://leafshade.io",
    "",
    "",
    "1.0.0",
    "A test effect for Aural's CLAP host",
    features,
};

// ----- params -----------------------------------------------------------------------------------

uint32_t paramsCount(const clap_plugin_t*)
{
    return 1;
}

bool paramsGetInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info)
{
    if (index != 0)
        return false;
    *info = clap_param_info_t{};
    info->id = gainParam;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE;
    std::strncpy(info->name, "Gain", sizeof(info->name) - 1);
    std::strncpy(info->module, "", sizeof(info->module) - 1);
    info->min_value = 0.0;
    info->max_value = 2.0;
    info->default_value = 1.0;
    return true;
}

bool paramsGetValue(const clap_plugin_t* p, clap_id id, double* value)
{
    if (id != gainParam)
        return false;
    *value = Plugin::self(p).gain;
    return true;
}

bool paramsValueToText(const clap_plugin_t*, clap_id, double value, char* out, uint32_t size)
{
    std::snprintf(out, size, "%.2f", value);
    return true;
}

bool paramsTextToValue(const clap_plugin_t*, clap_id, const char* text, double* value)
{
    *value = std::atof(text);
    return true;
}

void applyEvents(Plugin& self, const clap_input_events_t* in)
{
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i)
    {
        const auto* header = in->get(in, i);
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_VALUE)
        {
            const auto* e = reinterpret_cast<const clap_event_param_value_t*>(header);
            if (e->param_id == gainParam)
                self.gain = std::clamp(e->value, 0.0, 2.0);
        }
    }
}

void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*)
{
    applyEvents(Plugin::self(p), in);
}

const clap_plugin_params_t paramsExtension = {
    &paramsCount, &paramsGetInfo, &paramsGetValue, &paramsValueToText, &paramsTextToValue, &paramsFlush};

// ----- state ------------------------------------------------------------------------------------

bool stateSave(const clap_plugin_t* p, const clap_ostream_t* stream)
{
    const double gain = Plugin::self(p).gain;
    return stream->write(stream, &gain, sizeof(gain)) == sizeof(gain);
}

bool stateLoad(const clap_plugin_t* p, const clap_istream_t* stream)
{
    double gain = 1.0;
    if (stream->read(stream, &gain, sizeof(gain)) != sizeof(gain))
        return false;
    Plugin::self(p).gain = std::clamp(gain, 0.0, 2.0);
    return true;
}

const clap_plugin_state_t stateExtension = {&stateSave, &stateLoad};

// ----- latency, ports ---------------------------------------------------------------------------

uint32_t latencyGet(const clap_plugin_t*)
{
    return latencyFrames;
}

const clap_plugin_latency_t latencyExtension = {&latencyGet};

uint32_t audioPortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 2 : 1; // main in, key in; main out
}

bool audioPortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    if (index >= (isInput ? 2u : 1u))
        return false;
    *info = clap_audio_port_info_t{};
    info->id = index;
    std::strncpy(info->name, index == 0 ? "Main" : "Key", sizeof(info->name) - 1);
    info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = index == 0 ? 0 : CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPortsExtension = {&audioPortsCount, &audioPortsGet};

// ----- plugin -----------------------------------------------------------------------------------

bool pluginInit(const clap_plugin_t*)
{
    return true;
}

void pluginDestroy(const clap_plugin_t* p)
{
    delete &Plugin::self(p);
}

bool pluginActivate(const clap_plugin_t* p, double, uint32_t, uint32_t)
{
    auto& self = Plugin::self(p);
    self.delayL.assign(latencyFrames, 0.0f);
    self.delayR.assign(latencyFrames, 0.0f);
    self.delayPos = 0;
    return true;
}

void pluginDeactivate(const clap_plugin_t*) {}

bool pluginStartProcessing(const clap_plugin_t* p)
{
    auto& self = Plugin::self(p);
    // Many plugins assert on the thread check; this one records the answer.
    if (const auto* check = static_cast<const clap_host_thread_check_t*>(
            self.host->get_extension(self.host, CLAP_EXT_THREAD_CHECK)))
        if (!check->is_audio_thread(self.host))
            return false;
    self.processing = true;
    return true;
}

void pluginStopProcessing(const clap_plugin_t* p)
{
    Plugin::self(p).processing = false;
}

void pluginReset(const clap_plugin_t*) {}

clap_process_status pluginProcess(const clap_plugin_t* p, const clap_process_t* process)
{
    auto& self = Plugin::self(p);
    if (!self.processing)
        return CLAP_PROCESS_ERROR;
    applyEvents(self, process->in_events);
    const uint32_t n = process->frames_count;
    if (process->audio_inputs_count < 1 || process->audio_outputs_count < 1)
        return CLAP_PROCESS_CONTINUE;
    float* const* in = process->audio_inputs[0].data32;
    float* const* out = process->audio_outputs[0].data32;
    const bool keyed = process->audio_inputs_count >= 2 && process->audio_inputs[1].data32 != nullptr;
    const float* const* key = keyed ? process->audio_inputs[1].data32 : nullptr;
    const auto g = static_cast<float>(self.gain);
    for (uint32_t i = 0; i < n; ++i)
    {
        // Source: the key when any of it is non-zero in this block, else the audio.
        const float sl = in[0][i];
        const float sr = in[1][i];
        float srcL = sl, srcR = sr;
        if (keyed && (key[0][i] != 0.0f || key[1][i] != 0.0f))
        {
            srcL = key[0][i];
            srcR = key[1][i];
        }
        const float dl = self.delayL[self.delayPos];
        const float dr = self.delayR[self.delayPos];
        self.delayL[self.delayPos] = srcL;
        self.delayR[self.delayPos] = srcR;
        self.delayPos = (self.delayPos + 1) % latencyFrames;
        out[0][i] = dl * g;
        out[1][i] = dr * g;
    }
    return CLAP_PROCESS_CONTINUE;
}

const void* pluginGetExtension(const clap_plugin_t*, const char* id)
{
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &paramsExtension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0)
        return &stateExtension;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0)
        return &latencyExtension;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
        return &audioPortsExtension;
    return nullptr;
}

void pluginOnMainThread(const clap_plugin_t*) {}

// ----- factory, entry ---------------------------------------------------------------------------

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*)
{
    return 1;
}

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
}

const clap_plugin_t*
factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* id)
{
    if (!clap_version_is_compatible(host->clap_version) || std::strcmp(id, descriptor.id) != 0)
        return nullptr;
    auto* self = new Plugin();
    self->host = host;
    self->plugin.desc = &descriptor;
    self->plugin.plugin_data = self;
    self->plugin.init = &pluginInit;
    self->plugin.destroy = &pluginDestroy;
    self->plugin.activate = &pluginActivate;
    self->plugin.deactivate = &pluginDeactivate;
    self->plugin.start_processing = &pluginStartProcessing;
    self->plugin.stop_processing = &pluginStopProcessing;
    self->plugin.reset = &pluginReset;
    self->plugin.process = &pluginProcess;
    self->plugin.get_extension = &pluginGetExtension;
    self->plugin.on_main_thread = &pluginOnMainThread;
    return &self->plugin;
}

const clap_plugin_factory_t factory = {
    &factoryGetPluginCount, &factoryGetPluginDescriptor, &factoryCreatePlugin};

bool entryInit(const char*)
{
    return true;
}

void entryDeinit() {}

const void* entryGetFactory(const char* id)
{
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT, &entryInit, &entryDeinit, &entryGetFactory};
