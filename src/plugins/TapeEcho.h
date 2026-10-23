#pragma once

#include "engine/Insert.h"
#include "plugins/Biquad.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

// The tape echo: a tape loop machine. One record head and four playback heads at
// fixed spacings (1, 2, 3 and 4 units from the record head), any of eight head
// combinations playing at once, and the heads fed back to the record head. The
// motor speed sets the delay: a change glides like a motor with a flywheel, and
// because every head reads the same moving tape, the pitch bends while it
// settles, as on the real thing. Wow and flutter wobble the speed; the record
// path carries the loop's tone (bass and treble), tape saturation with
// pre- and de-emphasis (antiderivative anti-aliased), the tape's high-frequency
// loss and, with age, hiss and dropouts, so each pass round the loop darkens,
// thickens and wears a little more, and feedback past 100% self-oscillates into
// the saturation instead of exploding. Two tape tracks give stereo: the heads
// panned across the field, ping-pong or true stereo. A short dispersive spring
// reverb can be sent from the echoes. Zero latency. JUCE-free.

namespace northlight::plugins::tapeecho
{

inline const std::string type = "northlight.tapeecho";

enum Param
{
    heads,    // choice: headModes
    time,     // ms, head 1's delay (the motor speed), free
    sync,     // 0 off, else head 1's note value at the project tempo (plugins/BeatSync.h)
    feedback, // %, 0..120 (past 100 the loop self-oscillates into the tape)
    bass,     // dB, low shelf in the loop
    treble,   // dB, high shelf in the loop
    wow,      // %, slow speed drift
    flutter,  // %, fast speed jitter
    age,      // %, high-frequency loss, hiss and dropouts
    drive,    // dB, the record level into the tape
    spread,   // %, stereo width of the echoes
    stereo,   // choice: StereoMode
    spring,   // %, spring reverb on the echoes
    mix,      // % wet
    killDry,  // 0 off, 1 on: no dry signal (for a send)
    output,   // dB
    count
};

inline constexpr int headCount = 4;
// Each playback head's distance from the record head, in units of head 1's.
inline constexpr std::array<float, headCount> headSpacing{1.0f, 2.0f, 3.0f, 4.0f};

struct HeadMode
{
    const char* label; // on the selector (up to four characters)
    const char* name;  // for the tooltip
    std::uint8_t mask; // bit k: playback head k + 1
};
inline constexpr std::array<HeadMode, 8> headModes{{
    {"1", "Head 1", 0b0001},
    {"2", "Head 2", 0b0010},
    {"3", "Head 3", 0b0100},
    {"4", "Head 4", 0b1000},
    {"13", "Heads 1 + 3", 0b0101},
    {"24", "Heads 2 + 4", 0b1010},
    {"134", "Heads 1 + 3 + 4", 0b1101},
    {"1234", "All four heads", 0b1111},
}};

enum StereoMode
{
    stereoHeads,    // one track; the active heads alternate left and right
    stereoPingPong, // two tracks, the feedback crossed: repeats alternate sides
    stereoTracks,   // two tracks, left and right kept apart
};

inline constexpr float minTimeMs = 20.0f;
inline constexpr float maxTimeMs = 1000.0f;
inline constexpr float maxFeedbackPercent = 120.0f;
// The tape's headroom: it saturates toward this level (about -6 dBFS).
inline constexpr float tapeHeadroom = 0.5f;
// The loop's fixed filters and shelf corners.
inline constexpr double loopHighPassHz = 45.0;
inline constexpr double bassHz = 220.0;
inline constexpr double trebleHz = 2800.0;
// The motor: a damped spring on the logarithm of the delay (a flywheel's settle,
// with a little overshoot).
inline constexpr double motorHz = 1.6;
inline constexpr double motorDamping = 0.7;
// Wow and flutter at 100%: the peak speed deviation.
inline constexpr float wowDepth = 0.008f;
inline constexpr float flutterDepth = 0.0025f;
// The spring: its transition frequency (the dispersion lives below it).
inline constexpr double springTransitionHz = 4400.0;

std::uint8_t headMask(float choice) noexcept;
int activeHeadCount(std::uint8_t mask) noexcept;
// Head 1's delay in seconds: the synced note value at the tempo, or the free
// time; clamped to the motor's range.
double headOneSeconds(float timeMs, float syncChoice, double tempoBpm) noexcept;
// Whether a synced note value is longer than the motor can run (clamped).
bool syncClamped(float syncChoice, double tempoBpm) noexcept;
// The tape's high-frequency loss corner for an age (0..100%) and a speed (head
// 1's delay): older tape and a slower motor sound darker.
double highLossHz(float agePercent, double headOneSeconds) noexcept;

// The record path's linear filters, designed the same for the processor and the
// card (so the card's feedback state is the loop that runs).
struct LoopFilters
{
    Biquad highPass, bassShelf, trebleShelf, highLoss;
    void
    design(double sampleRate, float bassDb, float trebleDb, float agePercent, double headOneSeconds) noexcept;
    // The loop's magnitude at a frequency (linear).
    double magnitude(double hz, double sampleRate) const noexcept;
};

// The loop gain at its loudest frequency: the feedback times the loop filters'
// peak (the saturation's small-signal slope is 1, and the heads' sum is
// normalised). At or past 1 the loop self-oscillates.
float loopGain(float feedbackPercent,
               float bassDb,
               float trebleDb,
               float agePercent,
               double headOneSeconds,
               double sampleRate) noexcept;

// What the card reads from each feedback frame: input and output peaks, the
// tape saturation (dB of compression at the record head) as the reduction, the
// level written at the record head as `extra` (the echoes travelling round the
// loop), and the motor's head-1 delay in seconds as `extra2` (it glides).
std::unique_ptr<engine::InsertProcessor> create();

} // namespace northlight::plugins::tapeecho
