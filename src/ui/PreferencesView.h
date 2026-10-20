#pragma once

#include "audio/AudioDeviceLayer.h"
#include "hosting/PluginRegistry.h"
#include "ui/AudioSetupView.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aural::ui
{

// Preferences (08-setup.md 8.2): a 160 px page list on the left and the page's
// rows (a 200 px label and a control, section headers in uppercase caption) on
// the right. Pages: General, Audio (the Audio Setup content inline), Recording,
// Editing, Plugins, Shortcuts (a searchable table of the keyboard map) and
// Diagnostics. Every value is a user setting read and written through the
// callbacks; the owner re-applies the ones with an immediate effect when
// onChanged fires. What has no effect yet says so in its hint.
class PreferencesView final : public juce::Component
{
public:
    PreferencesView(const Theme& theme,
                    audio::AudioDeviceLayer& audio,
                    const hosting::PluginRegistry& registry);
    ~PreferencesView() override;

    std::function<std::string(const std::string& key)> loadSetting;
    std::function<void(const std::string& key, const std::string& value)> saveSetting;
    std::function<void()> onChanged;
    std::function<void()> onClearBlocklist;
    std::function<void()> onScan;
    std::function<juce::String()> systemReport;
    std::function<void(const juce::String& message)> onNotice;
    // The Audio page's pass-through warning (a toast in the main window).
    std::function<void(const juce::String& message)> onWarning;

    void showPage(const juce::String& key); // "general", "audio", "recording", "editing", "plugins",
                                            // "shortcuts", "diagnostics"
    juce::String currentPage() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

    static constexpr int preferredWidth = 820;
    static constexpr int preferredHeight = 600;
    static constexpr int listWidth = 160;

    // The settings keys (shared with the owner, which applies them).
    struct Keys
    {
        static constexpr const char* projectFolder = "general.projectFolder";
        static constexpr const char* recentCount = "general.recentCount";
        static constexpr const char* accent = "general.accent";
        static constexpr const char* uiScale = "general.uiScale";
        static constexpr const char* recordingFormat = "recording.format";
        static constexpr const char* countIn = "recording.countIn";
        static constexpr const char* preRoll = "recording.preRollBars";
        static constexpr const char* metronomeLevel = "recording.metronomeLevelDb";
        static constexpr const char* namePattern = "recording.namePattern";
        static constexpr const char* snap = "editing.snap";
        static constexpr const char* snapGrid = "editing.snapGrid";
        static constexpr const char* fadeMs = "editing.fadeMs";
        static constexpr const char* ripple = "editing.ripple";
        static constexpr const char* followPlayhead = "editing.followPlayhead";
        static constexpr const char* pluginPaths = "plugins.paths";
        static constexpr const char* scanOnLaunch = "plugins.scanOnLaunch";
        static constexpr const char* pluginsOnTop = "plugins.pin";
        static constexpr const char* crashReports = "diagnostics.crashReports";
    };

private:
    class Page;
    class PageList;
    class Swatches;
    class Shortcuts;

    std::string get(const char* key, const std::string& fallback = {}) const;
    void set(const char* key, const std::string& value);
    void buildPages();
    void writeAccentOverride(const juce::String& hex);

    const Theme& theme_;
    audio::AudioDeviceLayer& audio_;
    const hosting::PluginRegistry& registry_;
    std::unique_ptr<PageList> list_;
    std::vector<std::unique_ptr<Page>> pages_;
    std::vector<juce::String> keys_;
    juce::Viewport content_;
    int current_ = 0;
    bool updating_ = false;
};

} // namespace aural::ui
