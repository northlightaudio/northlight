#pragma once

#include "engine/Snapshot.h"
#include "engine/Transport.h"
#include "engine/WorkerPool.h"

#include <vector>

// Renders one block of a Snapshot at the transport position into stereo outputs.
// Tracks render in parallel on the worker pool into their own buffers and are
// summed in track order, so the output is identical whatever the worker count.
// Phase 2: every track mixes straight to the master; sends, buses and inserts
// arrive with the mixer in Phase 4. Real-time: render() allocates nothing.

namespace aural::engine
{

class Renderer
{
public:
    // Controller thread: allocates the mix buffers for blocks up to maxBlockSize
    // (capped at Snapshot::maxBlockSamples).
    void prepare(int maxBlockSize);
    int maxBlockSize() const noexcept { return maxBlockSize_; }

    // Real-time. Advances `transport` by numSamples when playing (honouring the
    // loop), and overwrites the outputs with the mix. Outputs beyond the first two
    // are cleared. numSamples must not exceed maxBlockSize(). `pool` may be null.
    void render(const Snapshot& snapshot,
                TransportState& transport,
                float* const* outputs,
                int numOutputs,
                int numSamples,
                WorkerPool* pool) noexcept;

    // Pan laws, exposed for tests. Mono sources use constant power; stereo sources
    // use a balance law that leaves the centre untouched.
    static void monoPanGains(float pan, float& left, float& right) noexcept;
    static void stereoPanGains(float pan, float& left, float& right) noexcept;

    // Renders one track's clips over [position, position + numSamples) into the
    // track's own buffers. Real-time; one job of the parallel batch.
    static void renderTrack(const TrackNode& track, core::SamplePosition position, int numSamples) noexcept;

private:
    struct SpanJob
    {
        const Snapshot* snapshot;
        core::SamplePosition position;
        int numSamples;
    };
    static void trackJob(void* context, int index) noexcept;

    void renderSpan(const Snapshot& snapshot,
                    core::SamplePosition position,
                    float* left,
                    float* right,
                    int numSamples,
                    WorkerPool* pool) noexcept;

    int maxBlockSize_ = 0;
    std::vector<float> mixLeft_, mixRight_;
};

} // namespace aural::engine
