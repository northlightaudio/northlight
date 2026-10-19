#pragma once

#include "core/Model.h"
#include "engine/AudioSource.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The compiled, immutable form of a Project that the real-time thread renders.
// Built on the controller thread by compileSnapshot(); published through
// rt::SnapshotPublisher; never mutated afterwards, except for the per-track work
// buffers, which belong to whichever real-time thread renders that track in the
// current block (the buffer pool is sized here, at compile time, so rendering never
// allocates).

namespace aural::engine
{

struct ClipNode
{
    core::SamplePosition start = 0;
    core::SamplePosition length = 0;
    core::SamplePosition sourceOffset = 0;
    float gain = 1.0f;
    const AudioSource* source = nullptr;          // null when the media failed to load
    std::shared_ptr<const AudioSource> keepAlive; // controller-side ownership only

    core::SamplePosition end() const noexcept { return start + length; }
};

struct TrackNode
{
    core::Id id = core::invalidId;
    float gain = 1.0f;
    float pan = 0.0f;
    bool audible = true;         // mute/solo already resolved
    std::vector<ClipNode> clips; // sorted by start

    // Work buffers for one block: the track's stereo output and clip read scratch.
    mutable std::vector<float> outLeft, outRight, scratchLeft, scratchRight;
};

struct Snapshot
{
    // Largest block a snapshot can render in one call; the engine splits bigger
    // device blocks.
    static constexpr int maxBlockSamples = 8192;

    std::uint64_t epoch = 0; // set by the publisher
    double sampleRate = 48000.0;
    std::vector<TrackNode> tracks;
};

// Caches loaded media by path across snapshot compiles so a re-publish after an edit
// does not reload every file. Controller thread only.
class MediaCache
{
public:
    explicit MediaCache(MediaLoader& loader) : loader_(loader) {}

    // Returns the cached or freshly loaded source; nullptr (with the error
    // recorded) when loading failed.
    std::shared_ptr<const AudioSource> get(const std::string& path, double sampleRate);

    // Drops entries no snapshot references any more.
    void prune();

    // Load errors by path since the last clearErrors().
    const std::map<std::string, std::string>& errors() const noexcept { return errors_; }
    void clearErrors() { errors_.clear(); }

private:
    MediaLoader& loader_;
    std::map<std::string, std::shared_ptr<const AudioSource>> sources_;
    std::map<std::string, std::string> errors_;
};

std::unique_ptr<Snapshot> compileSnapshot(const core::Project& project, MediaCache& media);

} // namespace aural::engine
