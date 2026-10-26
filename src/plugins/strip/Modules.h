#pragma once

#include "engine/Insert.h"
#include "plugins/ChannelStrip.h"
#include "plugins/dsp/Shapers.h"
#include "plugins/dsp/Smoothing.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

// What the channel strip's modules share inside src/plugins/strip: each
// module's processor is in a file of its own (Gate, Eq, Compressor, Opto,
// DeEsser, Saturation, Limiter), made through the functions below; the curves
// the editors draw are in Curves.cpp, and the registry (strip::create and the
// rest of plugins/ChannelStrip.h) is in plugins/ChannelStrip.cpp. Every module
// is a FeedbackBuiltIn (plugins/MeteredBuiltIn.h): bypassed, a watched module
// measures its input, so the threshold can be set against the signal before it
// is switched on. JUCE-free.

namespace northlight::plugins::strip
{

std::unique_ptr<engine::InsertProcessor> makeGate();
std::unique_ptr<engine::InsertProcessor> makeEq();
std::unique_ptr<engine::InsertProcessor> makeCompressor(const std::string& type, bool keyed);
std::unique_ptr<engine::InsertProcessor> makeOpto(const std::string& type);
std::unique_ptr<engine::InsertProcessor> makeDeEsser();
std::unique_ptr<engine::InsertProcessor> makeSaturation();
std::unique_ptr<engine::InsertProcessor> makeLimiter();

// A one-pole coefficient for a time in milliseconds (at least 1 us).
inline float coefficient(double sampleRate, float ms) noexcept
{
    return dsp::poleForMs(std::max(0.001f, ms), static_cast<float>(sampleRate));
}

namespace saturation
{
// The saturation's curves in double precision, for its processor and the opto
// compressor's valve stage (saturation::shape() is the float face of curve()).
inline constexpr double tubeBias = 0.25;
inline const double tanhTubeBias = std::tanh(tubeBias); // the curve's offset, computed once

inline double curve(int type, double x) noexcept
{
    switch (type)
    {
        case tube:
            return dsp::tube(x, tubeBias, tanhTubeBias);
        case clip:
            return std::abs(x) <= 1.0 ? 1.5 * (x - x * x * x / 3.0) : (x > 0.0 ? 1.0 : -1.0);
        default:
            return std::tanh(x);
    }
}

// The antiderivative of curve(), for first-order antiderivative anti-aliasing.
inline double antiderivative(int type, double x) noexcept
{
    switch (type)
    {
        case tube:
            return dsp::tubeIntegral(x, tubeBias, tanhTubeBias);
        case clip:
            return std::abs(x) <= 1.0 ? 1.5 * (x * x / 2.0 - x * x * x * x / 12.0) : std::abs(x) - 0.375;
        default:
            return dsp::logCosh(x);
    }
}
} // namespace saturation

} // namespace northlight::plugins::strip
