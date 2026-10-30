#pragma once

#include "engine/Insert.h"
#include "plugins/EffectPresets.h"

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

// What the built-ins share to describe their parameters and build their preset
// lists. JUCE-free; not real-time (they run when a type is first described).

namespace northlight::plugins
{

// A stepped parameter: one value per choice, from 0.
inline engine::ParameterInfo
choice(std::string name, std::vector<std::string> choices, float defaultValue = 0.0f)
{
    engine::ParameterInfo info;
    info.name = std::move(name);
    info.minimum = 0.0f;
    info.maximum = static_cast<float>(choices.size() - 1);
    info.defaultValue = defaultValue;
    info.choices = std::move(choices);
    return info;
}

// A switch: Off (0) or On (1).
inline engine::ParameterInfo onOff(std::string name, bool on)
{
    return choice(std::move(name), {"Off", "On"}, on ? 1.0f : 0.0f);
}

// A percentage, 0..100 unless a range is given.
inline engine::ParameterInfo
percent(std::string name, float defaultValue, float minimum = 0.0f, float maximum = 100.0f)
{
    return {std::move(name), "%", minimum, maximum, defaultValue, false};
}

// A continuous value in a range, with its unit ("ms", "st", "" for none).
inline engine::ParameterInfo
range(std::string name, std::string unit, float minimum, float maximum, float defaultValue)
{
    engine::ParameterInfo info;
    info.name = std::move(name);
    info.unit = std::move(unit);
    info.minimum = minimum;
    info.maximum = maximum;
    info.defaultValue = defaultValue;
    return info;
}

// A whole-number setting (a count, semitones, a velocity).
inline engine::ParameterInfo
whole(std::string name, std::string unit, float minimum, float maximum, float defaultValue)
{
    auto info = range(std::move(name), std::move(unit), minimum, maximum, defaultValue);
    info.integer = true;
    return info;
}

// Builds a preset list by parameter index: each preset starts from the
// defaults and sets only what it names. Values are doubles so a choice's
// enumerator goes in as it is. An index past the end or a value outside its
// parameter's range (which is clamped into it) is an error, reported through
// factoryPresetErrors() once done() hands the list over, so a test catches a
// preset that drifted from its table.
class PresetBuilder
{
public:
    using Setting = std::pair<int, double>;

    PresetBuilder(std::string type, std::vector<engine::ParameterInfo> infos);

    PresetBuilder& add(const std::string& name, std::initializer_list<Setting> settings)
    {
        return add(name, std::vector<Setting>(settings));
    }
    PresetBuilder& add(const std::string& name, const std::vector<Setting>& settings);

    // The problems found so far (done() reports them).
    const std::vector<std::string>& errors() const noexcept { return errors_; }
    std::vector<EffectPreset> done();

private:
    std::string type_;
    std::vector<engine::ParameterInfo> infos_;
    std::vector<float> base_;
    std::vector<EffectPreset> list_;
    std::vector<std::string> errors_;
};

// Adds a problem found while building a preset list (thread-safe).
void reportPresetError(std::string error);

} // namespace northlight::plugins
