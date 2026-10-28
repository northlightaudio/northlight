#include "audio/AudioDeviceLayer.h"
#include "audio/AudioSettings.h"
#include "core/Settings.h"
#include "hosting/PluginRegistry.h"
#include "ui/NorthlightLookAndFeel.h"
#include "ui/PreferencesView.h"
#include "ui/ShortcutEditor.h"
#include "ui/Shortcuts.h"
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

TEST_CASE("Contrast and Reduce motion are stored as choices and write no token file",
          "[ui][preferences][accessibility]")
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
    ui::SegmentedControl* motion = nullptr;
    for (const auto& row : f.view->rowLayout())
    {
        if (row.label == "Contrast")
            contrast = dynamic_cast<ui::SegmentedControl*>(row.component);
        if (row.label == "Reduce motion")
            motion = dynamic_cast<ui::SegmentedControl*>(row.component);
    }
    REQUIRE(contrast != nullptr);
    REQUIRE(motion != nullptr);

    // High is applied from the token file's variant when the app reads the
    // setting, so nothing is written beside the settings for it.
    const auto appearance = folder.getChildFile(ui::Theme::preferencesOverrideName);
    contrast->setSelected(2, juce::sendNotificationSync); // System | Standard | High
    CHECK(f.settings[std::string(settings::contrast.key)] == "high");
    CHECK_FALSE(appearance.existsAsFile());
    CHECK(user.loadFileAsString() == handEdited);
    contrast->setSelected(0, juce::sendNotificationSync);
    CHECK(f.settings[std::string(settings::contrast.key)] == "system");

    motion->setSelected(1, juce::sendNotificationSync); // System | On | Off
    CHECK(f.settings[std::string(settings::reduceMotion.key)] == "on");
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
    CHECK_FALSE(badged("recording", "Pre-roll")); // recordings use them now
    CHECK_FALSE(badged("recording", "Take names"));
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
    Fixture f({{std::string(settings::recentCount.key), "12"}, {std::string(settings::snap.key), "0"}});
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

TEST_CASE("The recording format is read, so it carries no Coming soon badge", "[ui][preferences]")
{
    Fixture f;
    f.view->showPage("recording");
    bool formatBadged = true;
    for (const auto& row : f.view->rowLayout())
        if (row.label == "Format")
            formatBadged = row.badge;
    CHECK_FALSE(formatBadged);
}

TEST_CASE("Each Preferences page opens by its key, in list order", "[ui][preferences]")
{
    Fixture f;
    const auto& keys = ui::PreferencesView::pageKeys();
    REQUIRE(keys.size() == 7);
    CHECK(keys.front() == "general");
    CHECK(keys.back() == "diagnostics");
    for (const auto& key : keys)
    {
        f.view->showPage(key);
        CHECK(f.view->currentPage() == key);
    }
}

TEST_CASE("The Shortcuts page rebinds a key, warns of a conflict and saves the bindings",
          "[ui][preferences][shortcuts]")
{
    ui::Shortcuts::resetAll();
    Fixture f;
    f.view->showPage("shortcuts");
    ui::ShortcutEditor* editor = nullptr;
    for (const auto& row : f.view->rowLayout())
        if (auto* e = dynamic_cast<ui::ShortcutEditor*>(row.component))
            editor = e;
    REQUIRE(editor != nullptr);
    // Every action is listed, the piano roll's and audio editor's too.
    size_t listed = 0;
    for (const auto* action : editor->listedActions())
        listed += action != nullptr ? 1 : 0;
    CHECK(listed == ui::Shortcuts::all().size());

    using Mods = juce::ModifierKeys;
    // A free key binds at once and is saved.
    editor->startRecording(ui::Command::click);
    CHECK(editor->isRecording());
    editor->recordKey(juce::KeyPress('K', Mods::commandModifier | Mods::altModifier, 0));
    CHECK_FALSE(editor->isRecording());
    CHECK(ui::Shortcuts::keyText(ui::Command::click) == "Ctrl+Alt+K");
    CHECK(juce::String(f.settings["shortcuts.bindings"]).contains("click="));

    // A key in use opens the conflict line; Cancel keeps both as they were.
    editor->startRecording(ui::Command::loop);
    editor->recordKey(juce::KeyPress('M', Mods::commandModifier, 0));
    CHECK(editor->conflictText().contains("Mute or unmute the selected clips"));
    editor->cancelRecording();
    CHECK(editor->conflictText().isEmpty());
    CHECK(ui::Shortcuts::keyText(ui::Command::loop) == "L");
    // Replace takes it.
    editor->startRecording(ui::Command::loop);
    editor->recordKey(juce::KeyPress('M', Mods::commandModifier, 0));
    editor->replace();
    CHECK(ui::Shortcuts::keyText(ui::Command::loop) == "Ctrl+M");
    CHECK(ui::Shortcuts::keyText(ui::Command::muteClips).isEmpty());

    // Esc cancels a recording; a fixed family does not record.
    editor->startRecording(ui::Command::undo);
    editor->recordKey(juce::KeyPress(juce::KeyPress::escapeKey));
    CHECK(ui::Shortcuts::isDefault(ui::Command::undo));
    editor->startRecording(ui::Command::pickTool);
    CHECK_FALSE(editor->isRecording());

    // The search finds actions by key; Reset all puts every key back and saves nothing.
    editor->setSearch("Ctrl+Alt+K");
    CHECK(editor->rows() == 2); // the section header and Click
    editor->resetAll();
    CHECK(ui::Shortcuts::allDefault());
    CHECK(f.settings["shortcuts.bindings"].empty());
}

TEST_CASE("Return on stop is a Recording setting, on by default", "[ui][preferences]")
{
    Fixture f;
    f.view->showPage("recording");
    bool found = false;
    for (const auto& row : f.view->rowLayout())
        if (row.label == "Return on stop")
        {
            found = true;
            auto* box = dynamic_cast<juce::ToggleButton*>(row.component);
            REQUIRE(box != nullptr);
            CHECK(box->getToggleState());
            box->setToggleState(false, juce::dontSendNotification);
            box->onClick(); // what a click does after toggling
            CHECK(f.settings["transport.returnOnStop"] == "0");
        }
    CHECK(found);
}

// Pictures of the Shortcuts page in each of its states, hidden from the default run
// (northlight_juce_tests "[.shortcut-snapshots]"), saved at 100% and 200% in the
// folder NORTHLIGHT_SNAPSHOT_DIR names.
TEST_CASE("The Shortcuts page's states, saved as pictures", "[.shortcut-snapshots]")
{
    ui::Shortcuts::resetAll();
    const auto folder = juce::SystemStats::getEnvironmentVariable("NORTHLIGHT_SNAPSHOT_DIR", {});
    REQUIRE(folder.isNotEmpty());
    const juce::File dir(folder);
    REQUIRE(dir.createDirectory());
    Fixture f;
    ui::NorthlightLookAndFeel lnf(f.theme);
    juce::LookAndFeel::setDefaultLookAndFeel(&lnf);
    f.view->sendLookAndFeelChange();
    f.view->showPage("shortcuts");
    ui::ShortcutEditor* editor = nullptr;
    for (const auto& row : f.view->rowLayout())
        if (auto* e = dynamic_cast<ui::ShortcutEditor*>(row.component))
            editor = e;
    REQUIRE(editor != nullptr);
    const auto save = [&](const juce::String& name)
    {
        for (const float scale : {1.0f, 2.0f})
        {
            const auto image = f.view->createComponentSnapshot(f.view->getLocalBounds(), true, scale);
            const auto file = dir.getChildFile(name + (scale > 1.0f ? "@2x" : "") + ".png");
            file.deleteFile();
            juce::FileOutputStream out(file);
            REQUIRE(out.openedOk());
            CHECK(juce::PNGImageFormat().writeImageToStream(image, out));
        }
    };
    save("shortcuts-rest");
    ui::Shortcuts::bind(ui::Command::click, ui::Key{'K', juce::ModifierKeys::commandModifier});
    editor->setSearch({}); // rebuilds the rows: Click shows its reset button
    editor->startRecording(ui::Command::punch);
    save("shortcuts-recording");
    editor->recordKey(juce::KeyPress('M', juce::ModifierKeys::commandModifier, 0));
    save("shortcuts-conflict");
    editor->cancelRecording();
    ui::Shortcuts::bind(ui::Command::muteClips,
                        ui::Key{'Q', juce::ModifierKeys::altModifier}); // fills Editing
    ui::Shortcuts::bind(
        ui::Command::loop, ui::Key{'M', juce::ModifierKeys::commandModifier}, {ui::Command::muteClips});
    editor->setSearch("clips");
    save("shortcuts-search");
    editor->setSearch("nothing like this");
    save("shortcuts-nomatch");
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    ui::Shortcuts::resetAll();
}
