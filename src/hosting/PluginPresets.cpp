#include "hosting/PluginPresets.h"

#include <algorithm>

namespace northlight::hosting
{

juce::File PluginPresets::userPresetFolder() const
{
    const auto location = presetLocation();
    const auto safe = [](const std::string& s)
    { return juce::File::createLegalFileName(juce::String::fromUTF8(s.c_str())).trim(); };
    const auto vendor = safe(location.vendor);
    const auto name = safe(location.name);
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile(juce::String::fromUTF8(location.root.c_str()))
        .getChildFile(vendor.isEmpty() ? "Unknown" : vendor)
        .getChildFile(name.isEmpty() ? "Plugin" : name);
}

std::vector<juce::File> PluginPresets::userPresetFiles() const
{
    std::vector<juce::File> files;
    const auto folder = userPresetFolder();
    if (!folder.isDirectory())
        return files;
    const auto pattern = "*." + juce::String::fromUTF8(presetLocation().extension.c_str());
    for (const auto& f : folder.findChildFiles(juce::File::findFiles, true, pattern))
        files.push_back(f);
    std::sort(files.begin(),
              files.end(),
              [](const juce::File& a, const juce::File& b)
              { return a.getFullPathName().compareIgnoreCase(b.getFullPathName()) < 0; });
    return files;
}

bool PluginPresets::writePresetFile(const juce::File& file, const void* data, size_t size)
{
    if (size == 0)
        return false;
    file.getParentDirectory().createDirectory();
    return file.replaceWithData(data, size);
}

} // namespace northlight::hosting
