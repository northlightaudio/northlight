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

namespace northlight::ui
{

// Preferences (screens/setup.md 8.2): the page list on canvas at the left and the
// page on surface0, its settings in cards like Audio Setup's. A card row is a label
// in the 120 px column, a control as wide as what it holds, an optional Coming soon
// badge and a wrapping hint. Pages: General, Audio (the Audio Setup content
// inline), Recording, Editing, Plugins, Shortcuts (a searchable table of the
// keyboard map) and Diagnostics. Every value is a user setting read and written
// through the callbacks; the owner re-applies the ones with an immediate effect
// when onChanged fires.
class PreferencesView final : public juce::Component
{
public:
    PreferencesView(const Theme& theme,
                    audio::AudioDeviceLayer& audio,
                    const hosting::PluginRegistry& registry);
    ~PreferencesView() override;

    // The settings folder: the Support page shows it and the accent and contrast
    // choices are written there. Set before the view is shown.
    juce::File settingsFolder;
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
    static const std::vector<juce::String>& pageKeys();

    // The laid-out rows of the page showing, in page coordinates (for tests).
    struct RowLayout
    {
        juce::String label, hint;
        juce::Rectangle<int> card, labelBounds, control, hintBounds;
        juce::Component* component = nullptr;
        bool badge = false, bare = false;
    };
    std::vector<RowLayout> rowLayout() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    // The pages are built the first time the view is laid out with its callbacks
    // set; a window adding it without resizing it lays it out here.
    void parentHierarchyChanged() override;

    static constexpr int preferredWidth = 820;
    static constexpr int preferredHeight = 600;
    static constexpr int listWidth = 160;
    static constexpr int maxCardWidth = 600;

    // The settings keys (shared with the owner, which applies them).
    struct Keys
    {
        static constexpr const char* projectFolder = "general.projectFolder";
        static constexpr const char* recentCount = "general.recentCount";
        static constexpr const char* autosaveMinutes = "general.autosaveMinutes";
        static constexpr const char* theme = "general.theme";
        static constexpr const char* accent = "general.accent";
        static constexpr const char* uiScale = "general.uiScale";
        static constexpr const char* contrast = "general.contrast";
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

    std::string get(const char* key, const std::string& fallback = {}) const;
    void set(const char* key, const std::string& value);
    void buildPages();
    void writeAccentOverride(const juce::String& hex);
    // Writes the accent and contrast choices to their own token file beside the
    // settings (Theme::initialise reads it at the next launch). The user's
    // hand-edited tokens.json is never touched.
    void writeTokenOverride();
    // An async folder chooser starting at `start`; `chosen` gets the folder.
    void chooseFolder(const juce::String& title,
                      const juce::File& start,
                      std::function<void(const juce::File&)> chosen);

    const Theme& theme_;
    audio::AudioDeviceLayer& audio_;
    const hosting::PluginRegistry& registry_;
    std::unique_ptr<PageList> list_;
    std::vector<std::unique_ptr<Page>> pages_;
    juce::Viewport content_;
    std::unique_ptr<juce::FileChooser> chooser_;
    int current_ = 0;
    bool updating_ = false;
};

} // namespace northlight::ui
