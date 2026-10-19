// A set of tiny CLAP plugins built alongside the tests so the host can be
// exercised without a third-party plugin. One file, five descriptors:
//
//   io.leafshade.aural.testgain           stereo in/out paired in place, a stereo
//                                         key input, a note input port, gain of
//                                         32 samples latency, a state of one double
//   io.leafshade.aural.testgain.quad      a four-channel main port (the host must
//                                         refuse it, never index past its buffers)
//   io.leafshade.aural.testgain.multiout  stereo main plus a second stereo output
//                                         port the plugin writes a constant into
//                                         (the host must drain it)
//   io.leafshade.aural.testgain.noinplace stereo in/out without an in-place pair:
//                                         the output is the input with its
//                                         channels swapped, written left first,
//                                         so an aliased buffer gives the wrong right
//   io.leafshade.aural.testgain.callback  a "Trigger" parameter: 1 asks the host
//                                         for a main-thread callback from process()
//                                         and, there, sets its gain to 0.5 and asks
//                                         for a rescan of values; 2 asks for a
//                                         restart from process(); 3 asks for a
//                                         rescan of everything from the callback
//
// The key input, when present and non-zero, replaces the audio (so a test can see
// what the host fed it); a note-on adds its velocity (0..1) to the left output at
// its sample and a note-off subtracts one. No GUI.

#include <clap/clap.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace
{
constexpr clap_id gainParam = 1;
constexpr clap_id triggerParam = 2;

struct Variant
{
    const char* id;
    const char* name;
    const char* description;
    int mainChannels; // channels of the main input and output ports
    bool keyPort;     // a stereo second input port
    int extraOutputs; // additional stereo output ports
    bool inPlace;     // the main ports are declared an in-place pair
    bool swap;        // the output is the input with the channels swapped (no delay)
    int latency;      // samples of delay the gain path adds
    bool trigger;     // the Trigger parameter exists
};

const Variant variants[] = {
    {"io.leafshade.aural.testgain",
     "Aural Test Gain",
     "A test effect for Aural's CLAP host",
     2,
     true,
     0,
     true,
     false,
     32,
     false},
    {"io.leafshade.aural.testgain.quad",
     "Aural Test Gain Quad",
     "A four-channel main port",
     4,
     false,
     0,
     true,
     false,
     32,
     false},
    {"io.leafshade.aural.testgain.multiout",
     "Aural Test Gain Multi",
     "A second output port",
     2,
     false,
     1,
     true,
     false,
     32,
     false},
    {"io.leafshade.aural.testgain.noinplace",
     "Aural Test Gain Swap",
     "No in-place pair; swaps channels",
     2,
     false,
     0,
     false,
     true,
     0,
     false},
    {"io.leafshade.aural.testgain.callback",
     "Aural Test Gain Callback",
     "Asks the host for callbacks",
     2,
     false,
     0,
     true,
     false,
     0,
     true},
};
constexpr uint32_t variantCount = sizeof(variants) / sizeof(variants[0]);

const char* const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_UTILITY, nullptr};

clap_plugin_descriptor_t descriptors[variantCount];

void fillDescriptors()
{
    for (uint32_t i = 0; i < variantCount; ++i)
    {
        auto& d = descriptors[i];
        d.clap_version = CLAP_VERSION_INIT;
        d.id = variants[i].id;
        d.name = variants[i].name;
        d.vendor = "Leafshade";
        d.url = "https://leafshade.io";
        d.manual_url = "";
        d.support_url = "";
        d.version = "1.0.0";
        d.description = variants[i].description;
        d.features = features;
    }
}

struct Plugin
{
    clap_plugin_t plugin{};
    const clap_host_t* host = nullptr;
    const Variant* variant = nullptr;
    double gain = 1.0;
    double trigger = 0.0;
    std::vector<std::vector<float>> delay; // per channel
    size_t delayPos = 0;
    bool processing = false;
    int mainThreadCalls = 0;

    static Plugin& self(const clap_plugin_t* p) { return *static_cast<Plugin*>(p->plugin_data); }
};

// ----- params -----------------------------------------------------------------------------------

uint32_t paramsCount(const clap_plugin_t* p)
{
    return Plugin::self(p).variant->trigger ? 2 : 1;
}

bool paramsGetInfo(const clap_plugin_t* p, uint32_t index, clap_param_info_t* info)
{
    if (index >= paramsCount(p))
        return false;
    *info = clap_param_info_t{};
    if (index == 0)
    {
        info->id = gainParam;
        info->flags = CLAP_PARAM_IS_AUTOMATABLE;
        std::strncpy(info->name, "Gain", sizeof(info->name) - 1);
        info->min_value = 0.0;
        info->max_value = 2.0;
        info->default_value = 1.0;
    }
    else
    {
        info->id = triggerParam;
        info->flags = CLAP_PARAM_IS_STEPPED;
        std::strncpy(info->name, "Trigger", sizeof(info->name) - 1);
        info->min_value = 0.0;
        info->max_value = 3.0;
        info->default_value = 0.0;
    }
    std::strncpy(info->module, "", sizeof(info->module) - 1);
    return true;
}

bool paramsGetValue(const clap_plugin_t* p, clap_id id, double* value)
{
    auto& self = Plugin::self(p);
    if (id == gainParam)
        *value = self.gain;
    else if (id == triggerParam && self.variant->trigger)
        *value = self.trigger;
    else
        return false;
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

void applyEvents(Plugin& self, const clap_input_events_t* in, bool fromProcess)
{
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; ++i)
    {
        const auto* header = in->get(in, i);
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->type != CLAP_EVENT_PARAM_VALUE)
            continue;
        const auto* e = reinterpret_cast<const clap_event_param_value_t*>(header);
        if (e->param_id == gainParam)
            self.gain = std::clamp(e->value, 0.0, 2.0);
        else if (e->param_id == triggerParam && self.variant->trigger)
        {
            self.trigger = e->value;
            if (fromProcess && self.trigger >= 2.0 && self.trigger < 3.0)
                self.host->request_restart(self.host);
            else if (fromProcess && self.trigger >= 1.0)
                self.host->request_callback(self.host);
        }
    }
}

void paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*)
{
    applyEvents(Plugin::self(p), in, false);
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

uint32_t latencyGet(const clap_plugin_t* p)
{
    return static_cast<uint32_t>(Plugin::self(p).variant->latency);
}

const clap_plugin_latency_t latencyExtension = {&latencyGet};

uint32_t audioPortsCount(const clap_plugin_t* p, bool isInput)
{
    const auto& v = *Plugin::self(p).variant;
    return isInput ? (v.keyPort ? 2u : 1u) : 1u + static_cast<uint32_t>(v.extraOutputs);
}

bool audioPortsGet(const clap_plugin_t* p, uint32_t index, bool isInput, clap_audio_port_info_t* info)
{
    const auto& v = *Plugin::self(p).variant;
    if (index >= audioPortsCount(p, isInput))
        return false;
    *info = clap_audio_port_info_t{};
    info->id = index;
    const bool main = index == 0;
    std::strncpy(info->name, main ? "Main" : (isInput ? "Key" : "Aux"), sizeof(info->name) - 1);
    info->flags = main ? CLAP_AUDIO_PORT_IS_MAIN : 0;
    info->channel_count = static_cast<uint32_t>(main ? v.mainChannels : 2);
    info->port_type = info->channel_count == 2 ? CLAP_PORT_STEREO : nullptr;
    info->in_place_pair = main && v.inPlace ? 0 : CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t audioPortsExtension = {&audioPortsCount, &audioPortsGet};

uint32_t notePortsCount(const clap_plugin_t*, bool isInput)
{
    return isInput ? 1 : 0;
}

bool notePortsGet(const clap_plugin_t*, uint32_t index, bool isInput, clap_note_port_info_t* info)
{
    if (!isInput || index != 0)
        return false;
    *info = clap_note_port_info_t{};
    info->id = 0;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    std::strncpy(info->name, "Notes", sizeof(info->name) - 1);
    return true;
}

const clap_plugin_note_ports_t notePortsExtension = {&notePortsCount, &notePortsGet};

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
    const auto channels = static_cast<size_t>(self.variant->mainChannels);
    self.delay.assign(channels,
                      std::vector<float>(static_cast<size_t>(std::max(1, self.variant->latency)), 0.0f));
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
    const auto& v = *self.variant;
    if (!self.processing)
        return CLAP_PROCESS_ERROR;
    applyEvents(self, process->in_events, true);
    const uint32_t n = process->frames_count;
    // The host must hand exactly the declared ports with their channel counts.
    if (process->audio_inputs_count != audioPortsCount(p, true) ||
        process->audio_outputs_count != audioPortsCount(p, false))
        return CLAP_PROCESS_ERROR;
    for (uint32_t i = 0; i < process->audio_inputs_count; ++i)
        if (process->audio_inputs[i].channel_count != (i == 0 ? static_cast<uint32_t>(v.mainChannels) : 2u) ||
            process->audio_inputs[i].data32 == nullptr)
            return CLAP_PROCESS_ERROR;
    for (uint32_t i = 0; i < process->audio_outputs_count; ++i)
        if (process->audio_outputs[i].channel_count !=
                (i == 0 ? static_cast<uint32_t>(v.mainChannels) : 2u) ||
            process->audio_outputs[i].data32 == nullptr)
            return CLAP_PROCESS_ERROR;
    float* const* in = process->audio_inputs[0].data32;
    float* const* out = process->audio_outputs[0].data32;
    const bool keyed =
        v.keyPort && process->audio_inputs_count >= 2 && process->audio_inputs[1].data32 != nullptr;
    const float* const* key = keyed ? process->audio_inputs[1].data32 : nullptr;
    const auto g = static_cast<float>(self.gain);
    const auto channels = static_cast<size_t>(v.mainChannels);
    if (v.swap)
    {
        // Left first, then right from the (hopefully untouched) left input.
        for (uint32_t i = 0; i < n; ++i)
            out[0][i] = in[1][i] * g;
        for (uint32_t i = 0; i < n; ++i)
            out[1][i] = in[0][i] * g;
    }
    else
    {
        const auto length = self.delay[0].size();
        for (uint32_t i = 0; i < n; ++i)
        {
            // Source: the key when any of it is non-zero in this block, else the audio.
            const bool useKey = keyed && (key[0][i] != 0.0f || key[1][i] != 0.0f);
            for (size_t c = 0; c < channels; ++c)
            {
                const float source = useKey ? key[c % 2][i] : in[c][i];
                if (v.latency > 0)
                {
                    const float delayed = self.delay[c][self.delayPos];
                    self.delay[c][self.delayPos] = source;
                    out[c][i] = delayed * g;
                }
                else
                    out[c][i] = source * g;
            }
            if (v.latency > 0)
                self.delayPos = (self.delayPos + 1) % length;
        }
    }
    // Extra outputs: a constant, so a host that forgot them would write to nothing.
    for (uint32_t port = 1; port < process->audio_outputs_count; ++port)
        for (uint32_t c = 0; c < process->audio_outputs[port].channel_count; ++c)
            std::fill(
                process->audio_outputs[port].data32[c], process->audio_outputs[port].data32[c] + n, 0.25f);
    // Notes mark the left output (both dialects).
    const auto* events = process->in_events;
    const uint32_t count = events->size(events);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* header = events->get(events, i);
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID || header->time >= n)
            continue;
        if (header->type == CLAP_EVENT_NOTE_ON)
            out[0][header->time] +=
                static_cast<float>(reinterpret_cast<const clap_event_note_t*>(header)->velocity);
        else if (header->type == CLAP_EVENT_NOTE_OFF)
            out[0][header->time] -= 1.0f;
        else if (header->type == CLAP_EVENT_MIDI)
        {
            const auto* midi = reinterpret_cast<const clap_event_midi_t*>(header);
            if ((midi->data[0] & 0xF0) == 0x90 && midi->data[2] > 0)
                out[0][header->time] += static_cast<float>(midi->data[2]) / 127.0f;
            else if ((midi->data[0] & 0xF0) == 0x80 || (midi->data[0] & 0xF0) == 0x90)
                out[0][header->time] -= 1.0f;
        }
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
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
        return &notePortsExtension;
    return nullptr;
}

void pluginOnMainThread(const clap_plugin_t* p)
{
    auto& self = Plugin::self(p);
    ++self.mainThreadCalls;
    if (!self.variant->trigger)
        return;
    const auto* hostParams =
        static_cast<const clap_host_params_t*>(self.host->get_extension(self.host, CLAP_EXT_PARAMS));
    if (self.trigger >= 3.0)
    {
        // Everything changed: the host must restart us (we are active).
        if (hostParams != nullptr)
            hostParams->rescan(self.host, CLAP_PARAM_RESCAN_ALL);
        return;
    }
    // The callback the plugin asked for: a value changed behind the host's back.
    self.gain = 0.5;
    if (hostParams != nullptr)
        hostParams->rescan(self.host, CLAP_PARAM_RESCAN_VALUES);
}

// ----- factory, entry ---------------------------------------------------------------------------

uint32_t factoryGetPluginCount(const clap_plugin_factory_t*)
{
    return variantCount;
}

const clap_plugin_descriptor_t* factoryGetPluginDescriptor(const clap_plugin_factory_t*, uint32_t index)
{
    return index < variantCount ? &descriptors[index] : nullptr;
}

const clap_plugin_t*
factoryCreatePlugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* id)
{
    if (!clap_version_is_compatible(host->clap_version))
        return nullptr;
    const Variant* variant = nullptr;
    for (uint32_t i = 0; i < variantCount; ++i)
        if (std::strcmp(id, variants[i].id) == 0)
            variant = &variants[i];
    if (variant == nullptr)
        return nullptr;
    auto* self = new Plugin();
    self->host = host;
    self->variant = variant;
    self->plugin.desc = &descriptors[static_cast<size_t>(variant - variants)];
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

// The entry point is initialised once per library load; a counter catches a
// host that initialises per instance (init twice without deinit fails).
int entryInits = 0;

bool entryInit(const char*)
{
    if (entryInits++ > 0)
        return false;
    fillDescriptors();
    return true;
}

void entryDeinit()
{
    --entryInits;
}

const void* entryGetFactory(const char* id)
{
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT, &entryInit, &entryDeinit, &entryGetFactory};
