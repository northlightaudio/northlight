#include "core/Command.h"
#include "plugins/Aurora.h"
#include "plugins/BuiltIns.h"
#include "plugins/EffectPresets.h"
#include "plugins/Nebula.h"
#include "plugins/Starbit.h"
#include "ui/Theme.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/EffectPresetLibrary.h"
#include "ui/strip/PresetBar.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace northlight;
using ui::strip::EffectPresetLibrary;

namespace
{
// A presets folder of the test's own, emptied before and after.
struct TempRoot
{
    TempRoot()
    {
        folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile("northlight-effect-presets-" +
                                   juce::String::toHexString(juce::Random().nextInt()));
        folder.deleteRecursively();
        EffectPresetLibrary::instance().setRoot(folder);
    }
    ~TempRoot()
    {
        folder.deleteRecursively();
        EffectPresetLibrary::instance().setRoot(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                    .getChildFile("northlight-effect-presets-unused"));
    }
    juce::File folder;
};
} // namespace

TEST_CASE("A saved effect preset is listed under the user's, matched, and deleted", "[ui][presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const TempRoot root;
    auto& library = EffectPresetLibrary::instance();
    const auto& type = plugins::types::reverb;
    const auto factoryCount = plugins::factoryPresets(type).size();
    REQUIRE(library.presets(type).size() == factoryCount);

    // The defaults are the first factory preset.
    auto params = plugins::defaultParameters(type);
    CHECK(library.match(type, params) == 0);

    // A changed value is no preset ("Custom") until saved.
    params[plugins::reverb::decay] = 7.25f;
    CHECK(library.match(type, params) == -1);
    CHECK(library.save(type, "Long Tail", params).empty());
    const auto& list = library.presets(type);
    REQUIRE(list.size() == factoryCount + 1);
    CHECK(list.back().user);
    CHECK(list.back().name == "Long Tail");
    CHECK(list.back().values == params);
    CHECK(library.match(type, params) == static_cast<int>(factoryCount));
    CHECK(library.folderFor(type).getChildFile("Long Tail.json").existsAsFile());

    // Saving the same name again replaces it.
    params[plugins::reverb::decay] = 6.5f;
    CHECK(library.save(type, "Long Tail", params).empty());
    CHECK(library.presets(type).size() == factoryCount + 1);
    CHECK(library.presets(type).back().values == params);

    // A name nothing can be made of is refused.
    CHECK_FALSE(library.save(type, "   ", params).empty());

    const auto entry = library.presets(type).back();
    CHECK(library.remove(type, entry));
    CHECK(library.presets(type).size() == factoryCount);
}

TEST_CASE("A user preset loads by parameter name; a missing one takes its default", "[ui][presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const TempRoot root;
    auto& library = EffectPresetLibrary::instance();
    const auto& type = plugins::types::reverb;
    const auto folder = library.folderFor(type);
    REQUIRE(folder.createDirectory().wasOk());
    // An older file: keys in another order, one parameter missing, one unknown.
    folder.getChildFile("Old.json").replaceWithText(R"({"name": "Old", "type": "northlight.reverb",
                              "params": {"Mix": 40, "Decay": 3.5, "Shimmer": 1}})");
    // Another effect's preset in the wrong folder is ignored.
    folder.getChildFile("Stray.json")
        .replaceWithText(R"({"name": "Stray", "type": "northlight.delay", "params": {"Mix": 40}})");
    library.reload(type);
    const auto& list = library.presets(type);
    REQUIRE(list.back().name == "Old");
    CHECK(list.size() == plugins::factoryPresets(type).size() + 1);
    const auto& infos = plugins::parameterInfo(type);
    for (size_t i = 0; i < infos.size(); ++i)
    {
        INFO(infos[i].name);
        const float expected = infos[i].name == "Mix"     ? 40.0f
                               : infos[i].name == "Decay" ? 3.5f
                                                          : infos[i].defaultValue;
        CHECK(list.back().values[i] == expected);
    }
}

TEST_CASE("The preset bar names what the effect is set to and steps through the list", "[ui][presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const TempRoot root;
    const auto& theme = ui::Theme::dark();
    const auto& type = plugins::types::delay;
    const auto& factory = plugins::factoryPresets(type);
    REQUIRE(factory.size() >= 3);

    ui::strip::PresetBar bar(theme, type);
    bar.setBounds(0, 0, bar.preferredWidth(), theme.sizes.controlCompact);
    std::vector<std::vector<float>> applied;
    std::vector<juce::String> names;
    bar.onApply = [&](const std::vector<float>& values, const juce::String& name)
    {
        applied.push_back(values);
        names.push_back(name);
    };
    bar.setValues(plugins::defaultParameters(type));
    CHECK(bar.shownName() == juce::String(factory.front().name));

    bar.step(1);
    REQUIRE(applied.size() == 1);
    CHECK(applied.back() == factory[1].values);
    CHECK(names.back() == juce::String(factory[1].name));
    CHECK(bar.shownName() == juce::String(factory[1].name));

    // Back from the first wraps to the last.
    bar.setValues(factory.front().values);
    bar.step(-1);
    CHECK(applied.back() == factory.back().values);

    // Values no preset has read Custom; Next from Custom goes to the first.
    auto custom = factory.front().values;
    custom[plugins::delay::mix] += 1.0f;
    bar.setValues(custom);
    CHECK(bar.shownName() == "Custom");
    bar.step(1);
    CHECK(applied.back() == factory.front().values);

    // A preset saved elsewhere names the bar at once.
    bar.setValues(custom);
    CHECK(EffectPresetLibrary::instance().save(type, "Mine", custom).empty());
    EffectPresetLibrary::instance().dispatchPendingMessages();
    CHECK(bar.shownName() == "Mine");
}

TEST_CASE("An instrument's presets come from the library and apply as one command", "[ui][presets]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const TempRoot root;
    auto& library = EffectPresetLibrary::instance();
    // The factory lists the instruments build are the library's, as an effect's are.
    for (const auto* list :
         {&plugins::aurora::presets(), &plugins::nebula::presets(), &plugins::starbit::presets()})
        REQUIRE_FALSE(list->empty());
    for (const auto& [type, list] : {std::pair{plugins::aurora::type, &plugins::aurora::presets()},
                                     std::pair{plugins::nebula::type, &plugins::nebula::presets()},
                                     std::pair{plugins::starbit::type, &plugins::starbit::presets()}})
    {
        INFO(type);
        const auto& entries = library.presets(type);
        REQUIRE(entries.size() == list->size());
        for (size_t i = 0; i < entries.size(); ++i)
            CHECK(entries[i].name == juce::String(list->at(i).name));
    }

    // Aurora's editor, its commands recorded.
    std::vector<std::unique_ptr<core::Command>> sent;
    ui::strip::CardHost host;
    host.onCommand = [&](std::unique_ptr<core::Command> c) { sent.push_back(std::move(c)); };
    host.onGestureBegin = [](const std::string&) {};
    host.onGestureEnd = [] {};
    core::Insert insert;
    insert.id = 7;
    insert.type = plugins::aurora::type;
    insert.params = plugins::defaultParameters(insert.type);
    auto card = ui::strip::createBuiltInCard(ui::Theme::dark(), host, core::masterId, insert);
    REQUIRE(card != nullptr);
    ui::strip::PresetBar* bar = nullptr;
    std::function<void(juce::Component&)> find = [&](juce::Component& c)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* b = dynamic_cast<ui::strip::PresetBar*>(child))
                bar = b;
            find(*child);
        }
    };
    find(*card);
    REQUIRE(bar != nullptr);
    const auto& presets = plugins::aurora::presets();
    CHECK(bar->shownName() == juce::String(presets[0].name)); // the defaults

    // The next preset: one command, named for it, with every value.
    bar->step(1);
    REQUIRE(sent.size() == 1);
    const auto* set = dynamic_cast<const core::SetInsertParameters*>(sent.front().get());
    REQUIRE(set != nullptr);
    CHECK(set->name() == "Preset " + presets[1].name);
    CHECK(set->values() == presets[1].values);
    CHECK(bar->shownName() == juce::String(presets[1].name));

    // The model coming back a hair off (a value read back from a file) still
    // matches, as an effect's does: within a part in ten thousand of the value.
    insert.params = presets[1].values;
    const auto largest = std::max_element(insert.params.begin(),
                                          insert.params.end(),
                                          [](float a, float b) { return std::abs(a) < std::abs(b); });
    REQUIRE(std::abs(*largest) > 10.0f);
    *largest *= 0.99995f;
    card->refresh(insert);
    CHECK(bar->shownName() == juce::String(presets[1].name));
}
