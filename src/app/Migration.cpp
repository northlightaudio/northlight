#include "Migration.h"

#include "core/ProjectJson.h"

namespace northlight::migration
{

juce::String migrateSettingsText(const juce::String& text)
{
    // Built-in processor names appear in settings values (favourites, the
    // default instrument); each starts with the old prefix right after a quote,
    // a colon (a "plugin:" drag name) or a separator in a list.
    const juce::String old(core::legacyTypePrefix());
    juce::String out;
    int from = 0;
    for (int at = text.indexOf(old); at >= 0; at = text.indexOf(at + 1, old))
    {
        const auto before = at > 0 ? text[at - 1] : juce::juce_wchar(' ');
        if (before != '"' && before != ':' && before != ';' && before != ',' && before != ' ')
            continue;
        out << text.substring(from, at) << core::currentTypePrefix();
        from = at + old.length();
    }
    out << text.substring(from);
    return out;
}

Result fromAural(const juce::File& appDataIn, const juce::File& documentsIn)
{
    Result result;
    const auto appData = appDataIn != juce::File()
                             ? appDataIn
                             : juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    const auto documents = documentsIn != juce::File()
                               ? documentsIn
                               : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    const auto oldSettings = appData.getChildFile("Aural");
    const auto newSettings = appData.getChildFile("Northlight");
    if (oldSettings.isDirectory() && !newSettings.exists() && oldSettings.copyDirectoryTo(newSettings))
    {
        const auto oldFile = newSettings.getChildFile("Aural.settings");
        const auto newFile = newSettings.getChildFile("Northlight.settings");
        if (oldFile.existsAsFile() && !newFile.exists())
        {
            newFile.replaceWithText(migrateSettingsText(oldFile.loadFileAsString()));
            oldFile.deleteFile(); // the copy's, not the original
        }
        result.settingsCopied = true;
    }

    const auto oldDocuments = documents.getChildFile("Aural");
    const auto newDocuments = documents.getChildFile("Northlight");
    for (const auto* folder : {"Templates", "Strip Presets"})
    {
        const auto from = oldDocuments.getChildFile(folder);
        const auto to = newDocuments.getChildFile(folder);
        if (from.isDirectory() && !to.exists() && newDocuments.createDirectory().wasOk() &&
            from.copyDirectoryTo(to))
            result.documentsCopied.add(folder);
    }
    return result;
}

} // namespace northlight::migration
