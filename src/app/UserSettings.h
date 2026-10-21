#pragma once

#include "audio/AudioSettings.h"

#include <juce_data_structures/juce_data_structures.h>

namespace northlight
{

// The app's per-user settings file (%APPDATA%\Northlight\Northlight.settings, XML). Each
// subsystem that persists something implements its store interface on top of this
// one file by writing namespaced keys ("audio.*").
class UserSettings final : public audio::AudioSettingsStore
{
public:
    UserSettings();
    ~UserSettings() override;

    audio::AudioSettings loadAudioSettings() override;
    void saveAudioSettings(const audio::AudioSettings& settings) override;

    // Generic per-user values (window positions, UI preferences).
    std::string value(const std::string& key, const std::string& fallback = {}) const;
    void setValue(const std::string& key, const std::string& value);

    // Writes any pending changes to disk now.
    void flush();

    juce::File file() const;

private:
    juce::ApplicationProperties properties_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UserSettings)
};

} // namespace northlight
