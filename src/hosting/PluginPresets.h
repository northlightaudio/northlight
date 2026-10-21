#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace northlight::hosting
{

// What the plugin window's Presets menu needs from a hosted plugin, whatever its
// format: the plugin's own program list, and preset files in a per-plugin folder
// under the user's documents. Message thread only.
class PluginPresets
{
public:
    virtual ~PluginPresets() = default;
    virtual std::string displayName() const = 0;
    virtual std::vector<std::string> factoryPrograms() const = 0;
    virtual int currentProgram() const = 0;
    virtual void setProgram(int index) = 0;
    virtual bool supportsPresetFiles() const = 0;
    virtual juce::File userPresetFolder() const = 0;
    virtual std::vector<juce::File> userPresetFiles() const = 0;
    virtual bool loadPresetFile(const juce::File& file) = 0;
    virtual bool savePresetFile(const juce::File& file) const = 0;
    // The file extension the format uses for presets ("vstpreset", "clappreset").
    virtual std::string presetFileExtension() const = 0;
};

} // namespace northlight::hosting
