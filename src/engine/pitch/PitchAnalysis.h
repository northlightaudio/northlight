#pragma once

#include "core/Vocal.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Pitch analysis of a monophonic vocal (docs/design/13-pitch-editing.md, "Analysis").
//
//   1. A pitch curve, pYIN-style: YIN's cumulative mean normalised difference per
//      5 ms frame, every dip a pitch candidate weighted by how many thresholds of a
//      beta prior pick it, then a Viterbi pass over the candidates and an unvoiced
//      state so the curve does not jump octaves or flicker in and out of voicing.
//   2. Pitch marks: one per glottal pulse in voiced audio, for the resynthesis.
//   3. Notes: voiced runs split where the slow pitch moves to a new level and at
//      loudness dips between syllables; each note's centre is the loudness-weighted
//      mean of its stable middle; the glides between joined notes become their
//      transitions.
//   4. Unvoiced segments sorted into breaths, consonants and silence.
//
// JUCE-free and deterministic, so the same media always gives the same notes.

namespace aural::engine::pitch
{

struct AnalysisOptions
{
    double minHz = 60.0;
    double maxHz = 1100.0;
    int threads = 0; // 0 = the machine's hardware threads
};

struct PitchAnalysis
{
    // Bumped whenever the analysis would give different notes; cached analyses of
    // an older version are rebuilt, and VocalEdit::analysisVersion records it.
    static constexpr int version = 1;

    double sampleRate = 0.0;
    std::int64_t length = 0;            // source samples
    int hop = 0;                        // source samples per frame; frame f is centred on sample f * hop
    std::vector<float> cents;           // per frame: the pitch, 0 when unvoiced
    std::vector<float> voicing;         // per frame: the probability that it is voiced
    std::vector<float> levelDb;         // per frame: RMS level
    std::vector<std::int64_t> marks;    // pitch marks in voiced audio, sorted
    std::vector<core::VocalNote> notes; // as found (no ids; target = centre)
    std::vector<core::VocalSegment> segments; // every unvoiced stretch (no ids)

    int frames() const noexcept { return static_cast<int>(cents.size()); }
    // The pitch at a sample (linear between frames); 0 where unvoiced.
    float centsAt(std::int64_t sample) const noexcept;
    // A VocalEdit holding the notes and segments as found, playing as recorded.
    core::VocalEdit initialEdit() const;

    std::string serialise() const;
    static bool deserialise(const std::string& bytes, PitchAnalysis& out);
};

// `channels` are the media's samples at `sampleRate`; the analysis runs on their mix.
// `progress` (0..1) may return false to abandon the analysis (the result is empty).
PitchAnalysis analyse(const std::vector<std::vector<float>>& channels,
                      double sampleRate,
                      const std::function<bool(double)>& progress = {},
                      const AnalysisOptions& options = {});

inline float hzToCents(double hz) noexcept;
inline double centsToHz(double cents) noexcept;

} // namespace aural::engine::pitch

#include <cmath>

namespace aural::engine::pitch
{
inline float hzToCents(double hz) noexcept
{
    return static_cast<float>(1200.0 * std::log2(hz / 440.0) + 6900.0);
}
inline double centsToHz(double cents) noexcept
{
    return 440.0 * std::exp2((cents - 6900.0) / 1200.0);
}
} // namespace aural::engine::pitch
