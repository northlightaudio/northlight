#include "UserSettings.h"

#include "engine/rt/RealtimeGuard.h"

namespace aural
{

namespace
{
constexpr const char* audioKeyPrefix = "audio.";
}

UserSettings::UserSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Aural";
    options.filenameSuffix = "settings";
    options.folderName = "Aural";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = 500;
    properties_.setStorageParameters(options);
}

UserSettings::~UserSettings()
{
    flush();
}

juce::File UserSettings::file() const
{
    return properties_.getStorageParameters().getDefaultFile();
}

void UserSettings::flush()
{
    properties_.saveIfNeeded();
}

std::string UserSettings::value(const std::string& key, const std::string& fallback) const
{
    return const_cast<juce::ApplicationProperties&>(properties_)
        .getUserSettings()
        ->getValue(juce::String(key), juce::String(fallback))
        .toStdString();
}

void UserSettings::setValue(const std::string& key, const std::string& value)
{
    AURAL_ASSERT_NOT_REALTIME("UserSettings::setValue");
    properties_.getUserSettings()->setValue(juce::String(key), juce::String(value));
}

audio::AudioSettings UserSettings::loadAudioSettings()
{
    AURAL_ASSERT_NOT_REALTIME("UserSettings::loadAudioSettings");
    audio::AudioSettings::KeyValues values;
    const auto& all = properties_.getUserSettings()->getAllProperties();
    for (const auto& key : all.getAllKeys())
        if (key.startsWith(audioKeyPrefix))
            values[key.toStdString()] = all[key].toStdString();
    return audio::AudioSettings::fromKeyValues(values);
}

void UserSettings::saveAudioSettings(const audio::AudioSettings& settings)
{
    AURAL_ASSERT_NOT_REALTIME("UserSettings::saveAudioSettings");
    auto* user = properties_.getUserSettings();
    const auto values = settings.toKeyValues();

    // Drop keys the new settings no longer carry (optional fields), then write.
    for (const auto& key : user->getAllProperties().getAllKeys())
        if (key.startsWith(audioKeyPrefix) && !values.contains(key.toStdString()))
            user->removeValue(key);

    for (const auto& [key, value] : values)
        user->setValue(juce::String(key), juce::String(value));
}

} // namespace aural
