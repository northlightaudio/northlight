#pragma once

#include "engine/Insert.h"

#include <memory>
#include <string>
#include <vector>

// The built-in utility processors: gain, a three-band EQ with high-pass, a
// compressor, a limiter, a reverb and a delay; the instruments (Aurora, Pulsar,
// Nebula, Starbit) are in their own files. JUCE-free, real-time safe after prepare().

namespace aural::plugins
{

namespace types
{
inline const std::string gain = "aural.gain";
inline const std::string eq = "aural.eq";
inline const std::string compressor = "aural.compressor";
inline const std::string limiter = "aural.limiter";
inline const std::string reverb = "aural.reverb";
inline const std::string delay = "aural.delay";
} // namespace types

class BuiltInFactory final : public engine::InsertFactory
{
public:
    std::vector<std::string> typeNames() const override;
    std::unique_ptr<engine::InsertProcessor> create(const std::string& typeName) const override;

    // Human-readable name for a type, for menus.
    static std::string displayName(const std::string& typeName);
    // Whether a type is an instrument (takes MIDI, makes audio).
    static bool isInstrument(const std::string& typeName);
};

namespace reverb
{
enum Param
{
    preDelay, // ms
    decay,    // seconds (RT60)
    size,     // % (the room's dimensions: the delay lines' lengths)
    damping,  // Hz: the highs fade faster above this
    lowCut,   // Hz: the input's lows stay out of the tail
    width,    // %
    mix,      // % wet
    count
};
} // namespace reverb

namespace delay
{
enum Param
{
    time,     // ms
    feedback, // %
    tone,     // Hz: the repeats darken above this
    pingPong, // 0 off, 1 on: the repeats alternate sides
    mix,      // % wet
    count
};
} // namespace delay

// Default parameter values for a type, for creating a model insert.
std::vector<float> defaultParameters(const std::string& typeName);
// A built-in's (or strip module's) parameter descriptions, made once per type
// (empty for an unknown type). Not real-time safe.
const std::vector<engine::ParameterInfo>& parameterInfo(const std::string& typeName);

} // namespace aural::plugins
