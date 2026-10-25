#pragma once

#include "engine/Insert.h"

#include <array>
#include <memory>
#include <string>

// Orbit: a studio tape machine, for glue and colour on a bus or the master. The
// tape is a Jiles-Atherton hysteresis model (the magnetisation lagging the field,
// so loud passages compress softly, the harmonics are mostly odd, and the highs,
// boosted by the record EQ, saturate first), run at 2x to 4x the session rate.
// Around it, the machine: the record and playback EQ of the chosen standard and
// speed (NAB or CCIR; AES at 30 ips), the bias (under-biased tape is gritty and
// bright, over-biased clean and dark), the head bump and gap loss of the tape
// speed, the operating level (how hard 0 VU hits the tape), three tape formulas,
// hiss, and the transport's wow and flutter. Link keeps the loudness steady while
// INPUT drives the tape. A fixed 1.5 ms of latency holds the flutter's wander, the
// dry signal delayed to match. JUCE-free.

namespace northlight::plugins::orbit
{

inline const std::string type = "northlight.orbit";

enum Param
{
    input,   // dB, the record level into the tape
    bias,    // %, -100 (under-biased) .. +100 (over-biased); 0 is calibrated
    speed,   // choice: Speed
    eq,      // choice: Equalisation (ignored at 30 ips: AES)
    level,   // choice: OperatingLevel
    tape,    // choice: Formula
    hiss,    // %, the tape's noise
    flutter, // %, wow and flutter, weighted rms (0 .. 0.3)
    output,  // dB
    mix,     // % wet
    link,    // Off / On: OUTPUT follows INPUT the other way
    meter,   // choice: MeterPoint (what the VU meters read; no effect on the sound)
    count
};

enum Speed
{
    ips7,  // 7.5 ips
    ips15, // 15 ips
    ips30, // 30 ips
};
enum Equalisation
{
    nab,
    ccir,
};
enum OperatingLevel
{
    plus3, // +3 dB over 185 nWb/m
    plus6,
    plus9,
};
enum Formula
{
    classic, // an older high-output tape: saturates early, a wider loop
    hot,     // a high-output, high-headroom tape
    modern,  // the most headroom and the narrowest loop: the cleanest
};
enum MeterPoint
{
    meterInput,  // after INPUT
    meterTape,   // the tape's playback, before OUTPUT
    meterOutput, // the output
};

inline constexpr float maxFlutterPercent = 0.3f;
// The fixed delay that holds the flutter's wander (and the latency reported).
inline constexpr double flutterDelaySeconds = 0.0015;
// The meters' reference: 0 VU is -18 dBFS at every point.
inline constexpr float vuReferenceDbfs = -18.0f;

float speedIps(float speedChoice) noexcept;
float operatingLevelDb(float levelChoice) noexcept;
// The corner of the record pre-emphasis (and playback de-emphasis) for a speed
// and a standard: 1 / (2 pi tau) of its time constant (NAB 50 us; CCIR 70 us at
// 7.5 ips, 35 us at 15; AES 17.5 us at 30 ips whatever the standard).
double emphasisHz(float speedChoice, float eqChoice) noexcept;
// Whether the standard has NAB's low-frequency time constant (3180 us).
bool hasLowEmphasis(float speedChoice, float eqChoice) noexcept;
// The playback head's low-frequency bump: its centre rises with the speed.
double headBumpHz(float speedChoice) noexcept;
double headBumpDb(float speedChoice) noexcept;
// The gap and spacing loss: the highs a slower tape cannot carry.
double gapLossHz(float speedChoice, double sampleRate) noexcept;
// The hiss's level (dBFS rms), or -inf at 0%: a slower tape hisses more.
float hissDbfs(float hissPercent, float speedChoice) noexcept;

// The Jiles-Atherton parameters of a formula at a bias (normalised units).
struct TapeModel
{
    double ms = 1.0; // saturation magnetisation
    double a = 0.33; // the anhysteretic curve's shape
    double alpha = 1.6e-3;
    double k = 0.47875; // the loop's width (pinning)
    double c = 0.95;    // the reversible share: bias pushes it towards 1
};
TapeModel tapeModel(float formulaChoice, float biasPercent) noexcept;
// The field the tape sees per unit of input (at the operating level), and the
// gain that brings its small-signal output back to unity.
double fieldGain(float levelChoice) noexcept;
double makeupGain(const TapeModel& m) noexcept;

// One channel of tape: the hysteresis solved per sample (second-order
// Runge-Kutta). Real-time safe.
class Hysteresis
{
public:
    void prepare(double sampleRate) noexcept;
    void setModel(const TapeModel& m) noexcept { model_ = m; }
    void reset() noexcept;
    // The magnetisation for a field `h`.
    double process(double h) noexcept;

private:
    double slope(double m, double h, double hd) const noexcept;
    TapeModel model_;
    double t_ = 1.0 / 192000.0;
    double m_ = 0.0, h1_ = 0.0, hd1_ = 0.0;
};

// What the card reads from each frame: input and output peaks; the tape's
// compression (dB) as the reduction; the meters' point (MeterPoint) as `extra`
// (left) and `extra2` (right): each channel's VU reading, the rectified signal through
// a VU's movement (99% of a step in 300 ms), scaled so a sine reads its rms. The feedback's transport carries
// the song's position for the tape counter.
std::unique_ptr<engine::InsertProcessor> create();

} // namespace northlight::plugins::orbit
