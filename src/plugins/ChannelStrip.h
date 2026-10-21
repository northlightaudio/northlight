#pragma once

#include "core/Model.h"
#include "engine/Insert.h"
#include "plugins/Biquad.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

// The channel strip (Cubase-style): a fixed set of built-in modules every track
// and bus has, switched on in place instead of being added to the insert rack.
// In the model each module is a core::Insert in Channel::strip (bypass = off),
// so the insert commands, undo and the engine's instance cache apply unchanged.
// Every module publishes engine::ProcessorFeedback for its editor: input and
// output peaks, gain reduction and, for the EQ, taps for the spectrum analyser.
//
// The parameter layouts below are part of the project format: append, never
// reorder. The curve functions are shared by the processors and the editors so
// what the editor draws is what runs.

namespace northlight::plugins::strip
{

namespace types
{
inline const std::string gate = "northlight.strip.gate";
inline const std::string eq = "northlight.strip.eq";
inline const std::string compressor = "northlight.strip.compressor";
inline const std::string deesser = "northlight.strip.deesser";
inline const std::string saturation = "northlight.strip.saturation";
inline const std::string limiter = "northlight.strip.limiter";
} // namespace types

// The modules, in their default processing order.
const std::vector<std::string>& moduleTypes();
bool isStripType(const std::string& type);
std::string displayName(const std::string& type);
std::unique_ptr<engine::InsertProcessor> create(const std::string& type);
// The channel strip's compressor as an insert of another type (the built-in
// "northlight.compressor"), listening to a sidechain key when one is chosen.
std::unique_ptr<engine::InsertProcessor> createKeyedCompressor(const std::string& type);

// Every module, off, with its default parameters and no ids (AddTrack and
// AddBus allocate them).
std::vector<core::Insert> defaultStrip();
// Gives every track and bus the modules it lacks (a project saved before the
// strip existed, or a module added since), with fresh ids, in default order
// after the ones it has. Returns whether anything changed.
bool ensureChannelStrip(core::Project& project);

namespace gate
{
enum Param
{
    threshold,   // dB
    range,       // dB of attenuation when closed
    attack,      // ms
    hold,        // ms
    release,     // ms
    keyHighPass, // Hz; the minimum is off
    keyLowPass,  // Hz; the maximum is off
    count
};
constexpr float hysteresisDb = 3.0f; // closes this far below the threshold
} // namespace gate

namespace eq
{
// Six bands: a low cut, four parametric bands and a high cut.
enum Band
{
    lowCut,
    band1,
    band2,
    band3,
    band4,
    highCut,
    bandCount
};
enum Field
{
    on,
    type, // the band's shape, or a cut's slope
    frequency,
    gain,
    q,
};
enum Shape
{
    peak,
    lowShelf,
    highShelf,
    notch,
};
// Cut slopes in dB/octave, by the slope parameter's value.
constexpr std::array<int, 6> slopes{6, 12, 18, 24, 36, 48};
constexpr int output = 26; // output gain, dB
constexpr int count = 27;
// The parameter index of a band's field, or -1 (cuts have no gain or Q).
int paramIndex(int band, Field field) noexcept;
bool isCut(int band) noexcept;

// The filter sections for a parameter set (all bands, or only `band`) in a
// fixed layout of maxSections slots (four per cut, one per band; identity
// where a band is off); returns how many are not identity. The processor runs
// these per channel.
constexpr int maxSections = 12;
int design(const float* params, double sampleRate, Biquad* out, int band = -1) noexcept;
// The response in dB at `hz` (all bands with the output gain, or one band alone).
double responseDb(const float* params, double sampleRate, double hz, int band = -1) noexcept;
} // namespace eq

namespace compressor
{
enum Param
{
    threshold, // dB
    ratio,     // :1
    attack,    // ms
    release,   // ms
    knee,      // dB
    makeup,    // dB
    autoMakeup,
    detector, // 0 peak, 1 RMS
    mix,      // % wet (parallel compression)
    count
};
// Gain reduction (dB, >= 0) the static curve applies to a detector level.
float gainReductionDb(float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept;
// The make-up gain auto make-up adds for a threshold and ratio.
float autoMakeupDb(float thresholdDb, float ratio) noexcept;
} // namespace compressor

namespace deesser
{
enum Param
{
    frequency, // Hz, where the sibilance band starts
    threshold, // dB
    range,     // the most reduction, dB
    mode,      // 0 split (only the band is reduced), 1 wide (everything is)
    listen,    // hear the detection band alone
    count
};
// Reduction (dB, >= 0) for a band level.
float reductionDb(float bandDb, float thresholdDb, float rangeDb) noexcept;
} // namespace deesser

namespace saturation
{
enum Param
{
    drive, // dB into the shaper
    type,  // Shape
    tone,  // -1 dark .. +1 bright (a high shelf after the shaper)
    output,
    mix, // % wet
    count
};
enum Shape
{
    tape, // symmetric tanh: odd harmonics, gentle
    tube, // biased tanh: even harmonics
    clip, // cubic soft clip that hardens at full scale
};
// The static curve (x is the driven signal).
float shape(int type, float x) noexcept;
// The output trim applied with the drive (so more drive is not only louder).
float driveCompensationDb(float driveDb) noexcept;
} // namespace saturation

namespace limiter
{
enum Param
{
    input,   // dB of gain into the limiter
    ceiling, // dB
    release, // ms
    count
};
} // namespace limiter

} // namespace northlight::plugins::strip
