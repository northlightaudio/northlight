#include "plugins/BuiltIns.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/Amp.h"
#include "plugins/Aurora.h"
#include "plugins/Bloom.h"
#include "plugins/ChannelStrip.h"
#include "plugins/Classic.h"
#include "plugins/EffectPresetLists.h"
#include "plugins/Exciter.h"
#include "plugins/Glue.h"
#include "plugins/Nebula.h"
#include "plugins/Orbit.h"
#include "plugins/Patina.h"
#include "plugins/Pedals.h"
#include "plugins/Pulsar.h"
#include "plugins/Sampler.h"
#include "plugins/Starbit.h"
#include "plugins/TapeEcho.h"
#include "plugins/Vesper.h"
#include "plugins/Widener.h"

#include <algorithm>
#include <map>
#include <mutex>
#include <vector>

// The built-ins' registry and factory. The first built-ins (gain, EQ, limiter,
// reverb, delay) are in plugins/Classic; every other one is in a file of its own.

namespace northlight::plugins
{

namespace
{
template <std::unique_ptr<engine::InsertProcessor> (*Create)()>
std::unique_ptr<engine::InsertProcessor> plain(const sampler::Loader&)
{
    return Create();
}

std::unique_ptr<engine::InsertProcessor> keyedCompressor(const sampler::Loader&)
{
    return strip::createKeyedCompressor(types::compressor); // the channel strip's, with a key
}

std::unique_ptr<engine::InsertProcessor> optoCompressor(const sampler::Loader&)
{
    return strip::createOpto(types::opto);
}

std::unique_ptr<engine::InsertProcessor> sampled(const sampler::Loader& loader)
{
    return sampler::create(loader);
}

std::unique_ptr<engine::InsertProcessor> corona(const sampler::Loader& loader)
{
    return amp::create(loader);
}

// Pulsar's pads by note ("Kick" on 36).
const char* pulsarNoteName(int note)
{
    const int pad = pulsar::padForNote(note);
    return pad >= 0 ? pulsar::padName(pad) : nullptr;
}
} // namespace

const std::vector<BuiltInType>& builtInTypes()
{
    using enum BuiltInKind;
    namespace pl = presetlists;
    static const std::vector<BuiltInType> list{
        {types::gain, "Gain", effect, plain<classic::createGain>, pl::gainPresets, "A plain level trim"},
        {types::eq, "EQ", effect, plain<classic::createEq>, pl::eqPresets, "Plain three-band EQ, high-pass"},
        {types::compressor,
         "Compressor",
         effect,
         keyedCompressor,
         pl::compressorPresets,
         "Clean compressor with a key input"},
        {types::opto,
         "Opto Compressor",
         effect,
         optoCompressor,
         pl::optoPresets,
         "Smooth optical, LA-2A style"},
        {glue::type,
         "Glue Compressor",
         effect,
         plain<glue::create>,
         pl::gluePresets,
         "Bus compressor that glues"},
        {widener::type,
         "Stereo Widener",
         effect,
         plain<widener::create>,
         pl::widenerPresets,
         "Widens or narrows the stereo image"},
        {patina::type,
         "Patina",
         effect,
         plain<patina::create>,
         pl::patinaPresets,
         "Retro colour: age and wear"},
        {exciter::type,
         "Exciter",
         effect,
         plain<exciter::create>,
         pl::exciterPresets,
         "Adds harmonics for air"},
        {orbit::type,
         "Orbit",
         effect,
         plain<orbit::create>,
         pl::orbitPresets,
         "Tape machine: drive and flutter"},
        {types::limiter,
         "Limiter",
         effect,
         plain<classic::createLimiter>,
         pl::limiterPresets,
         "Plain lookahead peak limiter"},
        {types::reverb,
         "Reverb",
         effect,
         plain<classic::createReverb>,
         pl::reverbPresets,
         "Plain algorithmic room"},
        {types::delay, "Delay", effect, plain<classic::createDelay>, pl::delayPresets, "Plain stereo delay"},
        {tapeecho::type,
         "Tape Echo",
         effect,
         plain<tapeecho::create>,
         pl::tapeEchoPresets,
         "Tape echo with wow and wear"},
        {bloom::type, "Bloom", effect, plain<bloom::create>, pl::bloomPresets, "Swelling, shimmering reverb"},
        {vesper::type,
         "Vesper",
         effect,
         plain<vesper::create>,
         pl::vesperPresets,
         "Vocal reverb that keeps clear"},
        {amp::type, "Corona", effect, corona, pl::ampPresets, "Guitar amp and cabinet"},
        {gravity::type, "Gravity", effect, plain<gravity::create>, pl::gravityPresets, "Compressor pedal"},
        {flare::type, "Flare", effect, plain<flare::create>, pl::flarePresets, "Distortion pedal"},
        {comet::type, "Comet", effect, plain<comet::create>, pl::cometPresets, "Delay pedal"},
        {tide::type, "Tide", effect, plain<tide::create>, pl::tidePresets, "Chorus pedal"},
        {polaris::type, "Polaris", effect, plain<polaris::create>, pl::polarisPresets, "Tuner pedal"},
        {aurora::type, "Aurora", instrument, plain<aurora::create>, pl::auroraPresets, "Polyphonic synth"},
        {pulsar::type,
         "Pulsar",
         instrument,
         plain<pulsar::create>,
         nullptr,
         "Eight-pad drum synth",
         pulsarNoteName},
        {nebula::type, "Nebula", instrument, plain<nebula::create>, pl::nebulaPresets, "Airy pad synth"},
        {starbit::type, "Starbit", instrument, plain<starbit::create>, pl::starbitPresets, "Chiptune synth"},
        {sampler::type, "Sampler", instrument, sampled, nullptr, "One sample across the keys"},
    };
    return list;
}

const BuiltInType* findBuiltIn(const std::string& typeName)
{
    const auto& list = builtInTypes();
    const auto it =
        std::find_if(list.begin(), list.end(), [&](const BuiltInType& b) { return b.type == typeName; });
    return it != list.end() ? &*it : nullptr;
}

std::string builtInNoteName(const std::string& typeName, int note)
{
    const auto* builtIn = findBuiltIn(typeName);
    if (builtIn == nullptr || builtIn->noteName == nullptr)
        return {};
    const char* name = builtIn->noteName(note);
    return name != nullptr ? std::string(name) : std::string{};
}

std::vector<std::string> BuiltInFactory::typeNames() const
{
    std::vector<std::string> names;
    for (const auto& builtIn : builtInTypes())
        names.push_back(builtIn.type);
    return names;
}

bool BuiltInFactory::isInstrument(const std::string& typeName)
{
    const auto* builtIn = findBuiltIn(typeName);
    return builtIn != nullptr && builtIn->kind == BuiltInKind::instrument;
}

std::unique_ptr<engine::InsertProcessor> BuiltInFactory::create(const std::string& typeName) const
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("BuiltInFactory::create");
    if (strip::isStripType(typeName))
        return strip::create(typeName);
    const auto* builtIn = findBuiltIn(typeName);
    return builtIn != nullptr ? builtIn->create(sampleLoader_) : nullptr;
}

std::string BuiltInFactory::displayName(const std::string& typeName)
{
    if (strip::isStripType(typeName))
        return strip::displayName(typeName);
    const auto* builtIn = findBuiltIn(typeName);
    return builtIn != nullptr ? builtIn->displayName : typeName;
}

std::string BuiltInFactory::description(const std::string& typeName)
{
    const auto* builtIn = findBuiltIn(typeName);
    return builtIn != nullptr ? std::string(builtIn->description) : std::string{};
}

const std::vector<engine::ParameterInfo>& parameterInfo(const std::string& typeName)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("plugins::parameterInfo");
    // Read from the message thread and the autosave worker. A map's entries
    // never move, so a reference handed out stays valid after the lock.
    static std::mutex mutex;
    static std::map<std::string, std::vector<engine::ParameterInfo>> cache;
    const std::scoped_lock lock(mutex);
    auto it = cache.find(typeName);
    if (it == cache.end())
    {
        std::vector<engine::ParameterInfo> infos;
        if (const auto p = BuiltInFactory().create(typeName))
            infos = p->parameters();
        it = cache.emplace(typeName, std::move(infos)).first;
    }
    return it->second;
}

std::vector<float> defaultParameters(const std::string& typeName)
{
    std::vector<float> values;
    for (const auto& p : parameterInfo(typeName))
        values.push_back(p.defaultValue);
    return values;
}

core::ParameterLayout builtInParameterLayout(const std::string& typeName)
{
    static std::mutex mutex;
    static std::map<std::string, core::ParameterLayout> cache;
    const std::scoped_lock lock(mutex);
    if (const auto it = cache.find(typeName); it != cache.end())
        return it->second;
    core::ParameterLayout layout;
    if (const auto processor = BuiltInFactory().create(typeName))
        for (int i = 0; i < static_cast<int>(processor->parameters().size()); ++i)
        {
            layout.keys.push_back(processor->parameterKey(i));
            layout.defaults.push_back(processor->parameters()[static_cast<size_t>(i)].defaultValue);
        }
    cache[typeName] = layout;
    return layout;
}

} // namespace northlight::plugins
