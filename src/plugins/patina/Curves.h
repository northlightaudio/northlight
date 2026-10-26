#pragma once

// Patina's curves: the mappings from a control to what a stage does, shared by
// the processor and the card (plugins/Patina.h includes this) so what the card
// shows is what runs. The parameter and choice enums are in plugins/Patina.h.
// JUCE-free.

namespace northlight::plugins::patina
{

// Crush's ends: the clean converter and the most it can be crushed.
inline constexpr float cleanRate = 48000.0f, lowestRate = 1000.0f;
inline constexpr float cleanBits = 16.0f, fewestBits = 2.0f;

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
// Pace's rate (Hz) for a pace (0..1): 0.05 to 1.5 Hz.
float paceHz(float pace) noexcept;
// Drift rate's scale of the machine's wow and flutter for a rate (0..1): 0.5x to 2x.
float driftRateScale(float rate) noexcept;
// Vinyl crackle's clicks a second for a texture (0..1): 2 to 300.
float crackleRate(float texture) noexcept;
// Wear's high cut (Hz) for a loss (0..1).
float wearCutoffHz(float loss) noexcept;
// Crush's bits and rate as an amount (0 = clean .. 1 = the most) and back.
float crushBitsFor(float amount) noexcept;
float crushRateFor(float amount) noexcept;
float crushAmountOfBits(float bits) noexcept;
float crushAmountOfRate(float hz) noexcept;

// Heat's curves themselves, for the Heat stage and heatTransfer(): u is the
// driven signal, the dead zone the fuzz's (fuzzDeadZone() of the amount).
namespace heatcurve
{
double fuzzDeadZone(double amount) noexcept;
double curve(int character, double u, double deadZone) noexcept;
// The antiderivative of curve(), for first-order antiderivative anti-aliasing.
double integral(int character, double u, double deadZone) noexcept;
// The output trim for a drive: between what holds small signals level and what
// holds a -12 dBFS peak level (weighted toward the latter), so more drive mostly
// adds harmonics and density rather than loudness.
double compensation(int character, double drive, double deadZone) noexcept;
} // namespace heatcurve

} // namespace northlight::plugins::patina
