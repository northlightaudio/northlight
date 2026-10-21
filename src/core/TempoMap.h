#pragma once

#include "core/Model.h"

#include <vector>

// The tempo track as conversions between the timeline (samples) and musical time
// (quarter notes, bars and beats). Built from a project's start tempo and
// signature and its tempo and meter changes; immutable afterwards, and every
// query is noexcept and allocation-free, so the real-time thread can use a copy
// (the snapshot carries one).
//
// Musical positions are quarter notes from the song's start. Tempos count quarter
// notes a minute whatever the signature (as Project::tempoBpm always has); beats
// are counted in the signature's denominator, so a bar of 6/8 has six beats of an
// eighth each. Positions before zero continue the first tempo and signature.

namespace aural::core
{

// A snap grid: every `bars` bars (from bar 1) when bars > 0, else every
// `quarters` quarter notes from the start of each bar (a line also falls on every
// bar line, so a division that does not fill a bar evenly restarts at each bar).
struct GridStep
{
    int bars = 0;
    double quarters = 1.0;
    bool operator==(const GridStep&) const = default;
};

class TempoMap
{
public:
    // 120 BPM, 4/4, 48 kHz.
    TempoMap() noexcept;
    TempoMap(double sampleRate,
             double bpm,
             TimeSignature signature,
             const std::vector<TempoChange>& tempoChanges = {},
             const std::vector<MeterChange>& meterChanges = {});

    double sampleRate() const noexcept { return sampleRate_; }
    // One tempo and one signature for the whole song.
    bool constant() const noexcept { return tempo_.size() == 1 && meter_.size() == 1; }

    // ----- time ------------------------------------------------------------------
    double quartersAt(double samples) const noexcept;
    double samplesAt(double quarters) const noexcept;
    SamplePosition positionAt(double quarters) const noexcept;
    double bpmAtQuarter(double quarters) const noexcept;
    double bpmAt(SamplePosition position) const noexcept
    {
        return bpmAtQuarter(quartersAt(double(position)));
    }
    double samplesPerQuarterAt(SamplePosition position) const noexcept;
    // The tempo at the song's start.
    double startBpm() const noexcept { return tempo_.front().bpm0; }

    // ----- bars ------------------------------------------------------------------
    static double quartersPerBeat(TimeSignature signature) noexcept;
    static double quartersPerBar(TimeSignature signature) noexcept;
    TimeSignature signatureAtBar(int bar) const noexcept; // 0-based bar index
    TimeSignature signatureAt(SamplePosition position) const noexcept;
    double barStartQuarter(int bar) const noexcept;
    int barAtQuarter(double quarters) const noexcept; // the bar holding the position (floor)
    SamplePosition barStart(int bar) const noexcept { return positionAt(barStartQuarter(bar)); }
    BarsBeats barsBeatsAt(SamplePosition position) const noexcept;
    SamplePosition positionAt(BarsBeats position) const noexcept;

    // ----- grid ------------------------------------------------------------------
    // The nearest grid line to a position.
    SamplePosition snap(SamplePosition position, GridStep step) const noexcept;
    // A position moved by `count` grid steps in musical time (bars stay bars; a
    // quarter-note step moves that many quarters), keeping an off-grid offset.
    SamplePosition move(SamplePosition position, GridStep step, int count) const noexcept;
    // The grid line at or after / before a position.
    SamplePosition nextLine(SamplePosition position, GridStep step) const noexcept;
    // The finest of bar, beat, half, quarter or eighth of a beat that is at least
    // `minPixels` wide at the zoom around `at` (whole bars doubled when even a bar
    // is too fine), as gridSamples does for one tempo.
    GridStep adaptiveStep(double samplesPerPixel, int minPixels, SamplePosition at) const noexcept;
    // A step's length in samples around a position.
    double stepSamples(GridStep step, SamplePosition at) const noexcept;

    // A beat on the timeline (bar and beat 0-based).
    struct Beat
    {
        SamplePosition position = 0;
        int bar = 0;
        int beat = 0;
    };
    // The first beat at or after a timeline position, and the one after a beat.
    Beat beatAtOrAfter(double samples) const noexcept;
    Beat nextBeat(const Beat& beat) const noexcept;

    // The changes as built (the start's entry first), for drawing the tempo track.
    struct TempoPoint
    {
        double quarter = 0.0;
        double sample = 0.0;
        double bpm0 = 120.0; // at `quarter`
        double bpm1 = 120.0; // at the next point's quarter (a ramp), else bpm0
    };
    struct MeterPoint
    {
        int bar = 0;
        double quarter = 0.0;
        TimeSignature signature;
    };
    const std::vector<TempoPoint>& tempoPoints() const noexcept { return tempo_; }
    const std::vector<MeterPoint>& meterPoints() const noexcept { return meter_; }

private:
    size_t tempoIndexAtQuarter(double quarters) const noexcept;
    size_t tempoIndexAtSample(double samples) const noexcept;
    size_t meterIndexAtBar(int bar) const noexcept;
    size_t meterIndexAtQuarter(double quarters) const noexcept;
    double quarterEnd(size_t index) const noexcept;

    double sampleRate_ = 48000.0;
    std::vector<TempoPoint> tempo_; // never empty; the first at quarter 0
    std::vector<MeterPoint> meter_; // never empty; the first at bar 0
};

TempoMap tempoMapOf(const Project& project);

// Rewrites every musical position (MIDI clips and notes, clips that follow the
// tempo, automation points, markers) from one tempo map to another, keeping its
// place in bars and beats. Clips that follow the tempo also keep their length in
// beats, re-stretched to fit.
void remapMusicalTime(Project& project, const TempoMap& from, const TempoMap& to);

} // namespace aural::core
