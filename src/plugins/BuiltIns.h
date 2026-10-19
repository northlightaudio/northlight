#pragma once

#include "engine/Insert.h"

#include <memory>
#include <string>
#include <vector>

// The built-in utility processors: gain, a three-band EQ with high-pass, a
// compressor and a limiter. Enough to make the mixer useful without third-party
// plugins. JUCE-free, real-time safe after prepare().

namespace aural::plugins
{

namespace types
{
inline const std::string gain = "aural.gain";
inline const std::string eq = "aural.eq";
inline const std::string compressor = "aural.compressor";
inline const std::string limiter = "aural.limiter";
} // namespace types

class BuiltInFactory final : public engine::InsertFactory
{
public:
    std::vector<std::string> typeNames() const override;
    std::unique_ptr<engine::InsertProcessor> create(const std::string& typeName) const override;

    // Human-readable name for a type, for menus.
    static std::string displayName(const std::string& typeName);
};

// Default parameter values for a type, for creating a model insert.
std::vector<float> defaultParameters(const std::string& typeName);

} // namespace aural::plugins
