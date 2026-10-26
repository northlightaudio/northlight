#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <string>
#include <vector>

namespace northlight::hosting
{

// Where a format keeps a plugin's preset files: Documents/<root>/<vendor>/<name>,
// files ending in .<extension> ("VST3 Presets" and "vstpreset", "CLAP Presets"
// and "clappreset").
struct PresetLocation
{
    std::string root;
    std::string vendor;
    std::string name;
    std::string extension;
};

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
    virtual PresetLocation presetLocation() const = 0;
    virtual bool loadPresetFile(const juce::File& file) = 0;
    virtual bool savePresetFile(const juce::File& file) const = 0;

    // The folder for this plugin (names made safe for the file system; "Unknown"
    // and "Plugin" stand in for empty ones) and its preset files, sorted by path.
    juce::File userPresetFolder() const;
    std::vector<juce::File> userPresetFiles() const;
    std::string presetFileExtension() const { return presetLocation().extension; }

    // What the Presets button shows: the preset file or program last loaded
    // here, else the plugin's current program when it lists programs; empty
    // when neither is known.
    std::string currentPresetName() const;
    void noteLoadedPreset(std::string name) { loadedPreset_ = std::move(name); }

protected:
    // Writes a preset's bytes, making the folder first; false for no data.
    static bool writePresetFile(const juce::File& file, const void* data, size_t size);

private:
    std::string loadedPreset_;
};

} // namespace northlight::hosting
