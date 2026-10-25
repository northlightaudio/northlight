#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "hosting/PluginRegistry.h"
#include "ui/PreferencesView.h"
#include "ui/Theme.h"
#include "ui/components/NumericField.h"
#include "ui/components/SegmentedControl.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>

using namespace northlight;

namespace
{
class MemoryStore final : public audio::AudioSettingsStore
{
public:
    audio::AudioSettings loadAudioSettings() override { return settings_; }
    void saveAudioSettings(const audio::AudioSettings& settings) override { settings_ = settings; }

private:
    audio::AudioSettings settings_;
};

struct Fixture
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ui::Theme theme = ui::Theme::defaults();
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    hosting::PluginRegistry registry;
    std::map<std::string, std::string> settings;
    std::unique_ptr<ui::PreferencesView> view;

    explicit Fixture(std::map<std::string, std::string> saved = {}) : settings(std::move(saved))
    {
        // Built the way MainWindow builds it: sized before the callbacks are set.
        view = std::make_unique<ui::PreferencesView>(theme, audio, registry);
        view->loadSetting = [this](const std::string& key)
        {
            const auto it = settings.find(key);
            return it == settings.end() ? std::string() : it->second;
        };
        view->saveSetting = [this](const std::string& key, const std::string& value)
        { settings[key] = value; };
        view->setSize(ui::PreferencesView::preferredWidth, ui::PreferencesView::preferredHeight);
        view->showPage("general");
    }
};
} // namespace

TEST_CASE("Every Preferences label fits its column and every row stays inside its card", "[ui][preferences]")
{
    Fixture f;
    for (const auto& page : ui::PreferencesView::pageKeys())
    {
        f.view->showPage(page);
        const auto rows = f.view->rowLayout();
        REQUIRE_FALSE(rows.empty());
        for (const auto& row : rows)
        {
            INFO(page << ": " << row.label);
            if (row.bare)
                continue;
            if (row.label.isNotEmpty())
                CHECK(juce::GlyphArrangement::getStringWidth(f.theme.body(), row.label) <=
                      static_cast<float>(f.theme.sizes.labelWidth - f.theme.spacing.sm));
            const int inner = row.card.getRight() - f.theme.spacing.lg;
            CHECK(row.control.getRight() <= inner);
            CHECK(row.hintBounds.getRight() <= inner);
            CHECK(row.card.getWidth() <= ui::PreferencesView::maxCardWidth);
            if (row.hint.isNotEmpty())
                CHECK(row.hintBounds.getHeight() > 0);
        }
    }
}

TEST_CASE("Preferences controls are only as wide as what they hold", "[ui][preferences]")
{
    Fixture f;
    int combos = 0, numbers = 0, toggles = 0;
    for (const auto& page : ui::PreferencesView::pageKeys())
    {
        if (page == "audio")
            continue; // Audio Setup's own layout (8.1)
        f.view->showPage(page);
        for (const auto& row : f.view->rowLayout())
        {
            INFO(page << ": " << row.label);
            if (dynamic_cast<juce::ComboBox*>(row.component) != nullptr)
            {
                CHECK(row.control.getWidth() == 200);
                ++combos;
            }
            if (dynamic_cast<ui::NumericField*>(row.component) != nullptr)
            {
                CHECK(row.control.getWidth() == 96);
                ++numbers;
            }
            if (dynamic_cast<juce::ToggleButton*>(row.component) != nullptr)
            {
                CHECK(row.control.getWidth() == f.theme.sizes.icon);
                ++toggles;
            }
            if (auto* segmented = dynamic_cast<ui::SegmentedControl*>(row.component))
                CHECK(row.control.getWidth() == segmented->preferredWidth());
            if (auto* button = dynamic_cast<juce::TextButton*>(row.component))
                CHECK(row.control.getWidth() <
                      juce::GlyphArrangement::getStringWidthInt(f.theme.label(), button->getButtonText()) +
                          40);
        }
    }
    CHECK(combos >= 3);
    CHECK(numbers >= 5);
    CHECK(toggles >= 5);
}

TEST_CASE("Contrast and accent choices go to their own token file, never the user's", "[ui][preferences]")
{
    Fixture f;
    const auto folder =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("northlight-prefs-tokens");
    folder.deleteRecursively();
    REQUIRE(folder.createDirectory());
    const auto user = folder.getChildFile(ui::Theme::userOverrideName);
    const juce::String handEdited = R"({"color":{"canvas":"#101010"}})";
    REQUIRE(user.replaceWithText(handEdited));
    f.view->settingsFolder = folder;
    f.view->showPage("general");
    ui::SegmentedControl* contrast = nullptr;
    for (const auto& row : f.view->rowLayout())
        if (row.label == "Contrast")
            contrast = dynamic_cast<ui::SegmentedControl*>(row.component);
    REQUIRE(contrast != nullptr);

    const auto appearance = folder.getChildFile(ui::Theme::preferencesOverrideName);
    contrast->setSelected(1, juce::sendNotificationSync); // High
    REQUIRE(appearance.existsAsFile());
    CHECK(appearance.loadFileAsString().contains("lineSubtle"));
    CHECK(user.loadFileAsString() == handEdited);

    contrast->setSelected(0, juce::sendNotificationSync); // Standard: nothing left to override
    CHECK_FALSE(appearance.existsAsFile());
    CHECK(user.loadFileAsString() == handEdited);
    folder.deleteRecursively();
}

TEST_CASE("The Audio page's cards line up with every other page's", "[ui][preferences]")
{
    Fixture f;
    f.view->showPage("general");
    const auto general = f.view->rowLayout();
    REQUIRE_FALSE(general.empty());
    f.view->showPage("audio");
    const auto audio = f.view->rowLayout();
    REQUIRE(audio.size() == 1);
    CHECK(audio.front().bare);
    // Audio Setup insets its cards by space.xl, as a page does.
    CHECK(audio.front().control.getX() + f.theme.spacing.xl == general.front().card.getX());
}

TEST_CASE("A setting that is kept but not used yet carries the Coming soon badge", "[ui][preferences]")
{
    Fixture f;
    const auto badged = [&f](const juce::String& page, const juce::String& label)
    {
        f.view->showPage(page);
        for (const auto& row : f.view->rowLayout())
            if (row.label == label)
                return row.badge;
        FAIL_CHECK("no row " << label << " on " << page);
        return false;
    };
    CHECK(badged("recording", "Pre-roll"));
    CHECK(badged("recording", "Take names"));
    CHECK(badged("editing", "Fade"));
    CHECK(badged("diagnostics", "Send reports"));
    CHECK_FALSE(badged("recording", "Click level"));
    CHECK_FALSE(badged("general", "Autosave"));
}

TEST_CASE("The accent presets are named track palette colours, the default first", "[ui][theme]")
{
    const auto theme = ui::Theme::defaults();
    REQUIRE(theme.track.accentPresets.size() == 6);
    CHECK(theme.track.accentPresets.front() == "Overcast");
    for (const auto& name : theme.track.accentPresets)
    {
        INFO(name);
        CHECK(std::any_of(theme.track.palette.begin(),
                          theme.track.palette.end(),
                          [&name](const ui::Theme::TrackColour& entry) { return entry.name == name; }));
    }
}

TEST_CASE("A disabled segment cannot be chosen, and the arrow keys step over it", "[ui][segmented]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto theme = ui::Theme::defaults();
    ui::SegmentedControl control(theme);
    control.setSegments({"Off", "1 bar", "2 bars"});
    control.setSize(control.preferredWidth(), theme.sizes.control);
    control.setSegmentEnabled(1, false);
    CHECK_FALSE(control.isSegmentEnabled(1));
    control.setSelected(0, juce::dontSendNotification);
    int changes = 0;
    control.onChange = [&changes](int) { ++changes; };
    control.keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    CHECK(control.selected() == 2);
    control.keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
    CHECK(control.selected() == 0);
    CHECK(changes == 2);
}

TEST_CASE("Preferences show the saved settings, read once the owner's callbacks are set", "[ui][preferences]")
{
    Fixture f({{ui::PreferencesView::Keys::recentCount, "12"}, {ui::PreferencesView::Keys::snap, "0"}});
    const auto find = [&f](const juce::String& label) -> juce::Component*
    {
        for (const auto& row : f.view->rowLayout())
            if (row.label == label)
                return row.component;
        return nullptr;
    };
    auto* recent = dynamic_cast<ui::NumericField*>(find("Recent"));
    REQUIRE(recent != nullptr);
    CHECK(recent->value() == 12.0);
    f.view->showPage("editing");
    auto* snap = dynamic_cast<juce::ToggleButton*>(find("Snap"));
    REQUIRE(snap != nullptr);
    CHECK_FALSE(snap->getToggleState());
}

TEST_CASE("Preferences opened without a page shows General once it is in a window", "[ui][preferences]")
{
    // The menu's path: the view is sized at construction, gets its callbacks, and is
    // added to a window of the same size, so it is never resized and no page is named.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto theme = ui::Theme::defaults();
    MemoryStore store;
    audio::AudioDeviceLayer audio{store};
    hosting::PluginRegistry registry;
    ui::PreferencesView view(theme, audio, registry);
    view.loadSetting = [](const std::string&) { return std::string(); };
    view.saveSetting = [](const std::string&, const std::string&) {};
    CHECK(view.rowLayout().empty());
    juce::Component window;
    window.setSize(view.getWidth(), view.getHeight());
    window.addAndMakeVisible(view);
    CHECK(view.currentPage() == "general");
    CHECK_FALSE(view.rowLayout().empty());
}
