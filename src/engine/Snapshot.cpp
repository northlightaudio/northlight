#include "engine/Snapshot.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>

namespace northlight::engine
{

std::shared_ptr<const AudioSource> MediaCache::get(const std::string& path, double sampleRate)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("MediaCache::get");
    if (const auto it = sources_.find(path); it != sources_.end())
    {
        if (it->second->sampleRate() == sampleRate)
            return it->second;
        sources_.erase(it);
    }

    std::string error;
    auto source = loader_.load(path, sampleRate, error);
    if (source == nullptr)
    {
        errors_[path] = error.empty() ? "could not load" : error;
        return nullptr;
    }
    errors_.erase(path);
    sources_[path] = source;
    return source;
}

std::shared_ptr<const AudioSource>
MediaCache::getForClip(const std::string& path, double sampleRate, core::Id clipId)
{
    if (const auto it = instances_.find(clipId); it != instances_.end())
    {
        if (it->second.path == path && it->second.sampleRate == sampleRate)
            return it->second.source;
        instances_.erase(it);
    }
    auto shared = get(path, sampleRate);
    if (shared == nullptr)
        return nullptr;
    auto instance = shared->createInstance();
    if (instance == nullptr)
        return shared;
    instances_[clipId] = Instance{path, sampleRate, instance};
    return instance;
}

std::shared_ptr<const AudioSource> MediaCache::sourceForClip(const core::AudioClip& clip, double sampleRate)
{
    if (const auto it = clipSources_.find(clip.id); it != clipSources_.end() &&
                                                    it->second.sampleRate == sampleRate &&
                                                    it->second.clip == clip && it->second.source != nullptr)
        return it->second.source;
    auto base = getForClip(clip.mediaPath, sampleRate, clip.id);
    std::shared_ptr<const AudioSource> source;
    if (clip.isStretched())
        source = stretchHook_ && base != nullptr ? stretchHook_(clip, sampleRate, std::move(base)) : nullptr;
    else if (clipHook_ && base != nullptr && clip.vocal.active())
        source = clipHook_(clip, sampleRate, std::move(base));
    else
        source = std::move(base);
    if (source != nullptr)
        clipSources_[clip.id] = ClipSource{clip, sampleRate, source};
    else
        clipSources_.erase(clip.id);
    return source;
}

void MediaCache::adopt(const std::string& path, std::shared_ptr<const AudioSource> source)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("MediaCache::adopt");
    if (source == nullptr)
        return;
    errors_.erase(path);
    sources_[path] = std::move(source);
}

void MediaCache::prune()
{
    std::erase_if(clipSources_, [](const auto& entry) { return entry.second.source.use_count() == 1; });
    std::erase_if(instances_, [](const auto& entry) { return entry.second.source.use_count() == 1; });
    std::erase_if(sources_, [](const auto& entry) { return entry.second.use_count() == 1; });
}

namespace
{
// Routing graph over channel indices: tracks are 0..T-1, buses T..T+B-1, master is -1.
struct Routing
{
    int trackCount = 0;
    std::vector<int> output;                   // per channel: bus index or -1 (master)
    std::vector<std::vector<int>> sendTargets; // per channel: bus indices
    std::vector<bool> solo, mute;

    int channelOfBus(int bus) const noexcept { return trackCount + bus; }
    int busOfChannel(int channel) const noexcept { return channel - trackCount; }

    // Every bus on the chain downstream of `channel` (through output and sends).
    void downstream(int channel, std::vector<bool>& visited) const
    {
        const auto visit = [&](int bus)
        {
            if (bus < 0 || visited[static_cast<size_t>(bus)])
                return;
            visited[static_cast<size_t>(bus)] = true;
            downstream(channelOfBus(bus), visited);
        };
        visit(output[static_cast<size_t>(channel)]);
        for (int bus : sendTargets[static_cast<size_t>(channel)])
            visit(bus);
    }
};
} // namespace

namespace
{
int chaseKeyOf(const core::MidiControllerEvent& e) noexcept
{
    switch (e.type)
    {
        case core::MidiControllerType::controller:
            return std::clamp(e.number, 0, 127);
        case core::MidiControllerType::pitchBend:
            return chaseKeyBend;
        case core::MidiControllerType::channelPressure:
            return chaseKeyPressure;
        case core::MidiControllerType::polyPressure:
            return -1;
    }
    return -1;
}

// A clip's controller events at their timeline places: those inside the clip
// in time order, preceded (at the clip's start) by the latest value of each
// controller that a trim left before it.
void compileControllers(const core::MidiClip& source,
                        MidiClipNode& clip,
                        std::array<std::uint8_t, chaseKeyCount>& usedKeys)
{
    const auto toNode = [&](const core::MidiControllerEvent& e)
    {
        MidiControllerNode n;
        n.time = source.start + std::max<core::SamplePosition>(0, e.time);
        n.type = e.type;
        n.number = static_cast<std::uint8_t>(std::clamp(e.number, 0, 127));
        n.value = static_cast<std::int16_t>(
            std::clamp(e.value, core::midiControllerMinimum(e.type), core::midiControllerMaximum(e.type)));
        n.key = static_cast<std::int16_t>(chaseKeyOf(e));
        return n;
    };
    std::array<int, chaseKeyCount> before;
    before.fill(-1);
    for (size_t i = 0; i < source.controllers.size(); ++i)
    {
        const auto& e = source.controllers[i];
        if (e.time < 0)
        {
            if (const int key = chaseKeyOf(e); key >= 0)
                before[static_cast<size_t>(key)] = static_cast<int>(i);
        }
    }
    for (const int i : before)
        if (i >= 0)
            clip.controllers.push_back(toNode(source.controllers[static_cast<size_t>(i)]));
    for (const auto& e : source.controllers)
        if (e.time >= 0 && e.time < source.length)
            clip.controllers.push_back(toNode(e));
    std::stable_sort(clip.controllers.begin(),
                     clip.controllers.end(),
                     [](const MidiControllerNode& a, const MidiControllerNode& b)
                     { return a.time < b.time; });
    std::array<int, chaseKeyCount> last;
    last.fill(-1);
    for (size_t i = 0; i < clip.controllers.size(); ++i)
        if (const int key = clip.controllers[i].key; key >= 0)
            last[static_cast<size_t>(key)] = static_cast<int>(i);
    for (int key = 0; key < chaseKeyCount; ++key)
        if (const int i = last[static_cast<size_t>(key)]; i >= 0)
        {
            clip.lastValues.push_back(clip.controllers[static_cast<size_t>(i)]);
            usedKeys[static_cast<size_t>(key)] = 1;
        }
    clip.chasedKeys = static_cast<int>(clip.lastValues.size());
}
} // namespace

std::unique_ptr<Snapshot> compileSnapshot(const core::Project& project,
                                          MediaCache& media,
                                          InsertCache& inserts,
                                          DelayCache* delays,
                                          RuntimeCache* runtimes,
                                          const std::function<int(const std::string&)>& midiSources,
                                          const InputBusResolver& inputBuses,
                                          int blockSamples)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("compileSnapshot");
    auto snapshot = std::make_unique<Snapshot>();
    snapshot->sampleRate = project.sampleRate;
    snapshot->tempoBpm = project.tempoBpm;
    snapshot->timeSignature = project.timeSignature;
    snapshot->tempoMap = core::tempoMapOf(project);
    snapshot->masterGain = project.master.gain;
    RuntimeCache localRuntimes;
    RuntimeCache& runtimeCache = runtimes != nullptr ? *runtimes : localRuntimes;
    snapshot->keepAliveMaster = runtimeCache.master();
    snapshot->master = snapshot->keepAliveMaster.get();
    snapshot->master->targetGain.store(project.master.gain, std::memory_order_relaxed);
    snapshot->masterMeterSlot = masterMeterSlot(project);
    // The work buffers hold the engine's prepared block, not the most it could be
    // (twelve per channel: a 100-track project's at 8192 were 39 MB).
    snapshot->blockSamples = std::clamp(blockSamples, 1, Snapshot::maxBlockSamples);
    snapshot->silence.assign(static_cast<size_t>(snapshot->blockSamples), 0.0f);

    const auto trackCount = static_cast<int>(project.tracks.size());
    const auto busCount = static_cast<int>(project.buses.size());
    const auto n = static_cast<size_t>(snapshot->blockSamples);

    // ----- routing graph, with bus cycles broken by dropping the offending edge -----
    Routing routing;
    routing.trackCount = trackCount;
    routing.output.assign(static_cast<size_t>(trackCount + busCount), -1);
    routing.sendTargets.resize(static_cast<size_t>(trackCount + busCount));
    routing.solo.resize(static_cast<size_t>(trackCount + busCount));
    routing.mute.resize(static_cast<size_t>(trackCount + busCount));

    // Sidechain edges between buses order them like sends do; a key source that
    // would form a cycle is dropped at compile time (the insert runs unkeyed).
    std::vector<std::vector<int>> busKeySources(static_cast<size_t>(busCount)); // per bus: source bus indices
    for (int b = 0; b < busCount; ++b)
        for (const auto& i : project.buses[static_cast<size_t>(b)].inserts)
            if (const int source = project.busIndex(i.sidechain); source >= 0 && source != b)
                busKeySources[static_cast<size_t>(b)].push_back(source);

    const auto channelOf = [&](int index, const core::Channel& c)
    {
        routing.output[static_cast<size_t>(index)] = project.busIndex(c.outputBus);
        for (const auto& s : c.sends)
        {
            const int bus = project.busIndex(s.bus);
            if (bus >= 0)
                routing.sendTargets[static_cast<size_t>(index)].push_back(bus);
        }
        routing.solo[static_cast<size_t>(index)] = c.solo;
        routing.mute[static_cast<size_t>(index)] = c.mute;
    };
    for (int t = 0; t < trackCount; ++t)
    {
        const auto& track = project.tracks[static_cast<size_t>(t)];
        channelOf(t, track);
        routing.solo[static_cast<size_t>(t)] = track.solo || project.soloedByFolder(track);
        routing.mute[static_cast<size_t>(t)] = track.mute || project.mutedByFolder(track) || track.isFolder();
    }
    for (int b = 0; b < busCount; ++b)
        channelOf(trackCount + b, project.buses[static_cast<size_t>(b)]);

    // A bus edge (output or send) that would reach back to its source is dropped:
    // the output falls back to the master, the send is removed. Buses are checked
    // in project order, so in a cycle the earlier bus loses its forward edge.
    const auto reaches = [&](int fromBus, int target) -> bool
    {
        std::vector<bool> visited(static_cast<size_t>(busCount), false);
        routing.downstream(routing.channelOfBus(target), visited);
        return visited[static_cast<size_t>(fromBus)]; // copied out before `visited` dies
    };
    for (int b = 0; b < busCount; ++b)
    {
        const auto channel = static_cast<size_t>(routing.channelOfBus(b));
        if (routing.output[channel] == b ||
            (routing.output[channel] >= 0 && reaches(b, routing.output[channel])))
            routing.output[channel] = -1;
        std::erase_if(routing.sendTargets[channel],
                      [&](int target) { return target == b || reaches(b, target); });
    }
    // A key source bus the keyed bus feeds (directly or through others) would have
    // to render both before and after it: drop that key.
    for (int b = 0; b < busCount; ++b)
        std::erase_if(busKeySources[static_cast<size_t>(b)], [&](int source) { return reaches(source, b); });

    // Bus order: feeders before the buses they feed (Kahn on the now-acyclic graph).
    {
        std::vector<int> indegree(static_cast<size_t>(busCount), 0);
        for (int b = 0; b < busCount; ++b)
        {
            const auto channel = static_cast<size_t>(routing.channelOfBus(b));
            if (routing.output[channel] >= 0)
                ++indegree[static_cast<size_t>(routing.output[channel])];
            for (int target : routing.sendTargets[channel])
                ++indegree[static_cast<size_t>(target)];
            indegree[static_cast<size_t>(b)] +=
                static_cast<int>(busKeySources[static_cast<size_t>(b)].size());
        }
        std::vector<int> ready;
        for (int b = 0; b < busCount; ++b)
            if (indegree[static_cast<size_t>(b)] == 0)
                ready.push_back(b);
        while (!ready.empty())
        {
            const int b = ready.back();
            ready.pop_back();
            snapshot->busOrder.push_back(b);
            const auto channel = static_cast<size_t>(routing.channelOfBus(b));
            const auto relax = [&](int target)
            {
                if (--indegree[static_cast<size_t>(target)] == 0)
                    ready.push_back(target);
            };
            if (routing.output[channel] >= 0)
                relax(routing.output[channel]);
            for (int target : routing.sendTargets[channel])
                relax(target);
            for (int keyed = 0; keyed < busCount; ++keyed)
                for (int source : busKeySources[static_cast<size_t>(keyed)])
                    if (source == b)
                        relax(keyed);
        }
        // Any bus left out (should not happen after cycle breaking) renders last.
        for (int b = 0; b < busCount; ++b)
            if (std::find(snapshot->busOrder.begin(), snapshot->busOrder.end(), b) ==
                snapshot->busOrder.end())
                snapshot->busOrder.push_back(b);
    }

    // ----- solo-in-place --------------------------------------------------------------
    // With any solo, a channel's direct output is heard if it is soloed, is fed by a
    // soloed channel (downstream), or feeds a lit bus through its output. Its sends
    // are heard if any of that holds or a send of its reaches a lit bus: a channel
    // lit only by a send to a soloed reverb keeps the send and loses the dry output.
    // Mute always wins. Buses resolve fed-first (reverse order), so a bus's lit
    // state is known before the channels that feed it.
    const bool anySolo = std::any_of(routing.solo.begin(), routing.solo.end(), [](bool s) { return s; });
    std::vector<bool> outputLit(static_cast<size_t>(trackCount + busCount), !anySolo);
    std::vector<bool> anyLit(static_cast<size_t>(trackCount + busCount), !anySolo);
    if (anySolo)
    {
        std::vector<bool> downstreamOfSolo(static_cast<size_t>(busCount), false);
        for (int c = 0; c < trackCount + busCount; ++c)
            if (routing.solo[static_cast<size_t>(c)])
                routing.downstream(c, downstreamOfSolo);
        // reach[c]: a soloed channel sits downstream of c (through outputs or sends).
        std::vector<bool> reach(static_cast<size_t>(trackCount + busCount), false);
        const auto resolve = [&](int c)
        {
            const auto index = static_cast<size_t>(c);
            const int output = routing.output[index];
            const bool outputReaches =
                output >= 0 && reach[static_cast<size_t>(routing.channelOfBus(output))];
            bool sendReaches = false;
            for (int target : routing.sendTargets[index])
                sendReaches = sendReaches || reach[static_cast<size_t>(routing.channelOfBus(target))];
            reach[index] = routing.solo[index] || outputReaches || sendReaches;
            const bool self =
                routing.solo[index] ||
                (c >= trackCount && downstreamOfSolo[static_cast<size_t>(routing.busOfChannel(c))]);
            outputLit[index] = self || outputReaches;
            anyLit[index] = self || outputReaches || sendReaches;
        };
        for (auto it = snapshot->busOrder.rbegin(); it != snapshot->busOrder.rend(); ++it)
            resolve(routing.channelOfBus(*it));
        for (int t = 0; t < trackCount; ++t)
            resolve(t);
    }

    DelayCache localDelays; // used only when the caller keeps no cache
    // The latency a channel's signal has gathered through each insert of its
    // chain, that insert included: an instrument's extra outputs leave there.
    std::map<core::Id, int> latencyThrough;
    // Automation: the lanes with points, when the mode reads. An insert
    // parameter plays only on a processor whose parameters the model owns
    // (built-ins and the strip; a hosted plugin owns its own) and that is in the
    // node's chain, so the chain is filled first.
    const auto compileAutomation = [&](ChannelNode& node, const core::Channel& c)
    {
        const bool reads = c.automationMode == core::AutomationMode::read ||
                           c.automationMode == core::AutomationMode::touch ||
                           c.automationMode == core::AutomationMode::latch;
        for (const auto& lane : c.automation)
        {
            if (!reads || lane.points.empty())
                continue;
            AutomationNode a;
            a.target = lane.target;
            a.parameter = lane.parameter;
            if (lane.target == core::AutomationTarget::insertParameter)
            {
                const auto instance = inserts.find(lane.insert);
                if (instance == nullptr || instance->ownsParameters() || lane.parameter < 0 ||
                    lane.parameter >= static_cast<int>(instance->parameters().size()) ||
                    std::none_of(
                        node.inserts.begin(),
                        node.inserts.end(),
                        [&](const InsertSlot& s) { return s.processor == instance.get(); }))
                    continue;
                a.processor = instance.get();
            }
            a.points = lane.points;
            auto runtime = runtimeCache.automation(lane.id);
            a.runtime = runtime.get();
            node.keepAliveAutomation.push_back(std::move(runtime));
            node.automation.push_back(std::move(a));
        }
    };
    // One slot of a chain: the processor, its mix and the dry path a latent slot
    // needs. The key source is the caller's (the master's take none).
    const auto compileSlot =
        [&](ChannelNode& node, const core::Insert& i, std::shared_ptr<InsertProcessor> instance)
    {
        // Latency counts whether or not the slot is bypassed: a bypassed
        // processor still delays by it (plugins through processBlockBypassed,
        // the limiter through its lookahead), so toggling bypass never
        // re-solves the graph.
        const int latency = std::max(0, instance->latencySamples());
        node.chainLatency += latency;
        InsertSlot s;
        s.processor = instance.get();
        s.id = i.id;
        auto mix = runtimeCache.insertMix(i.id);
        mix->targetMix.store(std::clamp(i.mix, 0.0f, 1.0f), std::memory_order_relaxed);
        s.mix = mix.get();
        node.keepAliveMixes.push_back(std::move(mix));
        s.inputLatency = node.chainLatency - latency;
        // A latent slot keeps its dry copy held back by the insert's latency, so
        // the two halves line up whatever the mix is moved to without a recompile.
        if (latency > 0)
        {
            auto line = (delays != nullptr ? *delays : localDelays)
                            .get(DelayKey{i.id, core::invalidId, true}, latency);
            s.dry = line.get();
            snapshot->allDelays.push_back(s.dry);
            node.keepAliveDelays.push_back(std::move(line));
        }
        node.inserts.push_back(s);
        node.keepAlive.push_back(std::move(instance));
        return &node.inserts.back();
    };
    const auto fillChannel = [&](ChannelNode& node, const core::Channel& c, int index, int slot, bool isBus)
    {
        node.id = c.id;
        node.gain = c.gain;
        node.pan = c.pan;
        node.phaseInvert = c.phaseInvert;
        node.audible = !routing.mute[static_cast<size_t>(index)] && outputLit[static_cast<size_t>(index)];
        node.sendsAudible = !routing.mute[static_cast<size_t>(index)] && anyLit[static_cast<size_t>(index)];
        node.keepAliveRuntime = runtimeCache.get(c.id);
        node.runtime = node.keepAliveRuntime.get();
        node.runtime->targetGain.store(c.gain, std::memory_order_relaxed);
        node.runtime->targetPan.store(c.pan, std::memory_order_relaxed);
        node.outputBus = routing.output[static_cast<size_t>(index)];
        node.meterSlot = slot;
        for (const auto& s : c.sends)
        {
            const int bus = project.busIndex(s.bus);
            const auto& allowed = routing.sendTargets[static_cast<size_t>(index)];
            if (bus >= 0 && std::find(allowed.begin(), allowed.end(), bus) != allowed.end())
            {
                SendNode send{bus, s.level, s.preFader};
                send.keepAliveRuntime = runtimeCache.send(c.id, s.bus);
                send.runtime = send.keepAliveRuntime.get();
                send.runtime->targetLevel.store(s.level, std::memory_order_relaxed);
                node.sends.push_back(std::move(send));
            }
        }
        for (const auto* chained : c.processingChain())
            if (const auto& i = *chained; auto instance = inserts.get(i))
            {
                const bool keyed = instance->acceptsSidechain();
                auto* s = compileSlot(node, i, std::move(instance));
                latencyThrough[i.id] = node.chainLatency;
                // The key source: a track for anyone; a bus only for a bus insert,
                // and only one that survived the ordering above.
                int keyTrack = project.trackIndex(i.sidechain);
                int keyBus = -1;
                if (keyTrack < 0 && isBus)
                {
                    const int source = project.busIndex(i.sidechain);
                    const auto& allowed = busKeySources[static_cast<size_t>(routing.busOfChannel(index))];
                    if (source >= 0 && std::find(allowed.begin(), allowed.end(), source) != allowed.end())
                        keyBus = source;
                }
                if (!isBus && keyTrack == index)
                    keyTrack = -1; // a track keying from itself: meaningless
                s->keyTrack = keyed ? keyTrack : -1;
                s->keyBus = keyed ? keyBus : -1;
                s->keyPostFader = i.sidechainPostFader; // its delay is set with the compensation below
            }
        compileAutomation(node, c);
        // The work buffers, kept per channel across compiles (none allocated for a
        // channel the last snapshot had).
        node.keepAliveBuffers = runtimeCache.buffers(c.id, n);
        auto& b = *node.keepAliveBuffers;
        node.outLeft = {b.buffer(0), n};
        node.outRight = {b.buffer(1), n};
        node.dryLeft = {b.buffer(2), n};
        node.dryRight = {b.buffer(3), n};
        node.delayScratchLeft = {b.buffer(4), n};
        node.delayScratchRight = {b.buffer(5), n};
        node.postLeft = {b.buffer(8), n};
        node.postRight = {b.buffer(9), n};
        node.keyLeft = {b.buffer(10), n};
        node.keyRight = {b.buffer(11), n};
    };

    // ----- tracks -----------------------------------------------------------------------
    snapshot->tracks.reserve(project.tracks.size());
    for (int t = 0; t < trackCount; ++t)
    {
        const auto& model = project.tracks[static_cast<size_t>(t)];
        // A frozen track (or a folder) runs no chain: compile it from a copy
        // without its instrument, inserts, strip, sends (a folder's) and
        // insert automation.
        std::optional<core::Track> stripped;
        if (model.isFrozen() || model.isFolder())
        {
            stripped = model;
            stripped->instrument = core::Insert{};
            stripped->inserts.clear();
            stripped->strip.clear();
            if (model.isFolder())
                stripped->sends.clear();
            std::erase_if(stripped->automation,
                          [](const core::AutomationLane& lane)
                          { return lane.target == core::AutomationTarget::insertParameter; });
        }
        const auto& track = stripped ? *stripped : model;
        TrackNode node;
        fillChannel(node, track, t, trackMeterSlot(t), false);
        node.isMidi = track.isMidi();
        node.isInstrument = track.isInstrument() && !track.isFrozen();
        if (node.isMidi || node.isInstrument)
            node.events = MidiEventList(1024);
        node.isAux = track.isAux();
        if (track.isAux())
            node.inputChannel = -1;
        else if (!track.inputBus.empty())
        {
            const auto pair = inputBuses != nullptr ? inputBuses(track.inputBus) : InputPair{};
            const bool found = pair.left >= 0 && pair.right >= 0;
            node.inputChannel = found ? pair.left : -1;
            node.inputChannelRight = found ? pair.right : -1;
        }
        else
            node.inputChannel = track.inputChannel;
        node.armed = track.armed;
        node.monitor = track.monitor;
        if (node.isMidi)
        {
            if (runtimes != nullptr)
            {
                node.keepAliveMidi = runtimes->midi(track.id);
                node.midi = node.keepAliveMidi.get();
            }
            node.midiSource =
                midiSources != nullptr && !track.midiInput.empty() ? midiSources(track.midiInput) : -1;
            node.midiOut = track.midiOut;
            node.midiOutChannel = static_cast<std::uint8_t>(std::clamp(track.midiOutChannel, 1, 16) - 1);
            node.midiChannel = static_cast<std::uint8_t>(std::clamp(track.midiChannel, 1, 16) - 1);
            node.velocityScale = std::clamp(track.gain, 0.0f, 4.0f);
            node.midiMuted = track.mute || project.mutedByFolder(track);
            for (const auto& c : track.midiClips)
            {
                if (c.muted)
                    continue; // a muted clip is left out, so it plays nothing
                MidiClipNode clip;
                clip.start = c.start;
                clip.length = c.length;
                for (const auto& source : c.notes)
                {
                    MidiNoteNode note;
                    note.start = c.start + source.start;
                    note.end = std::min(c.start + source.end(), c.end());
                    note.note = source.note;
                    note.velocity = source.velocity;
                    if (note.end > note.start && note.start < c.end())
                    {
                        clip.notes.push_back(note);
                        clip.longest = std::max(clip.longest, note.end - note.start);
                    }
                }
                // The renderer searches by start.
                std::stable_sort(clip.notes.begin(),
                                 clip.notes.end(),
                                 [](const MidiNoteNode& a, const MidiNoteNode& b)
                                 { return a.start < b.start; });
                compileControllers(c, clip, node.controllerKeys);
                node.midiClips.push_back(std::move(clip));
            }
        }
        if (track.isFrozen())
        {
            // Frozen: its render plays from the song's start, through the fader.
            ClipNode clip;
            clip.keepAlive = media.get(track.frozenMedia, project.sampleRate);
            clip.source = clip.keepAlive.get();
            clip.length = clip.source != nullptr ? clip.source->lengthSamples() : 0;
            node.clips.push_back(std::move(clip));
        }
        node.clips.reserve(track.clips.size());
        const bool playsClips = !track.isFrozen() && !track.isFolder();
        for (const auto& c : track.clips)
        {
            if (!playsClips)
                break;
            if (c.muted)
                continue;
            ClipNode clip;
            clip.start = c.start;
            clip.length = c.length;
            // A stretched clip's source counts stretched samples.
            clip.sourceOffset = c.isStretched() ? static_cast<core::SamplePosition>(std::llround(
                                                      static_cast<double>(c.sourceOffset) * c.stretch))
                                                : c.sourceOffset;
            clip.gain = c.gain;
            clip.fadeIn = std::clamp<core::SamplePosition>(c.fadeIn, 0, c.length);
            clip.fadeOut = std::clamp<core::SamplePosition>(c.fadeOut, 0, c.length);
            clip.fadeInK = core::fadeShapeK(c.fadeInCurve); // off the audio thread: exp2 here
            clip.fadeOutK = core::fadeShapeK(c.fadeOutCurve);
            clip.keepAlive = media.sourceForClip(c, project.sampleRate);
            clip.source = clip.keepAlive.get();
            node.clips.push_back(std::move(clip));
        }
        // Sorted by start, with the furthest end so far beside each (the renderer
        // binary-searches it for the first clip a span reaches).
        std::stable_sort(node.clips.begin(),
                         node.clips.end(),
                         [](const ClipNode& a, const ClipNode& b) { return a.start < b.start; });
        node.clipEndsBy.reserve(node.clips.size());
        core::SamplePosition furthest = std::numeric_limits<core::SamplePosition>::min();
        for (const auto& c : node.clips)
            node.clipEndsBy.push_back(furthest = std::max(furthest, c.end()));
        node.scratchLeft = {node.keepAliveBuffers->buffer(6), n};
        node.scratchRight = {node.keepAliveBuffers->buffer(7), n};
        snapshot->tracks.push_back(std::move(node));
    }

    // ----- buses ------------------------------------------------------------------------
    snapshot->buses.reserve(project.buses.size());
    for (int b = 0; b < busCount; ++b)
    {
        BusNode node;
        fillChannel(
            node, project.buses[static_cast<size_t>(b)], trackCount + b, busMeterSlot(project, b), true);
        node.sumLeft = {node.keepAliveBuffers->buffer(6), n};
        node.sumRight = {node.keepAliveBuffers->buffer(7), n};
        snapshot->buses.push_back(std::move(node));
    }

    // ----- the master's inserts ----------------------------------------------------------
    // Everything has been summed by the time they run, so they take no key: a key
    // source would have to be held back by the whole graph to line up.
    {
        auto& node = snapshot->masterChain;
        node.id = core::masterId;
        for (const auto& i : project.master.inserts)
            if (auto instance = inserts.get(i))
                compileSlot(node, i, std::move(instance));
        // Its lanes (volume and its inserts' parameters) play through a channel
        // runtime of its own: the renderer reads the automated gain from it, the
        // mixer what it shows.
        node.keepAliveRuntime = runtimeCache.get(core::masterId);
        node.runtime = node.keepAliveRuntime.get();
        node.gain = project.master.gain;
        compileAutomation(node, project.master);
        std::erase_if(node.automation,
                      [](const AutomationNode& a) { return a.target == core::AutomationTarget::pan; });
        if (!node.inserts.empty())
        {
            node.keepAliveBuffers = runtimeCache.buffers(core::masterId, n);
            auto& b = *node.keepAliveBuffers;
            node.dryLeft = {b.buffer(2), n};
            node.dryRight = {b.buffer(3), n};
            node.keyLeft = {b.buffer(10), n};
            node.keyRight = {b.buffer(11), n};
        }
    }

    // ----- MIDI tracks into their instrument channels ---------------------------------
    for (int t = 0; t < trackCount; ++t)
    {
        const auto& track = project.tracks[static_cast<size_t>(t)];
        if (!track.isMidi())
            continue;
        const int target = project.trackIndex(track.midiTarget);
        if (target >= 0 && project.tracks[static_cast<size_t>(target)].isInstrument() &&
            !project.tracks[static_cast<size_t>(target)].isFrozen())
            snapshot->tracks[static_cast<size_t>(target)].midiFeeds.push_back(t);
    }

    // ----- aux tracks: the instrument they read -------------------------------------
    for (int t = 0; t < trackCount; ++t)
    {
        const auto& track = project.tracks[static_cast<size_t>(t)];
        auto& node = snapshot->tracks[static_cast<size_t>(t)];
        if (!track.isAux())
            continue;
        const int source = project.trackIndex(track.auxTrack);
        if (source < 0 || source == t)
            continue;
        const auto* sourceTrack = project.findTrack(track.auxTrack);
        // A frozen instrument does not run: its extra outputs are silent.
        if (sourceTrack == nullptr || sourceTrack->isFrozen() ||
            sourceTrack->findInsert(track.auxInsert) == nullptr)
            continue;
        if (auto instance = inserts.find(track.auxInsert))
        {
            node.auxSourceTrack = source;
            node.auxInsert = instance.get();
            node.auxOutput = std::max(0, track.auxOutput - 1);
            node.keepAliveAux = std::move(instance);
        }
    }

    // ----- track levels (sidechain and aux order) --------------------------------------
    // A track keyed from another renders after it. Levels are assigned until every
    // track is placed; a cycle (A keys B, B keys A) drops the keys that block it.
    {
        std::vector<int> level(static_cast<size_t>(trackCount), -1);
        int placed = 0;
        while (placed < trackCount)
        {
            std::vector<int> current;
            for (int t = 0; t < trackCount; ++t)
            {
                if (level[static_cast<size_t>(t)] >= 0)
                    continue;
                bool ready = true;
                for (const auto& slot : snapshot->tracks[static_cast<size_t>(t)].inserts)
                    if (slot.keyTrack >= 0 && level[static_cast<size_t>(slot.keyTrack)] < 0)
                        ready = false;
                if (const int source = snapshot->tracks[static_cast<size_t>(t)].auxSourceTrack;
                    source >= 0 && level[static_cast<size_t>(source)] < 0)
                    ready = false;
                // An instrument channel plays what its MIDI tracks scheduled this span.
                for (const int feed : snapshot->tracks[static_cast<size_t>(t)].midiFeeds)
                    if (level[static_cast<size_t>(feed)] < 0)
                        ready = false;
                if (ready)
                    current.push_back(t);
            }
            if (current.empty())
            {
                // Stuck: every remaining track waits on another remaining one. Drop
                // those keys and place them all in this level.
                for (int t = 0; t < trackCount; ++t)
                    if (level[static_cast<size_t>(t)] < 0)
                    {
                        for (auto& slot : snapshot->tracks[static_cast<size_t>(t)].inserts)
                            if (slot.keyTrack >= 0 && level[static_cast<size_t>(slot.keyTrack)] < 0)
                                slot.keyTrack = -1;
                        if (auto& aux = snapshot->tracks[static_cast<size_t>(t)].auxSourceTrack;
                            aux >= 0 && level[static_cast<size_t>(aux)] < 0)
                        {
                            aux = -1;
                            snapshot->tracks[static_cast<size_t>(t)].auxInsert = nullptr;
                        }
                        current.push_back(t);
                    }
            }
            const int thisLevel = static_cast<int>(snapshot->trackLevels.size());
            for (const int t : current)
                level[static_cast<size_t>(t)] = thisLevel;
            placed += static_cast<int>(current.size());
            snapshot->trackLevels.push_back(std::move(current));
        }
    }

    // ----- delay compensation ---------------------------------------------------------
    // Every destination (each bus, the master) waits for its slowest source: a
    // source's output lags the timeline by what its own inputs lag plus its chain,
    // and each edge into the destination is delayed by the difference. Buses are
    // solved in dependency order, so a bus's inputs are known before its output.
    {
        const int master = busCount; // index of the master in the "in" table
        std::vector<int> arrival(static_cast<size_t>(busCount + 1), 0); // slowest source so far
        std::vector<int> trackOut(static_cast<size_t>(trackCount), 0);
        std::vector<int> busOut(static_cast<size_t>(busCount), 0);
        const auto destinationIndex = [&](int bus) { return bus >= 0 ? bus : master; };
        const auto offer = [&](const ChannelNode& node, int out)
        {
            auto& slot = arrival[static_cast<size_t>(destinationIndex(node.outputBus))];
            slot = std::max(slot, out);
            for (const auto& send : node.sends)
            {
                auto& sendSlot = arrival[static_cast<size_t>(destinationIndex(send.bus))];
                sendSlot = std::max(sendSlot, out);
            }
        };
        for (int t = 0; t < trackCount; ++t)
        {
            // An output track carries an instrument's extra pair, which already lags
            // by the source chain up to that instrument.
            int sourceLag = 0;
            if (const auto& track = project.tracks[static_cast<size_t>(t)];
                track.isAux() && snapshot->tracks[static_cast<size_t>(t)].auxInsert != nullptr)
                if (const auto it = latencyThrough.find(track.auxInsert); it != latencyThrough.end())
                    sourceLag = it->second;
            trackOut[static_cast<size_t>(t)] =
                sourceLag + snapshot->tracks[static_cast<size_t>(t)].chainLatency;
            offer(snapshot->tracks[static_cast<size_t>(t)], trackOut[static_cast<size_t>(t)]);
        }
        for (int b : snapshot->busOrder)
        {
            auto& bus = snapshot->buses[static_cast<size_t>(b)];
            busOut[static_cast<size_t>(b)] = arrival[static_cast<size_t>(b)] + bus.chainLatency;
            offer(bus, busOut[static_cast<size_t>(b)]);
        }
        snapshot->outputLatency = arrival[static_cast<size_t>(master)] + snapshot->masterChain.chainLatency;

        DelayCache& cache = delays != nullptr ? *delays : localDelays;
        const auto attach = [&](ChannelNode& node, core::Id id, int out)
        {
            const auto line = [&](int bus, bool send)
            {
                const int needed = arrival[static_cast<size_t>(destinationIndex(bus))] - out;
                const core::Id destination =
                    bus >= 0 ? project.buses[static_cast<size_t>(bus)].id : core::invalidId;
                auto shared = cache.get(DelayKey{id, destination, send}, needed);
                auto* raw = needed > 0 ? shared.get() : nullptr;
                if (raw != nullptr)
                    snapshot->allDelays.push_back(raw);
                node.keepAliveDelays.push_back(std::move(shared));
                return raw;
            };
            node.outputDelay = line(node.outputBus, false);
            for (auto& send : node.sends)
                send.delay = line(send.bus, true);
        };
        for (int t = 0; t < trackCount; ++t)
            attach(snapshot->tracks[static_cast<size_t>(t)],
                   project.tracks[static_cast<size_t>(t)].id,
                   trackOut[static_cast<size_t>(t)]);
        for (int b = 0; b < busCount; ++b)
            attach(snapshot->buses[static_cast<size_t>(b)],
                   project.buses[static_cast<size_t>(b)].id,
                   busOut[static_cast<size_t>(b)]);

        // Keys: a slot's key lines up with the audio reaching that slot. A key that
        // arrives early (its source gathered less latency) is delayed by the
        // difference; one that arrives late cannot be pulled earlier and plays as is.
        const auto keys = [&](ChannelNode& node, int inputLag)
        {
            for (auto& slot : node.inserts)
            {
                const int t = slot.keyTrack;
                const int b = slot.keyBus;
                if (t < 0 && b < 0)
                    continue;
                ChannelNode* source =
                    t >= 0 ? static_cast<ChannelNode*>(&snapshot->tracks[static_cast<size_t>(t)])
                           : static_cast<ChannelNode*>(&snapshot->buses[static_cast<size_t>(b)]);
                if (slot.keyPostFader)
                    source->keyPostFader = true;
                const int keyLag = t >= 0 ? trackOut[static_cast<size_t>(t)] : busOut[static_cast<size_t>(b)];
                const int needed = inputLag + slot.inputLatency - keyLag;
                if (needed <= 0)
                    continue;
                auto line = cache.get(DelayKey{slot.id, source->id, false}, needed);
                snapshot->allDelays.push_back(line.get());
                slot.keyDelay = line.get();
                node.keepAliveDelays.push_back(std::move(line));
            }
        };
        for (int t = 0; t < trackCount; ++t)
        {
            auto& node = snapshot->tracks[static_cast<size_t>(t)];
            keys(node, trackOut[static_cast<size_t>(t)] - node.chainLatency);
        }
        for (int b = 0; b < busCount; ++b)
            keys(snapshot->buses[static_cast<size_t>(b)], arrival[static_cast<size_t>(b)]);
    }
    return snapshot;
}

} // namespace northlight::engine
