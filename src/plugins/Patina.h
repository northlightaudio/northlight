#pragma once

#include "engine/Insert.h"
#include "engine/rt/TelemetryRing.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Patina: a vintage colour processor. Six stages age a sound in independent
// ways, in the order a real chain would: Heat (a circuit's saturation), Chamber
// (a short spring, plate or room), Drift (a transport's wow and flutter), Grain
// (the medium's own noise: vinyl, tape hiss, mains hum, room air), Wear (the
// medium's age: lost highs, dropouts, azimuth skew) and Crush (an early
// sampler's bits and rate). Each stage has a switch, a main amount and a
// Breath that lets the amount wander over time; one Age control scales every
// stage at once, from untouched to ruined. An output section (tilt, low and
// high cut, width, gain) finishes it. No latency: Drift delays the signal by
// its own wobble only, as a tape machine does. JUCE-free.

namespace northlight::plugins::patina
{

inline const std::string type = "northlight.patina";

enum Param
{
    age,  // %, the macro over every stage's amount (50 = as set)
    pace, // %, how quickly the stages breathe (slow .. quick)

    heatOn,        // Off / On
    heatCharacter, // choice: Character
    heatDrive,     // %, the drive into the curve
    heatTone,      // %, -100 dark .. +100 bright (a tilt after the curve)
    heatBreath,    // %

    chamberOn,
    chamberType,  // choice: Space
    chamberMix,   // %, the wet level
    chamberDecay, // %
    chamberTone,  // %, -100 dark .. +100 bright
    chamberBreath,

    driftOn,
    driftMachine, // choice: Machine
    driftWow,     // %
    driftFlutter, // %
    driftRate,    // %, 0 = half speed, 50 = as built, 100 = double
    driftStereo,  // %, how far the sides wobble apart
    driftBreath,

    grainOn,
    grainSource,  // choice: Source
    grainAmount,  // %, the noise level
    grainTexture, // %, per source: crackle density, hiss colour, hum buzz, air tone
    grainFollow,  // %, -100 ducks under the input .. +100 rides with it
    grainBreath,

    wearOn,
    wearLoss,     // %, high-frequency loss
    wearDropouts, // %, how often and how deep the dropouts are
    wearSkew,     // %, azimuth: the channels drifting apart in time and tone
    wearBreath,

    crushOn,
    crushFlavour, // choice: Flavour
    crushBits,    // bits, 16 .. 2
    crushRate,    // Hz, 48000 .. 1000
    crushBreath,

    tone,    // %, -100 dark .. +100 bright (a tilt about 900 Hz)
    lowCut,  // Hz, 20 (off) .. 600
    highCut, // Hz, 20000 (off) .. 2000
    width,   // %, 0 mono .. 200
    output,  // dB, -24 .. +12
    count
};

// The stages, in signal order.
enum Stage
{
    heat,
    chamber,
    drift,
    grain,
    wear,
    crush,
    stageCount
};

enum Character
{
    tube, // a biased valve: even harmonics, soft
    tape, // tape: pre-emphasised so the highs saturate first, with a head bump
    iron, // a transformer: the lows driven hardest, a firm knee
    fuzz, // a broken stage: starved (a dead zone that sputters) and clipped
};
enum Space
{
    room,
    spring,
    plate,
};
enum Machine
{
    reel, // studio tape: a slow, wandering wow and fine flutter
    deck, // a cassette deck: quicker, irregular wow, flutter and scrape
    disc, // a record: a warp turning at 33 1/3 rpm
};
enum Source
{
    vinyl, // crackle and pops at a density, surface noise, a low rumble
    hiss,  // tape hiss, shaped
    hum50, // mains hum at 50 Hz with its harmonics
    hum60, // the same at 60 Hz
    air,   // a room's tone: soft, wide, with a low HVAC rumble
};
enum Flavour
{
    soft,   // filtered converters: smooth, steppy only in the bits
    hard,   // no filters: aliasing and imaging, an early sampler
    mulaw,  // mu-law companding: an 8-bit telephone or drum machine
    jitter, // no filters and an unsteady clock
};

// The parameter that is each stage's switch, and its main amount.
int onParam(int stage) noexcept;
int breathParam(int stage) noexcept;

// Age over an amount (both 0..1, Age 0.5 = the amount as set): below the middle
// Age scales the amount toward nothing; above it, the amount moves toward full
// in proportion to how much of it is used, so a light touch stays light and a
// heavy one heads for ruin.
float aged(float amount, float ageFraction) noexcept;

// Heat's static curve as the processor runs it (without the emphasis filters):
// the output for input x at an amount (0..1), level-compensated.
float heatTransfer(int character, float amount, float x) noexcept;
// The drive (dB) for an amount.
float heatDriveDb(float amount) noexcept;
// The chamber's decay time (seconds to fall 60 dB) for a space and decay (0..1).
float chamberDecaySeconds(int space, float decay) noexcept;
// Wear's high cut (Hz) for a loss (0..1).
float wearCutoffHz(float loss) noexcept;
// Crush's bits and rate as an amount (0 = clean .. 1 = the most) and back.
float crushBitsFor(float amount) noexcept;
float crushRateFor(float amount) noexcept;
float crushAmountOfBits(float bits) noexcept;
float crushAmountOfRate(float hz) noexcept;

// What the card's stage tiles show, live. The real-time thread publishes a
// frame every 5 ms; the UI reads them (lock-free, like engine::ProcessorFeedback).
// Crush also publishes its input and output samples while an editor watches,
// for its scope.
class Telemetry
{
public:
    enum Value
    {
        amountHeat, // the effective amount (Age and Breath applied), 0..1, per stage
        amountChamber,
        amountDrift,
        amountGrain,
        amountWear,
        amountCrush,
        heatLevel,  // the stage's input peak, linear
        heatSquash, // dB the curve took off the peaks (>= 0)
        chamberWet, // the wet signal's peak, linear
        driftLeft,  // the speed's deviation, cents (signed, the frame's mean)
        driftRight,
        grainNoise,  // the noise's RMS, linear
        grainSignal, // the stage's input peak, linear
        wearDipDb,   // a dropout's dip, dB (>= 0)
        wearCutoff,  // the high cut now, Hz
        wearSkewMs,  // the right channel's lag behind the left, ms (signed)
        crushBits,   // the bits and rate in effect
        crushRate,
        valueCount
    };
    static constexpr int length = 1024; // frames
    static constexpr double frameSeconds = 0.005;
    static constexpr int scopeLength = 4096;

    Telemetry();

    // ----- controller thread ------------------------------------------------------------
    void prepare(double sampleRate) noexcept;

    // ----- real-time thread -------------------------------------------------------------
    // A frame of values (published when frameSeconds have passed).
    void publish(const std::array<float, valueCount>& values) noexcept { frames_.publish(values); }
    void scope(float in, float out) noexcept;

    // ----- UI thread --------------------------------------------------------------------
    using Frame = std::array<float, valueCount>;
    // Frames since `cursor` (the newest `maxFrames`), oldest first; advances it.
    int read(std::uint64_t& cursor, Frame* out, int maxFrames) const noexcept
    {
        return frames_.read(cursor, out, maxFrames);
    }
    std::uint64_t framesWritten() const noexcept { return frames_.framesWritten(); }
    // The newest `n` (<= scopeLength / 2) scope samples, oldest first.
    bool readScope(float* in, float* out, int n) const noexcept;
    double sampleRate() const noexcept { return sampleRate_.load(std::memory_order_relaxed); }

private:
    rt::TelemetryRing<valueCount, length> frames_;
    std::unique_ptr<std::atomic<float>[]> scopeIn_, scopeOut_;
    std::atomic<std::uint64_t> scopeWritten_{0};
    std::atomic<double> sampleRate_{48000.0};
};

// The telemetry of a live Patina (null for any other processor).
const Telemetry* telemetryOf(const engine::InsertProcessor* processor) noexcept;

std::unique_ptr<engine::InsertProcessor> create();

// The factory settings, by name: each a full set of parameter values. The first
// is the defaults.
struct Preset
{
    std::string name;
    std::vector<float> values;
};
const std::vector<Preset>& presets();

} // namespace northlight::plugins::patina
