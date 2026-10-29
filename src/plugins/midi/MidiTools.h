#pragma once

#include "core/Vocal.h"
#include "engine/Insert.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// What the built-in MIDI effects share: note-value rates, the key an effect
// plays in, a step clock for tempo-synced effects and a small random generator.
// JUCE-free; nothing here allocates on the real-time path.

namespace northlight::plugins::midi
{

// ----- note values -----------------------------------------------------------------------

// The rates a synced effect offers, longest first, with their length in quarter notes.
struct NoteValue
{
    const char* name;
    double quarters;
};
inline constexpr std::array<NoteValue, 16> noteValues{{
    {"1/1", 4.0},
    {"1/2", 2.0},
    {"1/2 dotted", 3.0},
    {"1/2 triplet", 4.0 / 3.0},
    {"1/4", 1.0},
    {"1/4 dotted", 1.5},
    {"1/4 triplet", 2.0 / 3.0},
    {"1/8", 0.5},
    {"1/8 dotted", 0.75},
    {"1/8 triplet", 1.0 / 3.0},
    {"1/16", 0.25},
    {"1/16 dotted", 0.375},
    {"1/16 triplet", 1.0 / 6.0},
    {"1/32", 0.125},
    {"1/32 triplet", 1.0 / 12.0},
    {"1/64", 0.0625},
}};
// Indices of the common ones, for defaults.
inline constexpr int noteValueQuarter = 4;
inline constexpr int noteValueEighth = 7;
inline constexpr int noteValueSixteenth = 10;

inline std::vector<std::string> noteValueChoices()
{
    std::vector<std::string> names;
    for (const auto& v : noteValues)
        names.emplace_back(v.name);
    return names;
}

inline double noteValueQuarters(float choice) noexcept
{
    const int i =
        std::clamp(static_cast<int>(std::lround(choice)), 0, static_cast<int>(noteValues.size()) - 1);
    return noteValues[static_cast<size_t>(i)].quarters;
}

// A rate's length in samples at the play head's tempo (at least one sample).
inline double samplesPerQuarters(const engine::PlayHead& head, double quarters) noexcept
{
    const double perQuarter = head.sampleRate * 60.0 / std::max(1.0, head.tempoBpm);
    return std::max(1.0, quarters * perQuarter);
}

// ----- keys ------------------------------------------------------------------------------

inline constexpr std::array<const char*, 12> pitchClassNames{
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
inline constexpr std::array<core::ScaleKind, 8> scaleKinds{core::ScaleKind::major,
                                                           core::ScaleKind::minor,
                                                           core::ScaleKind::harmonicMinor,
                                                           core::ScaleKind::dorian,
                                                           core::ScaleKind::mixolydian,
                                                           core::ScaleKind::majorPentatonic,
                                                           core::ScaleKind::minorPentatonic,
                                                           core::ScaleKind::chromatic};

inline std::vector<std::string> rootChoices()
{
    return {pitchClassNames.begin(), pitchClassNames.end()};
}
inline std::vector<std::string> scaleChoices()
{
    std::vector<std::string> names;
    for (const auto s : scaleKinds)
    {
        std::string name = core::scaleName(s);
        if (!name.empty())
            name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        names.push_back(std::move(name));
    }
    return names;
}

// The key an effect plays in: the project's (from the play head) when it follows
// it and one is set, else its own. `root` 0..11, `mask` bit n = pitch class n.
struct Key
{
    int root = 0;
    std::uint16_t mask = 0;
    bool contains(int note) const noexcept { return (mask >> (((note % 12) + 12) % 12) & 1u) != 0; }
};

inline Key keyFor(const engine::PlayHead& head, bool followProject, float ownRoot, float ownScale) noexcept
{
    if (followProject && head.keyRoot >= 0 && head.keyMask != 0)
        return {head.keyRoot, head.keyMask};
    core::MusicalKey own;
    own.root = std::clamp(static_cast<int>(std::lround(ownRoot)), 0, 11);
    own.scale = scaleKinds[static_cast<size_t>(
        std::clamp(static_cast<int>(std::lround(ownScale)), 0, static_cast<int>(scaleKinds.size()) - 1))];
    return {own.root, own.mask()};
}

// The scale's pitch classes above the root, in order (0 first), and how many.
struct Degrees
{
    std::array<int, 12> steps{};
    int count = 0;
};
inline Degrees degreesOf(const Key& key) noexcept
{
    Degrees d;
    for (int i = 0; i < 12; ++i)
        if (key.contains(key.root + i))
            d.steps[static_cast<size_t>(d.count++)] = i;
    if (d.count == 0)
        d.steps[static_cast<size_t>(d.count++)] = 0;
    return d;
}
// The note of scale degree `degree` (0 = the root at `rootNote`; any integer,
// wrapping into octaves).
inline int degreeNote(const Degrees& d, int rootNote, int degree) noexcept
{
    const int octave = degree >= 0 ? degree / d.count : -((-degree + d.count - 1) / d.count);
    const int index = degree - octave * d.count;
    return rootNote + 12 * octave + d.steps[static_cast<size_t>(index)];
}

// ----- random ----------------------------------------------------------------------------

// xorshift32: cheap, real-time safe and repeatable from its seed.
class Random
{
public:
    void seed(std::uint32_t s) noexcept { state_ = s != 0 ? s : 0x9E3779B9u; }
    std::uint32_t next() noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }
    // 0 <= x < 1.
    double unit() noexcept { return static_cast<double>(next() >> 8) / static_cast<double>(1u << 24); }
    // -1 < x < 1.
    double bipolar() noexcept { return unit() * 2.0 - 1.0; }

private:
    std::uint32_t state_ = 0x9E3779B9u;
};

// ----- steps -----------------------------------------------------------------------------

// The step times of a synced effect on the effect's clock. While the transport
// plays, steps fall on the song's grid (multiples of the rate from the song's
// start); stopped, they run from the moment the effect started them (`restart`).
class StepClock
{
public:
    // Starts free-running steps at `clock` (the first step is there).
    void restart(std::int64_t clock) noexcept
    {
        anchor_ = clock;
        anchored_ = true;
    }
    void stop() noexcept { anchored_ = false; }
    bool running() const noexcept { return anchored_; }

    // The first step at or after `from` (and before `to`), or -1 for none in
    // the span. `blockClock` and `head` describe the current block; `rate` is
    // in quarter notes. Steps are numbered in `index` (from the song's start
    // while playing, from the restart while stopped), for swing and patterns.
    std::int64_t next(std::int64_t from,
                      std::int64_t to,
                      std::int64_t blockClock,
                      const engine::PlayHead& head,
                      double rate,
                      std::int64_t& index) const noexcept
    {
        if (!anchored_ || to <= from)
            return -1;
        const double stepSamples = samplesPerQuarters(head, rate);
        double at = 0.0;
        if (head.playing)
        {
            // The song position at `from`, then the next multiple of the rate.
            const double perSample = head.quartersPerSample();
            const double q = head.quarterNotes + static_cast<double>(from - blockClock) * perSample;
            const double k = std::ceil(q / rate - 1e-9);
            index = static_cast<std::int64_t>(k);
            at = static_cast<double>(from) + (k * rate - q) / perSample;
        }
        else
        {
            const double elapsed = static_cast<double>(from - anchor_);
            const double k = std::ceil(elapsed / stepSamples - 1e-9);
            index = static_cast<std::int64_t>(std::max(0.0, k));
            at = static_cast<double>(anchor_) + static_cast<double>(index) * stepSamples;
        }
        const auto step = static_cast<std::int64_t>(std::llround(at));
        return step >= from && step < to ? step : (step < from ? from : -1);
    }

private:
    std::int64_t anchor_ = 0;
    bool anchored_ = false;
};

} // namespace northlight::plugins::midi
