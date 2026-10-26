#include "plugins/ChannelStrip.h"

#include "engine/rt/RealtimeGuard.h"
#include "plugins/BuiltIns.h"
#include "plugins/strip/Modules.h"

#include <algorithm>

// The channel strip's registry: its modules and slots, making a module, and
// the default strip. Each module's processor is in src/plugins/strip.

namespace northlight::plugins::strip
{

const std::vector<std::string>& moduleTypes()
{
    static const std::vector<std::string> list{
        types::gate, types::eq, types::compressor, types::deesser, types::saturation, types::limiter};
    return list;
}

bool isStripType(const std::string& type)
{
    return type.rfind("northlight.strip.", 0) == 0;
}

std::string displayName(const std::string& type)
{
    if (type == types::gate)
        return "Gate";
    if (type == types::eq)
        return "EQ";
    if (type == types::compressor)
        return "Compressor";
    if (type == types::deesser)
        return "De-esser";
    if (type == types::saturation)
        return "Saturation";
    if (type == types::limiter)
        return "Limiter";
    if (type == types::opto)
        return "Opto compressor";
    return type;
}

std::string slotOf(const std::string& type)
{
    return type == types::opto ? types::compressor : type;
}

std::vector<std::string> alternatives(const std::string& type)
{
    if (slotOf(type) == types::compressor)
        return {types::compressor, types::opto};
    return {type};
}

std::string modelName(const std::string& type)
{
    if (type == types::compressor)
        return "Clean";
    if (type == types::opto)
        return "Opto";
    return displayName(type);
}
std::unique_ptr<engine::InsertProcessor> createKeyedCompressor(const std::string& type)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("strip::createKeyedCompressor");
    return makeCompressor(type, true);
}

std::unique_ptr<engine::InsertProcessor> createOpto(const std::string& type)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("strip::createOpto");
    return makeOpto(type);
}

std::unique_ptr<engine::InsertProcessor> create(const std::string& type)
{
    NORTHLIGHT_ASSERT_NOT_REALTIME("strip::create");
    if (type == types::opto)
        return makeOpto(types::opto);
    if (type == types::gate)
        return makeGate();
    if (type == types::eq)
        return makeEq();
    if (type == types::compressor)
        return makeCompressor(types::compressor, false);
    if (type == types::deesser)
        return makeDeEsser();
    if (type == types::saturation)
        return makeSaturation();
    if (type == types::limiter)
        return makeLimiter();
    return nullptr;
}

std::vector<core::Insert> defaultStrip()
{
    std::vector<core::Insert> modules;
    for (const auto& type : moduleTypes())
    {
        core::Insert module;
        module.type = type;
        module.bypass = true; // off until switched on
        for (const auto& p : parameterInfo(type))
            module.params.push_back(p.defaultValue);
        modules.push_back(std::move(module));
    }
    return modules;
}

bool ensureChannelStrip(core::Project& project)
{
    const auto defaults = defaultStrip();
    bool changed = false;
    const auto ensure = [&](core::Channel& channel)
    {
        for (const auto& module : defaults)
        {
            // A slot counts as present whichever model runs in it.
            const bool present =
                std::any_of(channel.strip.begin(),
                            channel.strip.end(),
                            [&](const core::Insert& m) { return slotOf(m.type) == module.type; });
            if (present)
                continue;
            auto added = module;
            added.id = project.allocateId();
            channel.strip.push_back(std::move(added));
            changed = true;
        }
    };
    for (auto& t : project.tracks)
        if (t.hasAudio()) // a MIDI track makes no audio to process
            ensure(t);
    for (auto& b : project.buses)
        ensure(b);
    return changed;
}

} // namespace northlight::plugins::strip
