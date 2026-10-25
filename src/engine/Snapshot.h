#pragma once

#include "core/Model.h"
#include "core/TempoMap.h"
#include "engine/AudioSource.h"
#include "engine/Delay.h"
#include "engine/Insert.h"
#include "engine/Runtime.h"

#include <array>
#include <cstdint>
#include <functional>
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
// Signal flow (docs/architecture.md, "Rendering"): tracks render in parallel into their own
// pre-fader buffers; the summing stage applies the fader and routes each channel to
// its output bus (or the master) and its sends; buses follow in dependency order;
// the master gain is last. Mute and solo-in-place are resolved at compile time into
// `audible`.

namespace northlight::engine
{

struct ClipNode
{
    core::SamplePosition start = 0;
    core::SamplePosition length = 0;
    core::SamplePosition sourceOffset = 0;
    float gain = 1.0f;
    core::SamplePosition fadeIn = 0, fadeOut = 0; // in samples
    float fadeInK = 1.0f, fadeOutK = 1.0f;        // the curves' constants (core::fadeShape)
    const AudioSource* source = nullptr;          // null when the media failed to load
    std::shared_ptr<const AudioSource> keepAlive; // controller-side ownership only

    core::SamplePosition end() const noexcept { return start + length; }
};

struct SendNode
{
    int bus = -1; // index into Snapshot::buses
    float level = 1.0f;
    bool preFader = false;
    DelayLine* delay = nullptr;     // compensation on this edge (null = none)
    SendRuntime* runtime = nullptr; // the live level (ramped); outlives the snapshot
    std::shared_ptr<SendRuntime> keepAliveRuntime;
};

// A lane that plays: its points (a copy) and what it drives.
struct AutomationNode
{
    core::AutomationTarget target = core::AutomationTarget::gain;
    InsertProcessor* processor = nullptr; // an insert parameter's processor
    int parameter = 0;
    std::vector<core::AutomationPoint> points;
    AutomationRuntime* runtime = nullptr; // outlives the snapshot (the touch flag)
};

// One of a channel's work buffers: a view into its ChannelBuffers (the node
// keeps those alive), with the vector calls the renderer uses.
struct WorkBuffer
{
    float* ptr = nullptr;
    size_t count = 0;
    float* data() const noexcept { return ptr; }
    size_t size() const noexcept { return count; }
    float& operator[](size_t i) const noexcept { return ptr[i]; }
};

// One insert slot of a compiled chain.
struct InsertSlot
{
    InsertProcessor* processor = nullptr;
    core::Id id = core::invalidId;   // the insert's
    InsertMixRuntime* mix = nullptr; // wet/dry (1 = all wet), ramped; outlives the snapshot
    DelayLine* dry = nullptr;        // aligns the dry path to a latent insert (null = none)
    int keyTrack = -1;               // sidechain source: a track index, or -1
    int keyBus = -1;                 // or a bus index, or -1
    bool keyPostFader = false;       // the key taps after the source's fader
    DelayLine* keyDelay = nullptr;   // lines an early key up (null = none)
    int inputLatency = 0;            // what the chain adds before this slot
};

// What tracks and buses share once compiled.
struct ChannelNode
{
    core::Id id = core::invalidId;
    float gain = 1.0f;
    float pan = 0.0f;
    bool phaseInvert = false;
    bool audible = true;               // the direct output is heard (mute/solo-in-place resolved)
    bool sendsAudible = true;          // the sends are heard (a channel lit only through a send to a
                                       // soloed bus keeps its sends and loses its direct output)
    ChannelRuntime* runtime = nullptr; // ramped gain and pan; outlives the snapshot
    std::shared_ptr<ChannelRuntime> keepAliveRuntime;
    int outputBus = -1; // index into Snapshot::buses, -1 = master
    int meterSlot = -1;
    std::vector<SendNode> sends;
    std::vector<InsertSlot> inserts;                         // in chain order
    std::vector<std::shared_ptr<InsertProcessor>> keepAlive; // controller-side ownership
    std::vector<std::shared_ptr<InsertMixRuntime>> keepAliveMixes;
    WorkBuffer dryLeft, dryRight;     // the dry copy for a mixed slot
    int chainLatency = 0;             // samples the inserts add
    DelayLine* outputDelay = nullptr; // compensation on the output edge
    std::vector<std::shared_ptr<DelayLine>> keepAliveDelays;
    WorkBuffer delayScratchLeft, delayScratchRight; // a delayed copy for one edge

    // The lanes that play (empty when the channel's mode does not read).
    std::vector<AutomationNode> automation;
    std::vector<std::shared_ptr<AutomationRuntime>> keepAliveAutomation;

    // Pre-fader stereo output for one span (the parallel render writes it).
    WorkBuffer outLeft, outRight;
    // Some insert keys from this channel after its fader: the renderer also
    // writes the post-fader output, once the channel's chain has run.
    bool keyPostFader = false;
    WorkBuffer postLeft, postRight;
    WorkBuffer keyLeft, keyRight;                     // a delayed key for the slot being processed
    std::shared_ptr<ChannelBuffers> keepAliveBuffers; // what the WorkBuffers view
};

struct MidiNoteNode
{
    core::SamplePosition start = 0; // timeline position
    core::SamplePosition end = 0;   // exclusive, clipped to the clip
    int note = 60;
    int velocity = 100;
};

// A controller event at its timeline position. `key` is its chase key
// (engine::chaseKey*, -1 for poly pressure, which is not chased).
struct MidiControllerNode
{
    core::SamplePosition time = 0;
    core::MidiControllerType type = core::MidiControllerType::controller;
    std::uint8_t number = 0;
    std::int16_t value = 0;
    std::int16_t key = -1;
};

struct MidiClipNode
{
    core::SamplePosition start = 0, length = 0;
    std::vector<MidiNoteNode> notes; // sorted by start
    core::SamplePosition longest =
        0; // the longest note: one that starts earlier than this before a span has ended
    // Controller events inside the clip, sorted by time (events a trim left
    // before the start open the clip with their latest value per controller).
    std::vector<MidiControllerNode> controllers;
    // The last event of each chased controller in the clip (a chase from past
    // the clip's end), and how many controllers it chases.
    std::vector<MidiControllerNode> lastValues;
    int chasedKeys = 0;
    core::SamplePosition end() const noexcept { return start + length; }
};

struct TrackNode : ChannelNode
{
    bool isMidi = false;       // notes only: schedules its events, makes no audio
    bool isInstrument = false; // plays the merged events of the MIDI tracks feeding it
    bool isAux = false;
    // A MIDI track: how its events reach its instrument channel.
    std::uint8_t midiChannel = 0; // 0..15, stamped on every event it sends
    float velocityScale = 1.0f;   // the track's volume, applied to note-on velocities
    bool midiMuted = false;       // muted: no new notes (note-offs still pass)
    // An instrument channel: the MIDI tracks aimed at it (track indices).
    std::vector<int> midiFeeds;
    int auxSourceTrack = -1;              // the track whose instrument feeds this one
    InsertProcessor* auxInsert = nullptr; // that instrument
    int auxOutput = 0;                    // its extra pair, 0 = the first beyond main
    std::shared_ptr<InsertProcessor> keepAliveAux;
    int inputChannel = -1;           // device input (callback order) or -1; the left one of a stereo bus
    int inputChannelRight = -1;      // a stereo bus's right input, or -1 for a mono input
    int midiSource = -1;             // live MIDI: -1 = every input, else the engine's index for the device
    bool midiOut = false;            // the span's events also go to the MIDI output device
    std::uint8_t midiOutChannel = 0; // 0..15
    bool armed = false;
    core::MonitorMode monitor = core::MonitorMode::automatic;
    std::vector<ClipNode> clips;         // sorted by start
    std::vector<MidiClipNode> midiClips; // sorted by start
    // The chase keys any of its clips sets (1 where one does).
    std::array<std::uint8_t, 130> controllerKeys{};
    MidiRuntime* midi = nullptr; // held notes; set for MIDI tracks
    std::shared_ptr<MidiRuntime> keepAliveMidi;
    mutable MidiEventList events{1024}; // this span's events for the instrument

    WorkBuffer scratchLeft, scratchRight; // clip read scratch
};

struct BusNode : ChannelNode
{
    // What its inputs summed into it this span (before the bus's own fader).
    WorkBuffer sumLeft, sumRight;
};

struct Snapshot
{
    // Largest block a snapshot can render in one call; the engine splits bigger
    // device blocks.
    static constexpr int maxBlockSamples = 8192;

    std::uint64_t epoch = 0; // set by the publisher
    double sampleRate = 48000.0;
    double tempoBpm = 120.0;           // at the song's start
    core::TimeSignature timeSignature; // at the song's start
    core::TempoMap tempoMap;           // the tempo track: clicks and the play head
    std::vector<TrackNode> tracks;
    std::vector<BusNode> buses; // in project order
    std::vector<int> busOrder;  // bus indices in dependency order (feeders before fed)
    // Tracks grouped so that a track whose insert keys from another track renders
    // after it: each level runs in parallel, the levels in order. One level when
    // nothing is sidechained.
    std::vector<std::vector<int>> trackLevels;
    float masterGain = 1.0f;
    // The master's inserts, run in place on the mix before the master fader, and
    // its automation (volume, insert parameters) through the node's runtime. No
    // routing, sends or key.
    ChannelNode masterChain;
    MasterRuntime* master = nullptr; // ramped master gain
    std::shared_ptr<MasterRuntime> keepAliveMaster;
    int masterMeterSlot = -1;
    int outputLatency = 0; // samples the master output lags the timeline (plugin delay compensation,
                           // the master's own inserts included)
    std::vector<DelayLine*> allDelays; // every compensation and dry line, for a flush on locate
    std::vector<float> silence;        // maxBlockSamples zeros: the key of an inaudible source
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

    // What a clip plays: getForClip's source, passed through the clip hook when one
    // is set (vocal pitch edits lay their rendered spans over it).
    using ClipSourceHook = std::function<std::shared_ptr<const AudioSource>(
        const core::AudioClip& clip, double sampleRate, std::shared_ptr<const AudioSource> base)>;
    void setClipSourceHook(ClipSourceHook hook) { clipHook_ = std::move(hook); }
    // What a stretched or transposed clip plays (StretchService::sourceFor); a
    // stretched clip is silent without one.
    void setStretchHook(ClipSourceHook hook) { stretchHook_ = std::move(hook); }
    std::shared_ptr<const AudioSource> sourceForClip(const core::AudioClip& clip, double sampleRate);

    // A source loaded elsewhere (on a loading thread) for `path`; get() returns it
    // while its rate is the one asked for.
    void adopt(const std::string& path, std::shared_ptr<const AudioSource> source);

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
    ClipSourceHook clipHook_;
    ClipSourceHook stretchHook_;
};

// A stereo input bus resolved to the device inputs it reads, as indices into the
// active inputs in callback order; -1 where the device has no such input enabled.
struct InputPair
{
    int left = -1;
    int right = -1;
    bool operator==(const InputPair&) const = default;
};
using InputBusResolver = std::function<InputPair(const std::string& name)>;

// `delays` keeps compensation delay lines across compiles; without it each
// snapshot gets fresh (empty) lines. `inputBuses` resolves a track's input bus;
// without it (or for a name it does not know) the track has no input.
std::unique_ptr<Snapshot> compileSnapshot(const core::Project& project,
                                          MediaCache& media,
                                          InsertCache& inserts,
                                          DelayCache* delays = nullptr,
                                          RuntimeCache* runtimes = nullptr,
                                          const std::function<int(const std::string&)>& midiSources = {},
                                          const InputBusResolver& inputBuses = {});

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

} // namespace northlight::engine
