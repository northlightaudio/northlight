#include "engine/Snapshot.h"

#include "engine/rt/RealtimeGuard.h"

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

std::unique_ptr<Snapshot> compileSnapshot(const core::Project& project, MediaCache& media)
{
    AURAL_ASSERT_NOT_REALTIME("compileSnapshot");
    auto snapshot = std::make_unique<Snapshot>();
    snapshot->sampleRate = project.sampleRate;
    snapshot->tempoBpm = project.tempoBpm;
    snapshot->timeSignature = project.timeSignature;

    bool anySolo = false;
    for (const auto& t : project.tracks)
        anySolo = anySolo || t.solo;

    snapshot->tracks.reserve(project.tracks.size());
    for (const auto& t : project.tracks)
    {
        TrackNode node;
        node.id = t.id;
        node.gain = t.gain;
        node.pan = t.pan;
        node.audible = !t.mute && (!anySolo || t.solo);
        node.inputChannel = t.inputChannel;
        node.armed = t.armed;
        node.monitor = t.monitor;
        node.clips.reserve(t.clips.size());
        for (const auto& c : t.clips)
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
        const auto n = static_cast<size_t>(Snapshot::maxBlockSamples);
        node.outLeft.assign(n, 0.0f);
        node.outRight.assign(n, 0.0f);
        node.scratchLeft.assign(n, 0.0f);
        node.scratchRight.assign(n, 0.0f);
        snapshot->tracks.push_back(std::move(node));
    }
    return snapshot;
}

} // namespace aural::engine
