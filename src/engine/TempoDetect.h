#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Guessing a recording's tempo, so a loop can be stretched to the project's:
// from its file name ("Drums 124bpm.wav", "loop_90_Am.wav"), from its beats (an
// onset envelope's autocorrelation), and from its length (a loop is usually a
// whole number of bars). Not real-time.

namespace northlight::engine
{

struct TempoGuess
{
    double bpm = 0.0;        // 0 when nothing could be guessed
    double confidence = 0.0; // 0..1, how clearly the beats agreed
    enum class Source
    {
        none,
        name,
        beats,
        length,
    } source = Source::none;
};

// A tempo written in a file name, or 0: "124bpm", "124 BPM", "bpm124", or a
// separate number 60..200 between separators ("loop_90_Am").
double tempoFromName(const std::string& name);

// The tempo the beats of `channels` (each `length` samples) suggest, folded into
// [minBpm, maxBpm].
TempoGuess tempoFromBeats(const std::vector<const float*>& channels,
                          std::int64_t length,
                          double sampleRate,
                          double minBpm = 70.0,
                          double maxBpm = 180.0);

// The tempo at which `length` samples is a whole number of bars (1, 2, 4, 8, 16
// or 32 of `beatsPerBar` quarter notes) nearest to `near` (or to 120 when 0),
// within [minBpm, maxBpm]; 0 when none fits.
double tempoFromLength(std::int64_t length,
                       double sampleRate,
                       double beatsPerBar = 4.0,
                       double near = 0.0,
                       double minBpm = 70.0,
                       double maxBpm = 180.0);

// The best guess from all three: the name first, then a length that agrees with
// the beats (within 3%), then the beats, then the length alone.
TempoGuess guessTempo(const std::string& name,
                      const std::vector<const float*>& channels,
                      std::int64_t length,
                      double sampleRate);

} // namespace northlight::engine
