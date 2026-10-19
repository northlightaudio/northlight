#include "engine/Snapshot.h"

#include "engine/rt/RealtimeGuard.h"

#include <algorithm>
#include <functional>

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

std::unique_ptr<Snapshot>
compileSnapshot(const core::Project& project, MediaCache& media, InsertCache& inserts, DelayCache* delays)
{
    AURAL_ASSERT_NOT_REALTIME("compileSnapshot");
    auto snapshot = std::make_unique<Snapshot>();
    snapshot->sampleRate = project.sampleRate;
    snapshot->tempoBpm = project.tempoBpm;
    snapshot->timeSignature = project.timeSignature;
    snapshot->masterGain = project.master.gain;
    snapshot->masterMeterSlot = masterMeterSlot(project);

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
        channelOf(t, project.tracks[static_cast<size_t>(t)]);
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
        }
        // Any bus left out (should not happen after cycle breaking) renders last.
        for (int b = 0; b < busCount; ++b)
            if (std::find(snapshot->busOrder.begin(), snapshot->busOrder.end(), b) ==
                snapshot->busOrder.end())
                snapshot->busOrder.push_back(b);
    }

    // ----- solo-in-place --------------------------------------------------------------
    // With any solo, a channel is heard if it is soloed, feeds a soloed channel
    // (upstream), or is fed by one (downstream). Mute always wins.
    const bool anySolo = std::any_of(routing.solo.begin(), routing.solo.end(), [](bool s) { return s; });
    std::vector<bool> lit(static_cast<size_t>(trackCount + busCount), !anySolo);
    if (anySolo)
    {
        std::vector<bool> downstreamOfSolo(static_cast<size_t>(busCount), false);
        for (int c = 0; c < trackCount + busCount; ++c)
            if (routing.solo[static_cast<size_t>(c)])
            {
                lit[static_cast<size_t>(c)] = true;
                routing.downstream(c, downstreamOfSolo);
            }
        for (int b = 0; b < busCount; ++b)
            if (downstreamOfSolo[static_cast<size_t>(b)])
                lit[static_cast<size_t>(routing.channelOfBus(b))] = true;
        for (int c = 0; c < trackCount + busCount; ++c)
        {
            std::vector<bool> below(static_cast<size_t>(busCount), false);
            routing.downstream(c, below);
            for (int b = 0; b < busCount; ++b)
                if (below[static_cast<size_t>(b)] &&
                    routing.solo[static_cast<size_t>(routing.channelOfBus(b))])
                    lit[static_cast<size_t>(c)] = true;
        }
    }

    DelayCache localDelays; // used only when the caller keeps no cache
    const auto fillChannel = [&](ChannelNode& node, const core::Channel& c, int index, int slot)
    {
        node.id = c.id;
        node.gain = c.gain;
        node.pan = c.pan;
        node.phaseInvert = c.phaseInvert;
        node.audible = !routing.mute[static_cast<size_t>(index)] && lit[static_cast<size_t>(index)];
        node.outputBus = routing.output[static_cast<size_t>(index)];
        node.meterSlot = slot;
        for (const auto& s : c.sends)
        {
            const int bus = project.busIndex(s.bus);
            const auto& allowed = routing.sendTargets[static_cast<size_t>(index)];
            if (bus >= 0 && std::find(allowed.begin(), allowed.end(), bus) != allowed.end())
                node.sends.push_back(SendNode{bus, s.level, s.preFader});
        }
        for (const auto& i : c.inserts)
            if (auto instance = inserts.get(i))
            {
                const int latency = i.bypass ? 0 : std::max(0, instance->latencySamples());
                node.chainLatency += latency;
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
                    node.keepAliveDelays.push_back(std::move(line));
                }
                node.insertDry.push_back(dry);
                node.keepAlive.push_back(std::move(instance));
            }
        node.outLeft.assign(n, 0.0f);
        node.outRight.assign(n, 0.0f);
        node.dryLeft.assign(n, 0.0f);
        node.dryRight.assign(n, 0.0f);
        node.delayScratchLeft.assign(n, 0.0f);
        node.delayScratchRight.assign(n, 0.0f);
    };

    // ----- tracks -----------------------------------------------------------------------
    snapshot->tracks.reserve(project.tracks.size());
    for (int t = 0; t < trackCount; ++t)
    {
        const auto& track = project.tracks[static_cast<size_t>(t)];
        TrackNode node;
        fillChannel(node, track, t, trackMeterSlot(t));
        node.inputChannel = track.inputChannel;
        node.armed = track.armed;
        node.monitor = track.monitor;
        node.clips.reserve(track.clips.size());
        for (const auto& c : track.clips)
        {
            ClipNode clip;
            clip.start = c.start;
            clip.length = c.length;
            clip.sourceOffset = c.sourceOffset;
            clip.gain = c.gain;
            clip.keepAlive = media.getForClip(c.mediaPath, project.sampleRate, c.id);
            clip.source = clip.keepAlive.get();
            node.clips.push_back(std::move(clip));
        }
        node.scratchLeft.assign(n, 0.0f);
        node.scratchRight.assign(n, 0.0f);
        snapshot->tracks.push_back(std::move(node));
    }

    // ----- buses ------------------------------------------------------------------------
    snapshot->buses.reserve(project.buses.size());
    for (int b = 0; b < busCount; ++b)
    {
        BusNode node;
        fillChannel(node, project.buses[static_cast<size_t>(b)], trackCount + b, busMeterSlot(project, b));
        node.sumLeft.assign(n, 0.0f);
        node.sumRight.assign(n, 0.0f);
        snapshot->buses.push_back(std::move(node));
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
            trackOut[static_cast<size_t>(t)] = snapshot->tracks[static_cast<size_t>(t)].chainLatency;
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
    }
    return snapshot;
}

} // namespace aural::engine
