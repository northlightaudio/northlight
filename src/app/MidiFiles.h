#pragma once

#include "core/Command.h"
#include "core/Model.h"
#include "core/TempoMap.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <string>
#include <utility>
#include <vector>

// Standard MIDI Files (type 0 and 1) to and from the model: the conversion between
// juce::MidiMessageSequence and MIDI clips (notes, control changes, pitch bend,
// channel and poly pressure), kept apart from the session and the UI so it can be
// tested. Reading and writing the bytes is juce::MidiFile's.
//
// Times: a file counts in ticks, the model in samples. Import goes ticks ->
// seconds through the file's own tempo map, then seconds -> samples at the
// project's rate (ticksToSamples, the one place it happens); export goes the
// other way through the project's tempo map (samplesToTicks), and writes the
// map's tempo and signature changes into the file's first track (a ramp as a
// tempo event every sixteenth note along it).

namespace aural::midifile
{

// A file's tempo changes, for turning ticks into seconds.
struct TempoMap
{
    struct Change
    {
        double tick = 0.0;
        double secondsPerQuarter = 0.5;
    };
    int ticksPerQuarter = 960;
    // SMPTE timing (a negative time format): ticks per second, else 0.
    double smpteTicksPerSecond = 0.0;
    std::vector<Change> changes; // sorted by tick; none = 120 bpm throughout

    // From the tempo meta events of every track (type 1 keeps them in the first).
    static TempoMap fromFile(const juce::MidiFile& file);
    double secondsAt(double tick) const noexcept;
    // The tempo at the start, in BPM (120 when the file sets none).
    double firstBpm() const noexcept;
};

core::SamplePosition ticksToSamples(double tick, const TempoMap& tempo, double sampleRate) noexcept;
double
samplesToTicks(core::SamplePosition samples, double sampleRate, double bpm, int ticksPerQuarter) noexcept;
double samplesToTicks(core::SamplePosition samples, const core::TempoMap& map, int ticksPerQuarter) noexcept;
// The file's tempo as the project's tempo track: the first tempo and the
// changes after it (none for an SMPTE file).
core::TempoState tempoStateOf(const TempoMap& tempo, const core::TempoState& base);

// One part of an imported file: a file track's events on one MIDI channel.
struct Part
{
    std::string name;
    int channel = 1;     // 1..16
    core::MidiClip clip; // starts at 0 (the file's start); ids unset
};

struct Imported
{
    std::vector<Part> parts;
    TempoMap tempo;
    bool hasTimeSignature = false;
    core::TimeSignature timeSignature;
    std::string error; // set when the file could not be read
};

// A file track's events on `channel` (1..16, or 0 for every channel) as a clip
// from the file's start: notes paired with their offs (a retrigger ends the
// note before it; a note still held ends at the track's last event), control
// changes, pitch bend, channel and poly pressure. The clip runs to its last event.
core::MidiClip clipFromSequence(const juce::MidiMessageSequence& sequence,
                                const TempoMap& tempo,
                                double sampleRate,
                                int channel = 0);

// Every file track, split by MIDI channel where it uses more than one (a type 0
// file keeps all its channels in one track), each with events made a part.
Imported fromMidiFile(const juce::MidiFile& file, double sampleRate);
Imported readMidiFile(const juce::File& file, double sampleRate);

// A track's clips as one file track on its MIDI channel: events at their
// timeline places (tick 0 = the song's start), cut at each clip's end, a
// controller value a trim left before a clip at the clip's start.
juce::MidiMessageSequence sequenceFromClips(const std::vector<const core::MidiClip*>& clips,
                                            int channel,
                                            double sampleRate,
                                            double bpm,
                                            int ticksPerQuarter);
juce::MidiMessageSequence sequenceFromClips(const std::vector<const core::MidiClip*>& clips,
                                            int channel,
                                            const core::TempoMap& map,
                                            int ticksPerQuarter);

// A type 1 file at 960 PPQ: a first track with the project's tempo and time
// signature, then one track per (track, its clips), named after the track.
juce::MidiFile
toMidiFile(const core::Project& project,
           const std::vector<std::pair<const core::Track*, std::vector<const core::MidiClip*>>>& tracks);
// Writes it; returns an error or empty.
std::string writeMidiFile(const juce::File& file, const juce::MidiFile& midi);

constexpr int exportTicksPerQuarter = 960;

} // namespace aural::midifile
