#pragma once

#include "audio/AudioDeviceLayer.h"
#include "core/Settings.h"
#include "hosting/PluginRegistry.h"
#include "ui/AudioSetupView.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace northlight::ui
{

class NumericField;
class SegmentedControl;

// Preferences (screens/setup.md 8.2): the page list on canvas at the left and the
// page on surface0, its settings in cards like Audio Setup's. A card row is a label
// in the 120 px column, a control as wide as what it holds, an optional Coming soon
// badge and a wrapping hint. Pages: General, Audio (the Audio Setup content
// inline), Recording, Editing, Plugins, Shortcuts (a searchable table of the
// keyboard map) and Diagnostics. Every value is a user setting read and written
// through the callbacks, keyed and defaulted by the settings schema
// (core/Settings.h); the owner re-applies the ones with an immediate effect when
// onChanged fires.
class PreferencesView final : public juce::Component
{
public:
    PreferencesView(const Theme& theme,
                    audio::AudioDeviceLayer& audio,
                    const hosting::PluginRegistry& registry);
    ~PreferencesView() override;

    // The settings folder: the Support page shows it and the accent override
    // is written there. Set before the view is shown.
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
    // The height that shows the whole page without scrolling (a capture saves it whole).
    int fullHeight() const
    {
        const auto* page = content_.getViewedComponent();
        return page != nullptr ? content_.getY() + page->getHeight() : getHeight();
    }

    void paint(juce::Graphics& g) override;
    void resized() override;
    // The pages are built the first time the view is laid out with its callbacks
    // set; a window adding it without resizing it lays it out here.
    void parentHierarchyChanged() override;

    static constexpr int preferredWidth = 820;
    static constexpr int preferredHeight = 600;
    static constexpr int listWidth = 160;
    static constexpr int maxCardWidth = 600;

private:
    class Page;
    class PageList;

    // A setting's stored text, or its default (core/Settings.h).
    std::string get(const settings::Setting& setting) const;
    void set(const settings::Setting& setting, const std::string& value);
    // The pages in list order (key, title, icon, builder), and each page's builder.
    struct PageInfo;
    static const std::vector<PageInfo>& pageTable();
    void buildPages();
    void buildGeneral(Page& page);
    void buildAudio(Page& page);
    void buildRecording(Page& page);
    void buildEditing(Page& page);
    void buildPlugins(Page& page);
    void buildShortcuts(Page& page);
    void buildDiagnostics(Page& page);

    // The controls a row binds to a setting; each writes it back as it changes.
    struct Choice
    {
        juce::String text;
        std::string value;
        bool comingSoon = false;
    };
    std::unique_ptr<juce::ComboBox> makeCombo(const std::vector<Choice>& items, const settings::Setting& key);
    std::unique_ptr<SegmentedControl> makeSegmented(const std::vector<Choice>& items,
                                                    const settings::Setting& key);
    std::unique_ptr<juce::ToggleButton> makeCheckbox(const settings::Setting& key);
    std::unique_ptr<juce::TextEditor> makeText(const settings::Setting& key, const juce::String& placeholder);
    std::unique_ptr<NumericField> makeNumber(const settings::Setting& key,
                                             double lo,
                                             double hi,
                                             double step,
                                             const juce::String& unit,
                                             int decimals);
    // A button and the width its label needs.
    std::pair<std::unique_ptr<juce::TextButton>, int> makeButtonRow(const juce::String& label,
                                                                    std::function<void()> action);
    void writeAccentOverride(const juce::String& hex);
    // Writes the accent choice to its own token file beside the
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
