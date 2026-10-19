#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The project model: plain structs, no JUCE, no behaviour beyond lookups. Every edit
// goes through a core::Command (Command.h) so undo/redo and autosave fall out of one
// mechanism. The engine never reads this model; it reads compiled snapshots.
//
// Positions and lengths are in samples at the project sample rate, which makes the
// model sample-accurate and keeps tempo a display concern (TimeSignature below).

namespace aural::core
{

using Id = std::uint64_t;
constexpr Id invalidId = 0;
using SamplePosition = std::int64_t;

struct AudioClip
{
    Id id = invalidId;
    std::string name;
    std::string mediaPath;           // absolute or project-relative path to the audio file
    SamplePosition start = 0;        // timeline position of the first sample
    SamplePosition length = 0;       // samples on the timeline
    SamplePosition sourceOffset = 0; // first source sample used, in source samples
    float gain = 1.0f;               // linear
    SamplePosition fadeIn = 0;       // samples of linear fade at the start
    SamplePosition fadeOut = 0;      // and at the end (overlapping clips crossfade this way)

    SamplePosition end() const noexcept { return start + length; }
    bool operator==(const AudioClip&) const = default;
};

// A note inside a MIDI clip. Times are in samples relative to the clip's start.
struct MidiNote
{
    Id id = invalidId;
    int note = 60;      // 0..127
    int velocity = 100; // 1..127
    SamplePosition start = 0;
    SamplePosition length = 0;
    SamplePosition end() const noexcept { return start + length; }
    bool operator==(const MidiNote&) const = default;
};

// A clip of notes on a MIDI track.
struct MidiClip
{
    Id id = invalidId;
    std::string name;
    SamplePosition start = 0;
    SamplePosition length = 0;
    std::vector<MidiNote> notes; // kept sorted by start

    SamplePosition end() const noexcept { return start + length; }
    MidiNote* findNote(Id noteId) noexcept;
    const MidiNote* findNote(Id noteId) const noexcept;
    void sortNotes();
    bool operator==(const MidiClip&) const = default;
};

enum class TrackKind
{
    audio, // audio clips, an audio input
    midi,  // MIDI clips, a MIDI input, an instrument in its chain
    aux,   // an instrument's extra output pair, taken from another track
};

enum class MonitorMode
{
    off,       // never hear the input through the track
    automatic, // hear the input while the track is armed and the transport is not playing back
    on,        // always hear the input
};

// An aux send from a track or bus into a bus.
struct Send
{
    Id bus = invalidId;
    float level = 1.0f;    // linear
    bool preFader = false; // taps before the channel gain (after pan and phase)
    bool operator==(const Send&) const = default;
};

// A processor in a channel's insert chain. `type` names a built-in
// ("aural.eq") or, later, a hosted plugin; `params` are its parameter values in the
// processor's own order.
struct Insert
{
    Id id = invalidId;
    std::string type;
    bool bypass = false;
    std::vector<float> params;
    std::string state;        // opaque (base64) for processors that own their state
    float mix = 1.0f;         // wet/dry: 1 = fully processed, 0 = the input passes untouched
    Id sidechain = invalidId; // a track (or, on a bus, a bus) whose pre-fader signal feeds the key input
    bool operator==(const Insert&) const = default;
};

// What tracks and buses share: the fader section and routing.
// The loudest a channel fader goes (+6 dB); group moves clamp to it.
constexpr float maxChannelGain = 1.99526f;

// A fader group: its members' faders move together (keeping their balance) and
// mute and solo act on all of them. Groups are a model-level link applied by
// commands; the engine only sees the resulting channel values.
struct Group
{
    Id id = invalidId;
    std::string name;
    std::uint32_t colour = 0; // 0xAARRGGBB, 0 = default
    bool operator==(const Group&) const = default;
};

struct Channel
{
    Id id = invalidId;
    std::string name;
    float gain = 1.0f; // linear
    float pan = 0.0f;  // -1 left .. +1 right
    bool mute = false;
    bool solo = false;
    bool phaseInvert = false;
    std::uint32_t colour = 0; // 0xAARRGGBB, 0 = default
    Id outputBus = invalidId; // invalidId routes to the master
    Id group = invalidId;     // fader group membership
    std::vector<Send> sends;
    std::vector<Insert> inserts; // processed in order, pre-fader

    Send* findSend(Id busId) noexcept;
    Insert* findInsert(Id insertId) noexcept;
    const Insert* findInsert(Id insertId) const noexcept;
    int insertIndex(Id insertId) const noexcept;
    bool operator==(const Channel&) const = default;
};

struct Track : Channel
{
    TrackKind kind = TrackKind::audio;
    // An aux track (kind == aux) carries one extra stereo output of an instrument
    // on another track: output 1 is the first beyond the main pair.
    Id auxTrack = invalidId;
    Id auxInsert = invalidId;
    int auxOutput = 0;
    int inputChannel = -1; // device input channel index (callback order), -1 = none
    std::string midiInput; // MIDI input device name; empty = every enabled input
    bool armed = false;    // records when the transport records
    MonitorMode monitor = MonitorMode::automatic;
    std::vector<AudioClip> clips;    // kept sorted by start
    std::vector<MidiClip> midiClips; // kept sorted by start

    AudioClip* findClip(Id clipId) noexcept;
    const AudioClip* findClip(Id clipId) const noexcept;
    void sortClips();
    MidiClip* findMidiClip(Id clipId) noexcept;
    const MidiClip* findMidiClip(Id clipId) const noexcept;
    void sortMidiClips();
    bool isMidi() const noexcept { return kind == TrackKind::midi; }
    bool isAux() const noexcept { return kind == TrackKind::aux; }
    bool operator==(const Track&) const = default;
};

struct Bus : Channel
{
    bool operator==(const Bus&) const = default;
};

struct Master
{
    float gain = 1.0f;
    bool operator==(const Master&) const = default;
};

// A named point on the timeline, or a region when it has a length.
struct Marker
{
    Id id = invalidId;
    std::string name;
    SamplePosition position = 0;
    SamplePosition length = 0; // 0 = a marker; > 0 = a region
    SamplePosition end() const noexcept { return position + length; }
    bool isRegion() const noexcept { return length > 0; }
    bool operator==(const Marker&) const = default;
};

struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;
    bool operator==(const TimeSignature&) const = default;
};

struct Project
{
    static constexpr int schemaVersion = 1;

    std::string name = "Untitled";
    double sampleRate = 48000.0;
    double tempoBpm = 120.0;
    TimeSignature timeSignature;
    std::vector<Track> tracks;
    std::vector<Bus> buses;
    std::vector<Group> groups;
    std::vector<Marker> markers; // kept sorted by position
    Master master;
    Id nextId = 1;

    Id allocateId() noexcept { return nextId++; }

    Track* findTrack(Id trackId) noexcept;
    const Track* findTrack(Id trackId) const noexcept;
    int trackIndex(Id trackId) const noexcept; // -1 when absent
    Bus* findBus(Id busId) noexcept;
    const Bus* findBus(Id busId) const noexcept;
    int busIndex(Id busId) const noexcept;
    // A track or a bus, by id.
    Channel* findChannel(Id channelId) noexcept;
    const Channel* findChannel(Id channelId) const noexcept;
    Group* findGroup(Id groupId) noexcept;
    const Group* findGroup(Id groupId) const noexcept;
    Marker* findMarker(Id markerId) noexcept;
    const Marker* findMarker(Id markerId) const noexcept;
    void sortMarkers();
    // The nearest marker strictly before / after a position (null when none).
    const Marker* markerBefore(SamplePosition position) const noexcept;
    const Marker* markerAfter(SamplePosition position) const noexcept;
    int groupIndex(Id groupId) const noexcept;
    // Every track and bus in the group, in mixer order.
    std::vector<Channel*> groupMembers(Id groupId) noexcept;

    // Whether audio leaving bus `fromBus` can reach bus `toBus` through outputs
    // and sends (including fromBus == toBus). Used to refuse routing cycles.
    bool busReaches(Id fromBus, Id toBus) const noexcept;
    // Whether routing channel `channelId` to `busId` (as output or send) would
    // form a cycle: the bus is the channel itself or already feeds it.
    bool wouldCycle(Id channelId, Id busId) const noexcept;
    // Whether audio leaving `fromChannel` reaches `toChannel` through outputs,
    // sends and sidechain keys (a key edge runs from the source to the keyed
    // channel), including from == to.
    bool channelReaches(Id fromChannel, Id toChannel) const noexcept;
    // Whether keying an insert on `channelId` from `sourceId` would form a cycle:
    // the source is the channel itself or already hears it.
    bool keyWouldCycle(Id channelId, Id sourceId) const noexcept;

    // Length of the longest track in samples.
    SamplePosition length() const noexcept;

    bool operator==(const Project&) const = default;
};

// Bars/beats display helpers. Beats are counted in the time signature's denominator
// note value; bars and beats are 1-based for display.
struct BarsBeats
{
    int bar = 1;
    int beat = 1;
    int tick = 0; // 0..ticksPerBeat-1
    static constexpr int ticksPerBeat = 960;
    bool operator==(const BarsBeats&) const = default;
};

double samplesPerBeat(double sampleRate, double tempoBpm, TimeSignature signature) noexcept;

// Editing grid. gridSamples picks the finest musical division (bar, beat, half,
// quarter, eighth of a beat) that is at least `minPixels` wide at the zoom;
// snapToGrid rounds to the nearest line (the grid starts at zero).
SamplePosition gridSamples(double sampleRate,
                           double tempoBpm,
                           TimeSignature signature,
                           double samplesPerPixel,
                           int minPixels) noexcept;
SamplePosition snapToGrid(SamplePosition position, SamplePosition grid) noexcept;
BarsBeats samplesToBarsBeats(SamplePosition position,
                             double sampleRate,
                             double tempoBpm,
                             TimeSignature signature) noexcept;
SamplePosition
barsBeatsToSamples(BarsBeats position, double sampleRate, double tempoBpm, TimeSignature signature) noexcept;

} // namespace aural::core
