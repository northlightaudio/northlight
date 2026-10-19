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

    SamplePosition end() const noexcept { return start + length; }
    bool operator==(const AudioClip&) const = default;
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

// What tracks and buses share: the fader section and routing.
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
    std::vector<Send> sends;

    Send* findSend(Id busId) noexcept;
    bool operator==(const Channel&) const = default;
};

struct Track : Channel
{
    int inputChannel = -1; // device input channel index (callback order), -1 = none
    bool armed = false;    // records when the transport records
    MonitorMode monitor = MonitorMode::automatic;
    std::vector<AudioClip> clips; // kept sorted by start

    AudioClip* findClip(Id clipId) noexcept;
    const AudioClip* findClip(Id clipId) const noexcept;
    void sortClips();
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
BarsBeats samplesToBarsBeats(SamplePosition position,
                             double sampleRate,
                             double tempoBpm,
                             TimeSignature signature) noexcept;
SamplePosition
barsBeatsToSamples(BarsBeats position, double sampleRate, double tempoBpm, TimeSignature signature) noexcept;

} // namespace aural::core
