#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <map>
#include <string>
#include <vector>

// A built-in effect's presets (screens/plugins.md 7.2b): the factory list
// (plugins/EffectPresets.h), then the user's own, one JSON file each in
// Documents/Northlight/Effect Presets/<effect name>. A saved preset keeps its
// values by parameter name, so it still loads after parameters are added or
// reordered (a parameter it lacks takes its default). Message thread only;
// every preset bar listens, so all bars for an effect stay in step.

namespace northlight::ui::strip
{

struct PresetEntry
{
    juce::String name;
    std::vector<float> values;
    bool user = false;
    juce::File file; // a user preset's file
};

class EffectPresetLibrary final : public juce::ChangeBroadcaster
{
public:
    static EffectPresetLibrary& instance();

    // Where the user's presets live (tests point it elsewhere).
    void setRoot(const juce::File& root);
    juce::File folderFor(const std::string& type) const;

    // Factory presets first, then the user's in name order. Cached; the user's
    // are read again by reload() (the bars call it before showing their menu).
    const std::vector<PresetEntry>& presets(const std::string& type);
    void reload(const std::string& type);

    // The preset whose values match `params` (-1: none, "Custom").
    int match(const std::string& type, const std::vector<float>& params);

    // Writes (or replaces) a user preset; returns an error, or empty.
    std::string save(const std::string& type, const juce::String& name, const std::vector<float>& params);
    // Deletes a user preset's file (to the recycle bin where there is one).
    bool remove(const std::string& type, const PresetEntry& entry);

private:
    EffectPresetLibrary();
    std::vector<PresetEntry> load(const std::string& type) const;

    juce::File root_;
    std::map<std::string, std::vector<PresetEntry>> cache_;
};

} // namespace northlight::ui::strip
