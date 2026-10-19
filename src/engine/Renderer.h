#pragma once

#include "engine/Click.h"
#include "engine/Snapshot.h"
#include "engine/Transport.h"
#include "engine/WorkerPool.h"

#include <vector>

// Renders one block of a Snapshot at the transport position into stereo outputs.
// Tracks render in parallel on the worker pool into their own buffers and are
// summed in track order, so the output is identical whatever the worker count.
// Input monitoring (a track's device input heard through its gain and pan) is part
// of the track render, so it works while stopped as well as while playing.
// Phase 2/3: every track mixes straight to the master; sends, buses and inserts
// arrive with the mixer in Phase 4. Real-time: render() allocates nothing.

namespace aural::engine
{

struct RenderContext
{
    const float* const* inputs = nullptr; // active device inputs, callback order
    int numInputs = 0;
    bool playing = false;   // clips play and the position advances
    bool recording = false; // armed tracks in automatic monitor mode hear their input
    bool countIn = false;   // the count-in before a recording: clicks only, position holds
};

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
                const RenderContext& context,
                float* const* outputs,
                int numOutputs,
                int numSamples,
                WorkerPool* pool) noexcept;

    // Pan laws, exposed for tests. Mono sources use constant power; stereo sources
    // use a balance law that leaves the centre untouched.
    static void monoPanGains(float pan, float& left, float& right) noexcept;
    static void stereoPanGains(float pan, float& left, float& right) noexcept;

    // Whether a track hears its input given the transport state.
    static bool monitorsInput(const TrackNode& track, bool playing, bool recording) noexcept;

    // Renders one track's clips (and monitored input) over
    // [position, position + numSamples) into the track's own buffers. Real-time; one
    // job of the parallel batch.
    static void renderTrack(const TrackNode& track,
                            core::SamplePosition position,
                            int numSamples,
                            const RenderContext& context) noexcept;

private:
    struct SpanJob
    {
        const Snapshot* snapshot;
        const RenderContext* context;
        core::SamplePosition position;
        int numSamples;
    };
    static void trackJob(void* context, int index) noexcept;

    void renderSpan(const Snapshot& snapshot,
                    const RenderContext& context,
                    core::SamplePosition position,
                    float* left,
                    float* right,
                    int numSamples,
                    WorkerPool* pool) noexcept;

    void renderClicks(const Snapshot& snapshot,
                      const TransportState& transport,
                      core::SamplePosition start,
                      float* left,
                      float* right,
                      int numSamples) noexcept;

    int maxBlockSize_ = 0;
    std::vector<float> mixLeft_, mixRight_;
    ClickGenerator click_;
};

} // namespace aural::engine
