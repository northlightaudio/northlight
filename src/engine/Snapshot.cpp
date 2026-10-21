#include "engine/Snapshot.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>

namespace aural::engine
{

std::shared_ptr<const AudioSource> MediaCache::get(const std::string& path, double sampleRate)
{
    AURAL_ASSERT_NOT_REALTIME("MediaCache::get");
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
    auto base = getForClip(clip.mediaPath, sampleRate, clip.id);
    if (clip.isStretched())
        return stretchHook_ && base != nullptr ? stretchHook_(clip, sampleRate, std::move(base)) : nullptr;
    if (clipHook_ && base != nullptr && clip.vocal.active())
        return clipHook_(clip, sampleRate, std::move(base));
    return base;
}

void MediaCache::prune()
{
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

std::unique_ptr<Snapshot> compileSnapshot(const core::Project& project,
                                          MediaCache& media,
                                          InsertCache& inserts,
                                          DelayCache* delays,
                                          RuntimeCache* runtimes,
                                          const std::function<int(const std::string&)>& midiSources)
{
    AURAL_ASSERT_NOT_REALTIME("compileSnapshot");
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
    snapshot->silence.assign(static_cast<size_t>(Snapshot::maxBlockSamples), 0.0f);

    const auto trackCount = static_cast<int>(project.tracks.size());
    const auto busCount = static_cast<int>(project.buses.size());
    const auto n = static_cast<size_t>(Snapshot::maxBlockSamples);

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
                // Latency counts whether or not the slot is bypassed: a bypassed
                // processor still delays by it (plugins through processBlockBypassed,
                // the limiter through its lookahead), so toggling bypass never
                // re-solves the graph.
                const int latency = std::max(0, instance->latencySamples());
                node.chainLatency += latency;
                latencyThrough[i.id] = node.chainLatency;
                node.inserts.push_back(instance.get());
                node.insertMix.push_back(std::clamp(i.mix, 0.0f, 1.0f));
                // A partly dry slot needs its dry copy held back by the insert's
                // latency so the two halves line up.
                DelayLine* dry = nullptr;
                if (i.mix < 1.0f && latency > 0)
                {
                    auto line = (delays != nullptr ? *delays : localDelays)
                                    .get(DelayKey{i.id, core::invalidId, true}, latency);
                    dry = line.get();
                    snapshot->allDelays.push_back(dry);
                    node.keepAliveDelays.push_back(std::move(line));
                }
                node.insertDry.push_back(dry);
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
                node.insertKeyTrack.push_back(instance->acceptsSidechain() ? keyTrack : -1);
                node.insertKeyBus.push_back(instance->acceptsSidechain() ? keyBus : -1);
                node.insertKeyPostFader.push_back(i.sidechainPostFader ? 1 : 0);
                node.insertKeyDelay.push_back(nullptr); // set with the compensation below
                node.insertIds.push_back(i.id);
                node.insertInputLatency.push_back(node.chainLatency - latency);
                node.keepAlive.push_back(std::move(instance));
            }
        // Automation: the lanes with points, when the mode reads. An insert
        // parameter plays only on a processor whose parameters the model owns
        // (built-ins and the strip; a hosted plugin owns its own).
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
                    std::find(node.inserts.begin(), node.inserts.end(), instance.get()) == node.inserts.end())
                    continue;
                a.processor = instance.get();
            }
            a.points = lane.points;
            auto runtime = runtimeCache.automation(lane.id);
            a.runtime = runtime.get();
            node.keepAliveAutomation.push_back(std::move(runtime));
            node.automation.push_back(std::move(a));
        }
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
        node.isAux = track.isAux();
        node.inputChannel = track.isAux() ? -1 : track.inputChannel;
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
            clip.keepAlive = media.sourceForClip(c, project.sampleRate);
            clip.source = clip.keepAlive.get();
            node.clips.push_back(std::move(clip));
        }
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
                for (const int source : snapshot->tracks[static_cast<size_t>(t)].insertKeyTrack)
                    if (source >= 0 && level[static_cast<size_t>(source)] < 0)
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
                        for (auto& source : snapshot->tracks[static_cast<size_t>(t)].insertKeyTrack)
                            if (source >= 0 && level[static_cast<size_t>(source)] < 0)
                                source = -1;
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
        snapshot->outputLatency = arrival[static_cast<size_t>(master)];

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
            for (size_t k = 0; k < node.insertKeyTrack.size(); ++k)
            {
                const int t = node.insertKeyTrack[k];
                const int b = node.insertKeyBus[k];
                if (t < 0 && b < 0)
                    continue;
                ChannelNode* source =
                    t >= 0 ? static_cast<ChannelNode*>(&snapshot->tracks[static_cast<size_t>(t)])
                           : static_cast<ChannelNode*>(&snapshot->buses[static_cast<size_t>(b)]);
                if (node.insertKeyPostFader[k] != 0)
                    source->keyPostFader = true;
                const int keyLag = t >= 0 ? trackOut[static_cast<size_t>(t)] : busOut[static_cast<size_t>(b)];
                const int needed = inputLag + node.insertInputLatency[k] - keyLag;
                if (needed <= 0)
                    continue;
                auto line = cache.get(DelayKey{node.insertIds[k], source->id, false}, needed);
                snapshot->allDelays.push_back(line.get());
                node.insertKeyDelay[k] = line.get();
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

} // namespace aural::engine
