#include "plugins/FxTracks.h"

#include "plugins/BuiltIns.h"

namespace northlight::plugins
{

std::vector<FxChoice> fxTrackEffects()
{
    return {{types::reverb, "Reverb"}, {types::delay, "Delay"}, {{}, "Empty"}};
}

core::Bus
makeFxTrack(const core::Project& project, const std::string& effectType, const std::string& effectName)
{
    core::Bus bus;
    bus.id = project.nextId;
    bus.fx = true;
    // "Reverb", then "Reverb 2" and so on.
    const std::string base = effectName.empty() || effectType.empty() ? std::string("FX") : effectName;
    std::string name = base;
    for (int n = 2;; ++n)
    {
        const bool taken = std::any_of(
            project.buses.begin(), project.buses.end(), [&](const core::Bus& b) { return b.name == name; });
        if (!taken)
            break;
        name = base + " " + std::to_string(n);
    }
    bus.name = name;
    if (!effectType.empty())
    {
        core::Insert effect;
        effect.type = effectType;
        effect.params = defaultParameters(effectType);
        // Fully wet: the sends carry only what should be processed.
        if (effectType == types::reverb && effect.params.size() > reverb::mix)
            effect.params[reverb::mix] = 100.0f;
        if (effectType == types::delay && effect.params.size() > delay::mix)
            effect.params[delay::mix] = 100.0f;
        bus.inserts.push_back(effect);
    }
    return bus;
}

std::vector<std::unique_ptr<core::Command>> addFxTrackCommands(const core::Project& project,
                                                               const std::string& effectType,
                                                               const std::string& effectName,
                                                               const std::vector<core::Id>& sendFrom)
{
    std::vector<std::unique_ptr<core::Command>> commands;
    const auto bus = makeFxTrack(project, effectType, effectName);
    commands.push_back(std::make_unique<core::AddBus>(bus));
    for (const auto channel : sendFrom)
        if (project.findChannel(channel) != nullptr)
            commands.push_back(std::make_unique<core::SetSend>(channel, bus.id, 0.5f, false));
    return commands;
}

} // namespace northlight::plugins
