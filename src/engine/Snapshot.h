#pragma once

#include "core/Model.h"
#include "engine/AudioSource.h"
#include "engine/Delay.h"
#include "engine/Insert.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The compiled, immutable form of a Project that the real-time thread renders.
// Built on the controller thread by compileSnapshot(); published through
// rt::SnapshotPublisher; never mutated afterwards, except for the per-channel work
// buffers, which belong to whichever real-time thread renders that channel in the
// current block (the buffer pool is sized here, at compile time, so rendering never
// allocates).
//
// Signal flow (PLAN.md 3.2, Phase 4 scope): tracks render in parallel into their own
// pre-fader buffers; the summing stage applies the fader and routes each channel to
// its output bus (or the master) and its sends; buses follow in dependency order;
// the master gain is last. Mute and solo-in-place are resolved at compile time into
// `audible`.

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

struct SendNode
{
    int bus = -1; // index into Snapshot::buses
    float level = 1.0f;
    bool preFader = false;
    DelayLine* delay = nullptr; // compensation on this edge (null = none)
};

// What tracks and buses share once compiled.
struct ChannelNode
{
    core::Id id = core::invalidId;
    float gain = 1.0f;
    float pan = 0.0f;
    bool phaseInvert = false;
    bool audible = true; // mute/solo-in-place already resolved
    int outputBus = -1;  // index into Snapshot::buses, -1 = master
    int meterSlot = -1;
    std::vector<SendNode> sends;
    std::vector<InsertProcessor*> inserts;                   // in chain order
    std::vector<std::shared_ptr<InsertProcessor>> keepAlive; // controller-side ownership
    int chainLatency = 0;                                    // samples the inserts add
    DelayLine* outputDelay = nullptr;                        // compensation on the output edge
    std::vector<std::shared_ptr<DelayLine>> keepAliveDelays;
    mutable std::vector<float> delayScratchLeft, delayScratchRight; // a delayed copy for one edge

    // Pre-fader stereo output for one span (the parallel render writes it).
    mutable std::vector<float> outLeft, outRight;
};

struct TrackNode : ChannelNode
{
    int inputChannel = -1; // device input (callback order) or -1
    bool armed = false;
    core::MonitorMode monitor = core::MonitorMode::automatic;
    std::vector<ClipNode> clips; // sorted by start

    mutable std::vector<float> scratchLeft, scratchRight; // clip read scratch
};

struct BusNode : ChannelNode
{
    // What its inputs summed into it this span (before the bus's own fader).
    mutable std::vector<float> sumLeft, sumRight;
};

struct Snapshot
{
    // Largest block a snapshot can render in one call; the engine splits bigger
    // device blocks.
    static constexpr int maxBlockSamples = 8192;

    std::uint64_t epoch = 0; // set by the publisher
    double sampleRate = 48000.0;
    double tempoBpm = 120.0;
    core::TimeSignature timeSignature;
    std::vector<TrackNode> tracks;
    std::vector<BusNode> buses; // in project order
    std::vector<int> busOrder;  // bus indices in dependency order (feeders before fed)
    float masterGain = 1.0f;
    int masterMeterSlot = -1;
    int outputLatency = 0; // samples the master output lags the timeline (plugin delay compensation)
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

    // The source a clip should play: a per-clip instance for sources that keep
    // reader state (streaming), reused across recompiles so a clip's window
    // survives edits; the shared source otherwise.
    std::shared_ptr<const AudioSource>
    getForClip(const std::string& path, double sampleRate, core::Id clipId);

    // Drops entries no snapshot references any more.
    void prune();

    // Load errors by path since the last clearErrors().
    const std::map<std::string, std::string>& errors() const noexcept { return errors_; }
    void clearErrors() { errors_.clear(); }

private:
    struct Instance
    {
        std::string path;
        double sampleRate = 0.0;
        std::shared_ptr<const AudioSource> source;
    };

    MediaLoader& loader_;
    std::map<std::string, std::shared_ptr<const AudioSource>> sources_;
    std::map<core::Id, Instance> instances_;
    std::map<std::string, std::string> errors_;
};

// `delays` keeps compensation delay lines across compiles; without it each
// snapshot gets fresh (empty) lines.
std::unique_ptr<Snapshot> compileSnapshot(const core::Project& project,
                                          MediaCache& media,
                                          InsertCache& inserts,
                                          DelayCache* delays = nullptr);

// Meter slot layout used by the compiler: tracks in project order, then buses in
// project order, then the master.
inline int trackMeterSlot(int trackIndex) noexcept
{
    return trackIndex;
}
inline int busMeterSlot(const core::Project& project, int busIndex) noexcept
{
    return static_cast<int>(project.tracks.size()) + busIndex;
}
inline int masterMeterSlot(const core::Project& project) noexcept
{
    return static_cast<int>(project.tracks.size() + project.buses.size());
}

} // namespace aural::engine
