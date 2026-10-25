#pragma once

#include "core/ProjectJson.h"
#include "engine/Insert.h"
#include "plugins/EffectPresets.h"
#include "plugins/Sampler.h"

#include <memory>
#include <string>
#include <vector>

// The built-in processors: gain, a three-band EQ with high-pass, a compressor, an opto
// compressor, a limiter, a reverb and a delay here; the glue compressor (plugins/Glue.h),
// Patina, retro colour (plugins/Patina.h), the exciter (plugins/Exciter.h), the effect
// reverb (plugins/Bloom.h), the tape echo (plugins/TapeEcho.h), Orbit the tape machine
// (plugins/Orbit.h), the stereo widener
// (plugins/Widener.h), Corona the amp (plugins/Amp.h) and the guitar pedals (Gravity, Flare,
// Comet, Tide, Polaris: plugins/Pedals.h) in files of their own; and the instruments (Aurora,
// Pulsar, Nebula, Starbit, Sampler) in theirs. JUCE-free, real-time safe after prepare().

namespace northlight::plugins
{

namespace types
{
inline const std::string gain = "northlight.gain";
inline const std::string eq = "northlight.eq";
inline const std::string compressor = "northlight.compressor";
inline const std::string opto = "northlight.opto"; // the channel strip's opto compressor
inline const std::string limiter = "northlight.limiter";
inline const std::string reverb = "northlight.reverb";
inline const std::string delay = "northlight.delay";
} // namespace types

// The built-ins' registry: one entry per insert type, in menu order (the
// channel strip's modules keep their own list, strip::moduleTypes()). The
// factory, the menus and the factory presets all read it.
enum class BuiltInKind
{
    effect,
    instrument, // takes MIDI, makes audio
};
struct BuiltInType
{
    std::string type;
    std::string displayName; // for menus
    BuiltInKind kind = BuiltInKind::effect;
    std::unique_ptr<engine::InsertProcessor> (*create)(const sampler::Loader&) = nullptr;
    // An effect's factory presets, the first its defaults (null for an
    // instrument, which keeps its own list).
    std::vector<EffectPreset> (*presets)() = nullptr;
};
const std::vector<BuiltInType>& builtInTypes();
// The registry's entry for a type (null for a strip module or an unknown type).
const BuiltInType* findBuiltIn(const std::string& typeName);

class BuiltInFactory final : public engine::InsertFactory
{
public:
    // The loader decodes the Sampler's files (none: a sampler stays empty).
    explicit BuiltInFactory(sampler::Loader sampleLoader = {}) : sampleLoader_(std::move(sampleLoader)) {}
    void setSampleLoader(sampler::Loader loader) { sampleLoader_ = std::move(loader); }

    std::vector<std::string> typeNames() const override;
    std::unique_ptr<engine::InsertProcessor> create(const std::string& typeName) const override;

    // Human-readable name for a type, for menus.
    static std::string displayName(const std::string& typeName);
    // Whether a type is an instrument (takes MIDI, makes audio).
    static bool isInstrument(const std::string& typeName);

private:
    sampler::Loader sampleLoader_;
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
    sync,     // 0 off (the time in ms), else a note value at the project tempo (plugins/BeatSync.h)
    count
};
} // namespace delay

// Default parameter values for a type, for creating a model insert.
std::vector<float> defaultParameters(const std::string& typeName);
// A built-in's parameter keys and defaults (empty for a type that is not a
// built-in), for saving and reconciling projects. Cached; not real-time.
core::ParameterLayout builtInParameterLayout(const std::string& typeName);
// Keys for the built-ins alone (writing a project with no engine, e.g. the demo song).
class BuiltInParameterKeys final : public core::ParameterKeys
{
public:
    core::ParameterLayout layout(const core::Insert& insert) const override
    {
        return builtInParameterLayout(insert.type);
    }
};
// A built-in's (or strip module's) parameter descriptions, made once per type
// (empty for an unknown type). Not real-time safe.
const std::vector<engine::ParameterInfo>& parameterInfo(const std::string& typeName);

} // namespace northlight::plugins
