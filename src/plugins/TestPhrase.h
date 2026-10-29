#pragma once

#include <optional>
#include <string>
#include <vector>

// A fixed phrase for listening to an instrument and measuring it
// (docs/plans/electric-piano.md, section 4): single notes up the keyboard at
// three velocities, then chords, a repeated note and a run under the sustain
// pedal. The same phrase every time, so a change can be heard and measured
// against the last render (scripts/analysis/ep-measure.py reads the cue sheet
// written beside the audio). JUCE-free; not real-time.

namespace northlight::plugins::phrase
{

struct Cue
{
    enum class Kind
    {
        note,
        pedalDown,
        pedalUp
    };
    Kind kind = Kind::note;
    double seconds = 0.0; // when it starts
    int note = 60;
    int velocity = 100;
    double length = 1.0; // how long the key is held (notes only)
    std::string part;    // "single", "chord", "repeat" or "pedal"
};

// The single notes: every fourth key from E0 (28) to E6 (100), each at 40, 80
// and 120, held a second, one and a half seconds apart.
inline constexpr int singleLowest = 28;
inline constexpr int singleHighest = 100;
inline constexpr int singleStep = 4;
inline constexpr int singleVelocities[] = {40, 80, 120};
inline constexpr double singleHold = 1.0;
inline constexpr double singleSpacing = 1.5;

std::vector<Cue> standardPhrase();

struct Rendered
{
    double sampleRate = 48000.0;
    std::vector<float> left, right;
};

// The phrase through a fresh built-in instrument at its defaults, with
// `tailSeconds` after the last cue; nothing for a type that is not a built-in
// instrument.
std::optional<Rendered>
render(const std::string& type, const std::vector<Cue>& cues, double sampleRate, double tailSeconds = 3.0);

// The cue sheet as text, one cue a line: seconds, kind, note, velocity, length, part.
std::string cueSheet(const std::vector<Cue>& cues);

} // namespace northlight::plugins::phrase
