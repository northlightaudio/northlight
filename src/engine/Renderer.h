#pragma once

#include "engine/Click.h"
#include "engine/Loudness.h"
#include "engine/Meters.h"
#include "engine/Snapshot.h"
#include "engine/Transport.h"
#include "engine/WorkerPool.h"
#include "engine/rt/SpscQueue.h"

#include <vector>

// Renders one block of a Snapshot at the transport position into stereo outputs.
//
// Per span: tracks render in parallel on the worker pool into their pre-fader
// buffers (clips, monitored input, pan, phase); then the summing stage, on the
// callback thread, applies each track's fader, routes it to its bus or the master
// and feeds its sends, walks the buses in dependency order doing the same, applies
// the master gain, and publishes peak/RMS for every channel. Summing order is
// fixed, so the output is identical whatever the worker count. Inserts arrive with
// the built-in plugins. Real-time: render() allocates nothing.

namespace aural::engine
{

struct RenderContext
{
    const float* const* inputs = nullptr; // active device inputs, callback order
    int numInputs = 0;
    bool playing = false;              // clips play and the position advances
    bool recording = false;            // armed tracks in automatic monitor mode hear their input
    bool countIn = false;              // the count-in before a recording: clicks only, position holds
    rt::MeterBlock* meters = nullptr;  // may be null (tests)
    LoudnessMeter* loudness = nullptr; // master loudness, may be null
    // Live MIDI input for this block (offsets within it) with each event's source
    // index in a parallel array; null when there is none. The engine hands it to
    // the first span of a block only.
    const MidiEventList* liveMidi = nullptr;
    const int* liveMidiSources = nullptr;
    // Per event: the track an audition is for (core::invalidId for device input);
    // may be null.
    const core::Id* liveMidiTargets = nullptr;
    // Where this render call starts within the device block the live MIDI's
    // offsets count from, and whether it is the block's last piece (which takes
    // any event past its end).
    int liveMidiBase = 0;
    bool liveMidiLast = true;
    // The MIDI output: tracks that play it push their span's events here with
    // their time on the engine's sample clock (this span starts at sampleClock).
    rt::SpscQueue<MidiOutEvent>* midiOut = nullptr;
    std::int64_t sampleClock = 0;
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
    // The same for a MIDI track's live input (no device channel needed).
    static bool monitorsMidi(const TrackNode& track, bool playing, bool recording) noexcept;
    // Collects a MIDI track's events for [position, position + numSamples): its
    // clips while playing, the live input while it monitors, and the note-offs a
    // stop, jump or loop wrap demands, into track.events (sorted).
    static void scheduleMidi(const TrackNode& track,
                             core::SamplePosition position,
                             int numSamples,
                             const RenderContext& context) noexcept;

    // Renders one track's clips (and monitored input) over
    // [position, position + numSamples) into the track's pre-fader buffers.
    // Real-time; one job of the parallel batch.
    static void renderTrack(const Snapshot& snapshot,
                            const TrackNode& track,
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
        const std::vector<int>* tracks; // the level being rendered
    };
    static void trackJob(void* context, int index) noexcept;
    // Runs a channel's insert chain in place, blending each slot's wet/dry mix and
    // feeding keyed slots their source's pre-fader signal.
    static void processChain(const Snapshot& snapshot,
                             const ChannelNode& channel,
                             const PlayHead& playHead,
                             float* left,
                             float* right,
                             int numSamples,
                             const MidiEventList* midi = nullptr) noexcept;

    // The song at `position` for a span's inserts.
    static PlayHead
    playHeadAt(const Snapshot& snapshot, core::SamplePosition position, bool playing) noexcept;

    void renderSpan(const Snapshot& snapshot,
                    const RenderContext& context,
                    core::SamplePosition position,
                    float* left,
                    float* right,
                    int numSamples,
                    WorkerPool* pool) noexcept;

    // Applies a channel's fader and routes its post-fader signal and sends.
    static void routeChannel(const Snapshot& snapshot,
                             const ChannelNode& channel,
                             const RenderContext& context,
                             float* masterLeft,
                             float* masterRight,
                             int numSamples) noexcept;

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
