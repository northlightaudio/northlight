#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

// The project model: plain structs, no JUCE, no behaviour beyond lookups. Every edit
// goes through a core::Command (Command.h) so undo/redo and autosave fall out of one
// mechanism. The engine never reads this model; it reads compiled snapshots.
//
// Positions and lengths are in samples at the project sample rate, which makes the
// model sample-accurate and keeps tempo a display concern (TimeSignature below).

#include "core/Vocal.h"

namespace northlight::core
{

using Id = std::uint64_t;
constexpr Id invalidId = 0;
// The master's channel id: fixed, and never handed out (allocateId counts up from 1).
constexpr Id masterId = std::numeric_limits<Id>::max();
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
    VocalEdit vocal;                 // pitch edits (core/Vocal.h); inactive = the media as recorded
    // Time-stretch and pitch-shift: timeline samples per source sample (2 = half
    // speed, twice as long) and a transposition in semitones that keeps the
    // timing. sourceOffset stays in source samples; the clip plays
    // length / stretch of them.
    double stretch = 1.0;
    double transpose = 0.0;
    // Musical time base: a tempo change moves the clip with its bar and stretches
    // it to keep its length in beats (MIDI clips always follow the tempo).
    bool followTempo = false;

    SamplePosition end() const noexcept { return start + length; }
    bool isStretched() const noexcept { return stretch != 1.0 || transpose != 0.0; }
    // Source samples the clip plays.
    SamplePosition sourceLength() const noexcept
    {
        return static_cast<SamplePosition>(static_cast<double>(length) / stretch + 0.5);
    }
    // The source sample under a timeline offset from the clip's start.
    SamplePosition sourceAt(SamplePosition offsetInClip) const noexcept
    {
        return sourceOffset + static_cast<SamplePosition>(static_cast<double>(offsetInClip) / stretch);
    }
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

// What a controller event in a MIDI clip changes.
enum class MidiControllerType : std::uint8_t
{
    controller,      // a control change: `number` 0..127, `value` 0..127
    pitchBend,       // `value` -8192..8191 (0 = centre); `number` unused
    channelPressure, // `value` 0..127; `number` unused
    polyPressure,    // aftertouch on one key: `number` the note, `value` 0..127
};

// The value range of a controller type, for clamping and drawing.
constexpr int midiControllerMinimum(MidiControllerType type) noexcept
{
    return type == MidiControllerType::pitchBend ? -8192 : 0;
}
constexpr int midiControllerMaximum(MidiControllerType type) noexcept
{
    return type == MidiControllerType::pitchBend ? 8191 : 127;
}

// A controller event inside a MIDI clip. The time is in samples relative to the
// clip's start, like a note's; the value holds until the next event of the same
// controller (the engine chases it after a jump).
struct MidiControllerEvent
{
    Id id = invalidId;
    MidiControllerType type = MidiControllerType::controller;
    int number = 0; // the controller (or, for poly pressure, the note)
    int value = 0;
    SamplePosition time = 0;

    // Whether two events belong to the same lane (type and, where it counts, number).
    bool sameLane(MidiControllerType t, int n) const noexcept
    {
        const bool numbered =
            type == MidiControllerType::controller || type == MidiControllerType::polyPressure;
        return type == t && (!numbered || number == n);
    }
    bool operator==(const MidiControllerEvent&) const = default;
};

// A clip of notes on a MIDI track.
struct MidiClip
{
    Id id = invalidId;
    std::string name;
    SamplePosition start = 0;
    SamplePosition length = 0;
    std::vector<MidiNote> notes;                  // kept sorted by start
    std::vector<MidiControllerEvent> controllers; // kept sorted by time

    SamplePosition end() const noexcept { return start + length; }
    MidiNote* findNote(Id noteId) noexcept;
    const MidiNote* findNote(Id noteId) const noexcept;
    void sortNotes();
    MidiControllerEvent* findController(Id eventId) noexcept;
    const MidiControllerEvent* findController(Id eventId) const noexcept;
    void sortControllers();
    bool operator==(const MidiClip&) const = default;
};

// Take lanes and comping (docs/design/screens/comping.md). Each recorded pass over a
// range is a Take in a TakeGroup on its track; the comp says which take plays
// where. What plays is ordinary clips: the group materialises its comp as clips
// in Track::clips (each segment a clip on its take's media, crossfaded at the
// boundaries) and remembers their ids, so the engine, bounce and every clip
// edit see plain clips. The comp commands (core/Comping.h) rebuild those clips
// from the takes and segments; "flatten" drops the group and keeps the clips.
struct Take
{
    AudioClip clip; // the pass as recorded; clip.id is the take's id, clip.name its name
    int number = 1; // shown as "T<n>"; numbers are not reused within a group
    Id id() const noexcept { return clip.id; }
    bool operator==(const Take&) const = default;
};

// A stretch of the comp: [start, end) plays `take`.
struct CompSegment
{
    SamplePosition start = 0;
    SamplePosition end = 0;
    Id take = invalidId;
    SamplePosition length() const noexcept { return end - start; }
    bool operator==(const CompSegment&) const = default;
};

struct TakeGroup
{
    Id id = invalidId;
    std::vector<Take> takes;       // oldest first; the lanes show the newest on top
    std::vector<CompSegment> comp; // sorted, contiguous, covering [start(), end())
    std::vector<Id> clips;         // the Track::clips the comp is materialised as
    SamplePosition crossfade = 0;  // at each boundary between segments (0: butt splices)
    bool expanded = false;         // the take lanes show under the track's row

    SamplePosition start() const noexcept; // of the earliest take
    SamplePosition end() const noexcept;   // of the latest take
    Take* findTake(Id takeId) noexcept;
    const Take* findTake(Id takeId) const noexcept;
    int takeIndex(Id takeId) const noexcept; // in `takes`; -1 when absent
    bool ownsClip(Id clipId) const noexcept;
    bool operator==(const TakeGroup&) const = default;
};

enum class TrackKind
{
    audio,      // audio clips, an audio input
    midi,       // MIDI clips and a MIDI input: notes for an instrument channel, no audio of its own
    instrument, // an instrument channel: the instrument (its main output) and the processing after it
    aux,        // an instrument's extra output pair, taken from an instrument channel
    folder,     // holds other tracks (Track::folder): no audio; its mute and solo act on them
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
// ("northlight.eq") or, later, a hosted plugin; `params` are its parameter values in the
// processor's own order.
struct Insert
{
    Id id = invalidId;
    std::string type;
    bool bypass = false;
    std::vector<float> params;
    std::string state;        // opaque (base64) for processors that own their state
    float mix = 1.0f;         // wet/dry: 1 = fully processed, 0 = the input passes untouched
    Id sidechain = invalidId; // a track (or, on a bus, a bus) whose signal feeds the key input
    // Where the key taps its source: before the source's fader (false) or after
    // it, so the key follows the source's fader and gain automation.
    bool sidechainPostFader = false;
    // The stable keys of `params` as a file named them, kept only until a
    // processor of this type says where each one goes now (core
    // reconcileParameters); a plugin that is not installed keeps them, so a
    // re-save does not lose them. Empty: `params` is in the processor's order.
    std::vector<std::string> paramKeys;
    bool operator==(const Insert&) const = default;
};

// Automation (Phase 9): a lane of breakpoints for one of a channel's
// parameters. Values are in the parameter's own units (gain linear, pan -1..1,
// an insert parameter in its range). Between points the value is interpolated
// (gain in dB, so a fade sounds even), and it holds before the first point and
// after the last.
enum class AutomationTarget : std::uint8_t
{
    gain,
    pan,
    insertParameter,
};

struct AutomationPoint
{
    SamplePosition position = 0;
    float value = 0.0f;
    bool operator==(const AutomationPoint&) const = default;
};

// The value of a sorted run of points at a position. Real-time safe.
inline float automationValueAt(const AutomationPoint* points,
                               size_t count,
                               AutomationTarget target,
                               SamplePosition position) noexcept
{
    if (count == 0)
        return 0.0f;
    if (position <= points[0].position)
        return points[0].value;
    if (position >= points[count - 1].position)
        return points[count - 1].value;
    size_t lo = 0, hi = count - 1; // points[lo] <= position < points[hi]
    while (hi - lo > 1)
    {
        const size_t mid = (lo + hi) / 2;
        (points[mid].position <= position ? lo : hi) = mid;
    }
    const auto& a = points[lo];
    const auto& b = points[hi];
    const double t =
        static_cast<double>(position - a.position) / static_cast<double>(b.position - a.position);
    if (target == AutomationTarget::gain && a.value > 1.0e-5f && b.value > 1.0e-5f)
        return static_cast<float>(std::exp(std::log(a.value) + (std::log(b.value) - std::log(a.value)) * t));
    return static_cast<float>(a.value + (b.value - a.value) * t);
}

// How the arrange view shows a lane, saved with the project. None of it
// changes what plays: a hidden or collapsed lane still plays.
struct AutomationLaneView
{
    int height = 0;         // the lane's height in pixels, 0 = the view's default
    bool collapsed = false; // a thin strip with the curve and no points
    bool hidden = false;    // out of the view
    bool operator==(const AutomationLaneView&) const = default;
};

struct AutomationLane
{
    Id id = invalidId;
    AutomationTarget target = AutomationTarget::gain;
    Id insert = invalidId; // the insert or strip module, for an insert parameter
    int parameter = 0;
    std::vector<AutomationPoint> points; // sorted by position
    std::string parameterKey; // as a file named the parameter, until it is resolved (see Insert::paramKeys)
    AutomationLaneView view;

    // A lane with no points plays nothing (the engine skips it).
    float valueAt(SamplePosition position) const noexcept
    {
        return automationValueAt(points.data(), points.size(), target, position);
    }
    bool sameTarget(AutomationTarget t, Id insertId, int parameterIndex) const noexcept
    {
        return target == t && (t != AutomationTarget::insertParameter ||
                               (insert == insertId && parameter == parameterIndex));
    }
    bool operator==(const AutomationLane&) const = default;
};

// How a channel's automation plays and records.
enum class AutomationMode : std::uint8_t
{
    off,   // the lanes are ignored
    read,  // the lanes play
    touch, // the lanes play; a control moved while playing writes until released
    latch, // the lanes play; a control moved while playing writes until the transport stops
    write, // while playing, the controls' values overwrite their lanes
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
    int height = 0;           // the arrange row's height in pixels, 0 = the view's default
    Id outputBus = invalidId; // invalidId routes to the master
    Id group = invalidId;     // fader group membership
    std::vector<Send> sends;
    // An instrument channel's instrument (an empty type: none): the slot ahead
    // of its effects that the MIDI tracks aimed at the channel play. Its main
    // output is the channel's audio; each further stereo output can feed an
    // output track (an aux track). Other channels have none.
    Insert instrument;
    std::vector<Insert> inserts; // processed in order, pre-fader
    // The channel strip: built-in modules every channel has (gate, EQ,
    // compressor, ...), in processing order, switched on and off in place
    // (bypass = off) instead of being added and removed. They are stored as
    // inserts so the insert commands (parameters, bypass) and the engine's
    // instance cache apply to them unchanged.
    std::vector<Insert> strip;
    bool stripPreInserts = false; // the strip runs before the inserts (default: after them)
    std::vector<AutomationLane> automation;
    AutomationMode automationMode = AutomationMode::read;
    bool automationOpen = false; // the arrange view shows the lanes under the channel's row

    Send* findSend(Id busId) noexcept;
    const Send* findSend(Id busId) const noexcept;
    bool hasInstrument() const noexcept { return !instrument.type.empty(); }
    // The instrument, an insert or a strip module, by id.
    Insert* findInsert(Id insertId) noexcept;
    const Insert* findInsert(Id insertId) const noexcept;
    int insertIndex(Id insertId) const noexcept; // in `inserts` only; -1 for a strip module
    int stripIndex(Id insertId) const noexcept;  // in `strip` only
    // Every processor in processing order: the instrument, then the inserts and
    // the strip modules.
    std::vector<const Insert*> processingChain() const;
    AutomationLane* findLane(Id laneId) noexcept;
    const AutomationLane* findLane(Id laneId) const noexcept;
    const AutomationLane*
    findLane(AutomationTarget target, Id insertId = invalidId, int parameter = 0) const noexcept;
    bool operator==(const Channel&) const = default;
};

struct Track : Channel
{
    TrackKind kind = TrackKind::audio;
    // A MIDI track plays the instrument channel it aims at, on a MIDI channel
    // (several MIDI tracks may aim at one instrument; a multitimbral plugin
    // tells them apart by channel). Its volume scales the notes' velocities.
    Id midiTarget = invalidId;
    int midiChannel = 1; // 1..16
    // An aux track (kind == aux) carries one extra stereo output of the
    // instrument on an instrument channel: output 1 is the first beyond the main pair.
    Id auxTrack = invalidId;
    Id auxInsert = invalidId;
    int auxOutput = 0;
    int inputChannel = -1; // device input channel index (callback order), -1 = none
    // A stereo input bus by name (defined per device in Audio Setup); when set it
    // replaces inputChannel. A name the device does not define means no input.
    std::string inputBus;
    std::string midiInput; // MIDI input device name; empty = every enabled input
    // A MIDI track can also play the MIDI output device (the one chosen in Audio
    // Setup) on a channel: its clips and, while monitoring, its live input.
    bool midiOut = false;
    int midiOutChannel = 1; // 1..16
    bool armed = false;     // records when the transport records
    MonitorMode monitor = MonitorMode::automatic;
    std::vector<AudioClip> clips;      // kept sorted by start
    std::vector<MidiClip> midiClips;   // kept sorted by start
    std::vector<TakeGroup> takeGroups; // take lanes and comps (core/Comping.h), sorted by start
    // The folder track this track sits in (invalidId: none); a folder's own
    // tracks show under it while it is open.
    Id folder = invalidId;
    bool folderOpen = true;
    // Frozen: the track plays this render of its output before the fader (its
    // clips or instrument through its inserts and strip) instead of running them.
    std::string frozenMedia;

    AudioClip* findClip(Id clipId) noexcept;
    const AudioClip* findClip(Id clipId) const noexcept;
    void sortClips();
    MidiClip* findMidiClip(Id clipId) noexcept;
    const MidiClip* findMidiClip(Id clipId) const noexcept;
    void sortMidiClips();
    TakeGroup* findTakeGroup(Id groupId) noexcept;
    const TakeGroup* findTakeGroup(Id groupId) const noexcept;
    // The group whose comp a clip in `clips` belongs to (null for an ordinary clip).
    const TakeGroup* takeGroupOwning(Id clipId) const noexcept;
    // A take's clip in any of the track's groups, by the take's id.
    AudioClip* findTakeClip(Id takeId) noexcept;
    bool isMidi() const noexcept { return kind == TrackKind::midi; }
    bool isAux() const noexcept { return kind == TrackKind::aux; }
    bool isInstrument() const noexcept { return kind == TrackKind::instrument; }
    bool isFolder() const noexcept { return kind == TrackKind::folder; }
    bool isFrozen() const noexcept { return !frozenMedia.empty(); }
    // Whether the track makes audio (a MIDI track only makes notes; a folder none).
    bool hasAudio() const noexcept { return kind != TrackKind::midi && kind != TrackKind::folder; }
    // Whether an audio input is chosen (a channel or a bus, found on the device or not).
    bool hasAudioInput() const noexcept { return inputChannel >= 0 || !inputBus.empty(); }
    bool operator==(const Track&) const = default;
};

// A bus is a group (tracks route their output into it) or an FX track: a
// return that other channels feed through aux sends (a reverb or a delay shared
// by several sources). Both mix the same way; an FX track also shows in the
// arrange view as a row of its own, for its automation.
struct Bus : Channel
{
    bool fx = false;
    bool operator==(const Bus&) const = default;
};

// The master is a channel so the insert commands, the engine's instance cache
// and the editors work on its inserts as on any other channel's. Only its gain
// and inserts are used: it has no pan, sends, strip, routing or automation.
struct Master : Channel
{
    Master()
    {
        id = masterId;
        name = "Master";
    }
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

// The tempo track (core/TempoMap.h turns it into conversions). The project's
// tempoBpm and timeSignature hold at the start of the song; these change them
// later. A tempo change sits at a musical position, in quarter notes from the
// song's start (> 0): from there the tempo is `bpm` quarter notes a minute. With
// `ramp` the tempo glides (linearly over the quarter notes) from the previous
// tempo to this one instead of jumping at it.
struct TempoChange
{
    double quarter = 0.0;
    double bpm = 120.0;
    bool ramp = false;
    bool operator==(const TempoChange&) const = default;
};

// A time signature change at the start of a bar (0-based bar index, > 0).
struct MeterChange
{
    int bar = 0;
    TimeSignature signature;
    bool operator==(const MeterChange&) const = default;
};

struct Project
{
    // 2: instruments moved to instrument channels; MIDI tracks aim at them.
    // 3: parameters and automation lanes carry stable keys; row heights.
    // 4: vocal pitch edits on audio clips; the project key.
    // 5: sidechain keys may tap after the source's fader.
    // 6: the tempo track (tempo and meter changes); clip stretch, transpose and
    //    musical time base.
    // 7: take groups (take lanes and comps) on tracks.
    // 8: folder tracks; frozen tracks.
    // 9: controller events (CC, pitch bend, pressure) in MIDI clips.
    // 10: stereo input buses on tracks.
    // 11: inserts on the master.
    // 12: automation lanes' view (height, collapsed, hidden) and whether a
    //     channel's lanes are open in the arrange view.
    static constexpr int schemaVersion = 12;
    static constexpr int oldestReadableVersion = 2; // older files are refused

    std::string name = "Untitled";
    double sampleRate = 48000.0;
    double tempoBpm = 120.0;               // at the song's start
    TimeSignature timeSignature;           // at the song's start
    std::vector<TempoChange> tempoChanges; // later tempos, sorted by quarter (all > 0)
    std::vector<MeterChange> meterChanges; // later signatures, sorted by bar (all > 0)
    MusicalKey key;
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

    // Folders: the folder a track sits in (null when none or missing), how deep
    // it is nested, whether a closed folder above hides it, and whether a folder
    // above mutes or solos it (a folder's mute and solo act on what it holds).
    const Track* folderOf(const Track& track) const noexcept;
    int folderDepth(const Track& track) const noexcept;
    bool hiddenByFolder(const Track& track) const noexcept;
    bool mutedByFolder(const Track& track) const noexcept;
    bool soloedByFolder(const Track& track) const noexcept;
    // Whether `track` is `folder` or sits somewhere inside it.
    bool insideFolder(Id track, Id folder) const noexcept;

    // The MIDI tracks aiming at an instrument channel, in track order.
    std::vector<const Track*> midiFeeds(Id instrumentChannel) const;

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

// The project a freeze renders: only `trackId` sounds, at unity gain and centre
// pan with no sends, straight to the master (at unity), so the mix is the
// track's output before its fader. The MIDI tracks that play it keep playing;
// every other channel is silenced by its fader (not muted, so it can still key a
// sidechain before its fader), and folders mute and solo nothing.
Project freezeRenderProject(const Project& project, Id trackId);

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

} // namespace northlight::core
