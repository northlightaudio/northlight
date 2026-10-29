#include "core/Command.h"
#include "core/Model.h"
#include "engine/Meters.h"
#include "hosting/HostFactory.h"
#include "plugins/BuiltIns.h"
#include "plugins/Glue.h"
#include "ui/InsertMenu.h"
#include "ui/MixerView.h"
#include "ui/PluginBrowser.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// The insert slots' shared menu and states (ui/InsertMenu.h), the plugin
// browser's counts and empty states, and the mixer's hidden strips.

using namespace northlight;

namespace
{
struct Found
{
    juce::String text;
    int id = 0;
    bool enabled = false, ticked = false;
};
void collect(const juce::PopupMenu& menu, std::vector<Found>& out)
{
    for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
    {
        const auto& item = it.getItem();
        if (item.subMenu != nullptr)
        {
            out.push_back({item.text, 0, item.isEnabled, false});
            collect(*item.subMenu, out);
        }
        else if (!item.isSeparator)
            out.push_back({item.text, item.itemID, item.isEnabled, item.isTicked});
    }
}
const Found* find(const std::vector<Found>& items, const juce::String& text)
{
    for (const auto& item : items)
        if (item.text == text)
            return &item;
    return nullptr;
}
const Found* findStarting(const std::vector<Found>& items, const juce::String& text)
{
    for (const auto& item : items)
        if (item.text.startsWith(text))
            return &item;
    return nullptr;
}

// A processor that says it crashed.
class CrashedProcessor final : public engine::InsertProcessor
{
public:
    const std::string& typeName() const noexcept override { return type_; }
    const std::vector<engine::ParameterInfo>& parameters() const noexcept override { return params_; }
    void prepare(double, int) override {}
    void setParameter(int, float) noexcept override {}
    float parameter(int) const noexcept override { return 0.0f; }
    void process(float*, float*, int) noexcept override {}
    bool crashed() const noexcept override { return true; }

private:
    std::string type_ = "vst3:crashy";
    std::vector<engine::ParameterInfo> params_;
};

core::Project projectWithInserts()
{
    core::Project p;
    core::Track t;
    t.id = 1;
    t.name = "Gtr";
    for (core::Id id : {core::Id{50}, core::Id{51}})
    {
        core::Insert i;
        i.id = id;
        i.type = id == 50 ? plugins::types::eq : plugins::types::reverb;
        i.params = plugins::defaultParameters(i.type);
        t.inserts.push_back(i);
    }
    p.tracks.push_back(t);
    p.nextId = 100;
    return p;
}
} // namespace

TEST_CASE("Every insert's menu offers open, presets, replace, order, copy, paste and remove",
          "[ui][inserts][menu]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    auto project = projectWithInserts();
    std::vector<std::unique_ptr<core::Command>> issued;
    ui::PluginActions actions;
    ui::InsertMenuRequest request;
    request.project = &project;
    request.channelId = 1;
    request.insertId = 50;
    request.actions = &actions;
    request.onCommand = [&](std::unique_ptr<core::Command> c) { issued.push_back(std::move(c)); };
    ui::copyInserts({});

    juce::PopupMenu menu;
    std::vector<std::function<void()>> run;
    ui::addInsertMenuItems(menu, run, request);
    std::vector<Found> items;
    collect(menu, items);
    for (const char* text :
         {"Open", "Bypass", "Replace", "Move up", "Move down", "Wet/dry", "Copy", "Remove"})
        CHECK(find(items, text) != nullptr);
    CHECK(findStarting(items, "Presets") != nullptr); // the EQ's factory presets
    CHECK_FALSE(find(items, "Move up")->enabled);     // the first insert
    CHECK_FALSE(findStarting(items, "Paste")->enabled);
    CHECK(find(items, "Copy chain")->enabled);

    // Copy, then paste after the reverb: one AddInserts.
    run[static_cast<size_t>(find(items, "Copy")->id - 1)]();
    REQUIRE(ui::copiedInserts().size() == 1);
    request.insertId = 51;
    juce::PopupMenu second;
    std::vector<std::function<void()>> runSecond;
    ui::addInsertMenuItems(second, runSecond, request);
    std::vector<Found> secondItems;
    collect(second, secondItems);
    const auto* paste = findStarting(secondItems, "Paste EQ after");
    REQUIRE(paste != nullptr);
    CHECK(paste->enabled);
    runSecond[static_cast<size_t>(paste->id - 1)]();
    REQUIRE(issued.size() == 1);
    REQUIRE(issued[0]->apply(project));
    REQUIRE(project.tracks[0].inserts.size() == 3);
    CHECK(project.tracks[0].inserts[2].type == plugins::types::eq);
    CHECK(project.tracks[0].inserts[2].id != 50);

    // Replace with a reverb: a ReplaceInsert keeping the slot.
    const auto* replaceReverb = [&]() -> const Found*
    {
        // The Reverb item under Replace's Reverb & Delay group (the first "Reverb" after "Replace").
        bool inReplace = false;
        for (const auto& item : secondItems)
        {
            if (item.text == "Replace")
                inReplace = true;
            else if (inReplace && item.text == "Reverb\tPlain algorithmic room" && item.id > 0)
                return &item;
        }
        return nullptr;
    }();
    REQUIRE(replaceReverb != nullptr);
    issued.clear();
    runSecond[static_cast<size_t>(replaceReverb->id - 1)]();
    REQUIRE(issued.size() == 1);
    CHECK(dynamic_cast<core::ReplaceInsert*>(issued[0].get()) != nullptr);
}

TEST_CASE("A missing or crashed plugin's slot says so and offers the way out", "[ui][inserts][menu]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    hosting::PluginRegistry registry;
    hosting::HostFactory host(registry);
    core::Project project = projectWithInserts();
    core::Insert missing;
    missing.id = 60;
    missing.type = "vst3:not-here";
    missing.pluginName = "Old Verb";
    project.tracks[0].inserts.push_back(missing);
    core::Insert crashed;
    crashed.id = 61;
    crashed.type = "vst3:crashy";
    project.tracks[0].inserts.push_back(crashed);

    bool scanned = false, reloaded = false;
    ui::PluginActions actions;
    actions.host = &host;
    const auto crashy = std::make_shared<CrashedProcessor>();
    actions.live = [crashy](core::Id id) -> std::shared_ptr<engine::InsertProcessor>
    { return id == 61 ? crashy : nullptr; };
    actions.scan = [&] { scanned = true; };
    actions.reload = [&](core::Id id) { reloaded = id == 61; };

    const auto troubleMissing = ui::insertTrouble(missing, &actions);
    CHECK(troubleMissing.kind == ui::InsertTrouble::Kind::missing);
    CHECK(juce::String(troubleMissing.label()) == "Not installed");
    CHECK(ui::insertName(missing, &actions) == "Old Verb"); // named as when it was saved
    const auto troubleCrashed = ui::insertTrouble(crashed, &actions);
    CHECK(troubleCrashed.kind == ui::InsertTrouble::Kind::crashed);
    CHECK(ui::insertTrouble(project.tracks[0].inserts[0], &actions).kind == ui::InsertTrouble::Kind::none);

    ui::InsertMenuRequest request;
    request.project = &project;
    request.channelId = 1;
    request.insertId = 60;
    request.actions = &actions;
    juce::PopupMenu menu;
    std::vector<std::function<void()>> run;
    ui::addInsertMenuItems(menu, run, request);
    std::vector<Found> items;
    collect(menu, items);
    CHECK_FALSE(find(items, "Open")->enabled);
    const auto* scan = findStarting(items, "Scan for plugins");
    REQUIRE(scan != nullptr);
    run[static_cast<size_t>(scan->id - 1)]();
    CHECK(scanned);

    request.insertId = 61;
    juce::PopupMenu crashedMenu;
    std::vector<std::function<void()>> runCrashed;
    ui::addInsertMenuItems(crashedMenu, runCrashed, request);
    std::vector<Found> crashedItems;
    collect(crashedMenu, crashedItems);
    const auto* reload = find(crashedItems, "Reload plugin");
    REQUIRE(reload != nullptr);
    runCrashed[static_cast<size_t>(reload->id - 1)]();
    CHECK(reloaded);
}

TEST_CASE("The plugin browser counts the built-ins, describes them once and says when nothing matches",
          "[ui][browser]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    hosting::PluginRegistry registry;
    ui::PluginBrowser browser(registry);
    const auto builtIns = plugins::BuiltInFactory().typeNames().size(); // MIDI effects are not listed
    CHECK(browser.countText() == juce::String(static_cast<int>(builtIns)) + " built-in, 0 plugins");
    CHECK(browser.emptyText().isEmpty());
    // Every built-in has a description for the browser and the effects menu.
    for (const auto& b : plugins::builtInTypes())
        CHECK(std::string(b.description).size() > 3);
    // A search that matches nothing says so.
    browser.setSearchText("zzzz no plugin is called this");
    CHECK(browser.rowTexts().empty());
    CHECK(browser.emptyText() == "No plugins match");
    // A search finds a built-in by what it is, not only its name.
    browser.setSearchText("Chorus pedal");
    const auto chorus = browser.rowTexts();
    CHECK(std::find(chorus.begin(), chorus.end(), juce::String("Tide")) != chorus.end());
    // Showing one plugin clears the filters and finds it.
    browser.showPlugin(plugins::glue::type);
    const auto glue = browser.rowTexts();
    CHECK(std::find(glue.begin(), glue.end(), juce::String("Glue Compressor")) != glue.end());
}

TEST_CASE("Hidden strips leave the mixer and are counted for showing again", "[ui][mixer]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    core::Project project;
    for (core::Id id : {core::Id{1}, core::Id{2}})
    {
        core::Track t;
        t.id = id;
        t.name = "T" + std::to_string(id);
        t.mixerHidden = id == 2;
        project.tracks.push_back(t);
    }
    rt::MeterBlock meters;
    ui::MeterReadings readings(meters);
    ui::MixerView mixer(readings, nullptr, nullptr);
    mixer.setBounds(0, 0, 1200, 700);
    mixer.setProject(&project);
    CHECK(mixer.hiddenStripCount() == 1);
    project.tracks[1].mixerHidden = false;
    mixer.setProject(&project);
    CHECK(mixer.hiddenStripCount() == 0);
}

TEST_CASE("A MIDI effect's menu moves, replaces, copies and removes it in the MIDI FX, with no wet/dry",
          "[ui][inserts][menu][midifx]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    core::Project project;
    core::Track midi;
    midi.id = 1;
    midi.kind = core::TrackKind::midi;
    for (core::Id id : {core::Id{70}, core::Id{71}})
    {
        core::Insert i;
        i.id = id;
        i.type = id == 70 ? "northlight.arpeggiator" : "northlight.scale";
        i.params = plugins::defaultParameters(i.type);
        midi.midiInserts.push_back(i);
    }
    project.tracks.push_back(midi);
    project.nextId = 100;
    std::vector<std::unique_ptr<core::Command>> issued;
    ui::PluginActions actions;
    ui::InsertMenuRequest request;
    request.project = &project;
    request.channelId = 1;
    request.insertId = 70;
    request.midiEffect = true;
    request.actions = &actions;
    request.onCommand = [&](std::unique_ptr<core::Command> c) { issued.push_back(std::move(c)); };
    ui::copyInserts({});

    juce::PopupMenu menu;
    std::vector<std::function<void()>> run;
    ui::addInsertMenuItems(menu, run, request);
    std::vector<Found> items;
    collect(menu, items);
    for (const char* text : {"Bypass", "Replace", "Move up", "Move down", "Copy", "Remove"})
        CHECK(find(items, text) != nullptr);
    CHECK(find(items, "Open") == nullptr);    // edited on its card
    CHECK(find(items, "Wet/dry") == nullptr); // MIDI has no dry signal
    CHECK(find(items, "Move down")->enabled);
    // Replace offers the MIDI effects, and no audio effect.
    CHECK(findStarting(items, "Chord Pads") != nullptr);
    CHECK(findStarting(items, "Reverb") == nullptr);

    // Move down: a MoveInsert in the MIDI FX.
    run[static_cast<size_t>(find(items, "Move down")->id - 1)]();
    REQUIRE(issued.size() == 1);
    REQUIRE(issued[0]->apply(project));
    CHECK(project.tracks[0].midiInserts[1].id == 70);

    // An audio insert copied is not offered for pasting here.
    core::Insert eq;
    eq.type = plugins::types::eq;
    ui::copyInserts({eq});
    juce::PopupMenu second;
    std::vector<std::function<void()>> runSecond;
    ui::addInsertMenuItems(second, runSecond, request);
    std::vector<Found> secondItems;
    collect(second, secondItems);
    CHECK_FALSE(findStarting(secondItems, "Paste")->enabled);

    // Remove: from the MIDI FX.
    runSecond[static_cast<size_t>(find(secondItems, "Remove")->id - 1)]();
    REQUIRE(issued.size() == 2);
    REQUIRE(issued[1]->apply(project));
    REQUIRE(project.tracks[0].midiInserts.size() == 1);
    CHECK(project.tracks[0].midiInserts[0].id == 71);
    ui::copyInserts({});
}

TEST_CASE("The MIDI effects menu lists every built-in MIDI effect once", "[ui][menu][midifx]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto built =
        ui::buildPluginMenu(ui::PluginKind::midiEffect, static_cast<const ui::PluginSources*>(nullptr));
    auto expected = plugins::BuiltInFactory::midiEffectTypeNames();
    auto listed = built.types;
    std::sort(expected.begin(), expected.end());
    std::sort(listed.begin(), listed.end());
    CHECK(listed == expected);
    // And the effects menu lists none of them.
    const auto effects =
        ui::buildPluginMenu(ui::PluginKind::effect, static_cast<const ui::PluginSources*>(nullptr));
    for (const auto& type : effects.types)
        CHECK_FALSE(plugins::BuiltInFactory::isMidiEffect(type));
}
