#include "plugins/patina/Curves.h"

#include "plugins/Patina.h"
#include "plugins/dsp/Shapers.h"

#include <algorithm>
#include <cmath>

namespace northlight::plugins::patina
{

// ----- the shared mappings ------------------------------------------------------------------------

int onParam(int stage) noexcept
{
    static constexpr int on[] = {heatOn, chamberOn, driftOn, grainOn, wearOn, crushOn};
    return on[std::clamp(stage, 0, stageCount - 1)];
}

int breathParam(int stage) noexcept
{
    static constexpr int breath[] = {
        heatBreath, chamberBreath, driftBreath, grainBreath, wearBreath, crushBreath};
    return breath[std::clamp(stage, 0, stageCount - 1)];
}

float aged(float amount, float ageFraction) noexcept
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    ageFraction = std::clamp(ageFraction, 0.0f, 1.0f);
    if (ageFraction <= 0.5f)
        return amount * ageFraction * 2.0f;
    return amount + (1.0f - amount) * amount * (ageFraction - 0.5f) * 2.0f;
}

float heatDriveDb(float amount) noexcept
{
    return -12.0f + 39.0f * std::clamp(amount, 0.0f, 1.0f);
}

float chamberDecaySeconds(int space, float decay) noexcept
{
    decay = std::clamp(decay, 0.0f, 1.0f);
    switch (space)
    {
        case spring:
            return 0.8f + 3.2f * decay;
        case plate:
            return 0.9f + 3.6f * decay;
        default:
            return 0.3f + 1.2f * decay;
    }
}

float paceHz(float pace) noexcept
{
    return 0.05f * std::pow(30.0f, pace);
}

float driftRateScale(float rate) noexcept
{
    return std::pow(2.0f, 2.0f * rate - 1.0f);
}

float crackleRate(float texture) noexcept
{
    return 2.0f * std::pow(150.0f, texture);
}

float wearCutoffHz(float loss) noexcept
{
    return 20000.0f * std::pow(0.12f, std::clamp(loss, 0.0f, 1.0f));
}

float crushBitsFor(float amount) noexcept
{
    return cleanBits - (cleanBits - fewestBits) * std::clamp(amount, 0.0f, 1.0f);
}

float crushRateFor(float amount) noexcept
{
    return cleanRate * std::pow(lowestRate / cleanRate, std::clamp(amount, 0.0f, 1.0f));
}

float crushAmountOfBits(float bits) noexcept
{
    return std::clamp((cleanBits - bits) / (cleanBits - fewestBits), 0.0f, 1.0f);
}

float crushAmountOfRate(float hz) noexcept
{
    return std::clamp(
        std::log(cleanRate / std::max(1.0f, hz)) / std::log(cleanRate / lowestRate), 0.0f, 1.0f);
}

// ----- Heat's curves ------------------------------------------------------------------------------

namespace
{
constexpr double tubeBias = 0.3;
constexpr double fuzzPositiveClip = 0.7, fuzzNegativeClip = 1.0;
constexpr double heatReference = 0.25; // the level (-12 dBFS) the compensation holds

// The fuzz curve's half for a magnitude s >= 0: the dead zone t, then unity
// slope to the clip c.
double fuzzHalf(double s, double t, double c) noexcept
{
    return s < t ? 0.0 : std::min(s - t, c);
}
double fuzzHalfIntegral(double s, double t, double c) noexcept
{
    if (s < t)
        return 0.0;
    if (s < t + c)
        return 0.5 * (s - t) * (s - t);
    return 0.5 * c * c + c * (s - t - c);
}
} // namespace

namespace heatcurve
{
double fuzzDeadZone(double amount) noexcept
{
    return 0.02 + 0.1 * amount;
}

double curve(int character, double u, double deadZone) noexcept
{
    switch (character)
    {
        case tube:
            return dsp::tube(u, tubeBias);
        case iron:
            // A cubic with unity slope at zero that flattens at 1.5.
            return std::abs(u) <= 1.5 ? u - (4.0 / 27.0) * u * u * u : (u > 0.0 ? 1.0 : -1.0);
        case fuzz:
            return u >= 0.0 ? fuzzHalf(u, deadZone, fuzzPositiveClip)
                            : -fuzzHalf(-u, deadZone, fuzzNegativeClip);
        default:
            return std::tanh(u);
    }
}

double integral(int character, double u, double deadZone) noexcept
{
    switch (character)
    {
        case tube:
            return dsp::tubeIntegral(u, tubeBias);
        case iron:
            return std::abs(u) <= 1.5 ? 0.5 * u * u - u * u * u * u / 27.0 : std::abs(u) - 0.5625;
        case fuzz:
            return u >= 0.0 ? fuzzHalfIntegral(u, deadZone, fuzzPositiveClip)
                            : fuzzHalfIntegral(-u, deadZone, fuzzNegativeClip);
        default:
            return dsp::logCosh(u);
    }
}

double compensation(int character, double drive, double deadZone) noexcept
{
    const double atReference = std::max(1.0e-3, std::abs(curve(character, drive * heatReference, deadZone)));
    return std::pow(1.0 / drive, 0.35) * std::pow(heatReference / atReference, 0.65);
}
} // namespace heatcurve

float heatTransfer(int character, float amount, float x) noexcept
{
    const double drive = std::pow(10.0, heatDriveDb(amount) / 20.0);
    const double deadZone = heatcurve::fuzzDeadZone(amount);
    return static_cast<float>(heatcurve::compensation(character, drive, deadZone) *
                              heatcurve::curve(character, drive * static_cast<double>(x), deadZone));
}

} // namespace northlight::plugins::patina
