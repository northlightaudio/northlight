#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "plugins/Patina.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/components/RotarySelector.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/EffectPresetLibrary.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
namespace pa = plugins::patina;

namespace
{
// The card's stage windows, by their IDs ("stage:Heat").
juce::Component* tileFor(juce::Component& card, const juce::String& name)
{
    return card.findChildWithID("stage:" + name);
}

juce::Button* buttonFor(juce::Component& card, const juce::String& tooltip)
{
    for (auto* child : card.getChildren())
        if (auto* b = dynamic_cast<juce::Button*>(child); b != nullptr && b->getTooltip() == tooltip)
            return b;
    return nullptr;
}

std::vector<ui::RotarySelector*> visibleSelectors(juce::Component& card)
{
    std::vector<ui::RotarySelector*> out;
    for (auto* child : card.getChildren())
        if (auto* s = dynamic_cast<ui::RotarySelector*>(child); s != nullptr && s->isVisible())
            out.push_back(s);
    return out;
}
} // namespace

TEST_CASE("Patina's card reads its stages live and its tiles switch the controls shown", "[ui][patina]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(pa::type);
    live->prepare(48000.0, 240);
    live->setBypassed(false);
    core::Insert insert;
    insert.id = 12;
    insert.type = pa::type;
    insert.params = plugins::defaultParameters(insert.type);
    for (size_t i = 0; i < insert.params.size(); ++i)
        live->setParameter(static_cast<int>(i), insert.params[i]);
    std::vector<std::unique_ptr<core::Command>> sent;
    ui::strip::CardHost host;
    host.liveInsert = [&](core::Id) { return live; };
    host.onCommand = [&](std::unique_ptr<core::Command> c) { sent.push_back(std::move(c)); };
    auto card = ui::strip::createBuiltInCard(theme, host, core::masterId, insert);
    REQUIRE(card != nullptr);
    card->setBounds(0, 0, card->preferredWidth(), 420);
    card->tick(16.0); // starts watching

    // A second of a tone: the stages report and the card draws them.
    std::vector<float> l(240), r(240);
    double phase = 0.0;
    for (int b = 0; b < 200; ++b)
    {
        for (int i = 0; i < 240; ++i)
        {
            l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
                0.3f * static_cast<float>(std::sin(phase));
            phase += 2.0 * std::numbers::pi * 220.0 / 48000.0;
        }
        live->process(l.data(), r.data(), 240);
        if (b % 4 == 0)
            card->tick(20.0);
    }
    CHECK(card->inputDb() > -20.0f);
    const auto image = card->createComponentSnapshot(card->getLocalBounds(), true, 1.0f);
    CHECK(image.isValid());

    // Six tiles, in signal order.
    auto* heat = tileFor(*card, "Heat");
    auto* chamber = tileFor(*card, "Chamber");
    REQUIRE(heat != nullptr);
    REQUIRE(chamber != nullptr);
    for (const auto* name : {"Drift", "Grain", "Wear", "Crush"})
        CHECK(tileFor(*card, name) != nullptr);
    CHECK(heat->getX() < chamber->getX());

    // Heat is shown: one selector (its character), set from the model.
    heat->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    auto selectors = visibleSelectors(*card);
    REQUIRE(selectors.size() == 1);
    CHECK(selectors[0]->selected() == static_cast<int>(insert.params[pa::heatCharacter]));
    // The arrow moves to Chamber, whose space selector replaces it.
    heat->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    selectors = visibleSelectors(*card);
    REQUIRE(selectors.size() == 1);
    CHECK(selectors[0]->selected() == static_cast<int>(insert.params[pa::chamberType]));
    selectors[0]->keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
    CHECK(card->param(pa::chamberType) == static_cast<float>(pa::room));

    // Space on a tile switches its stage (with a command).
    const auto commands = sent.size();
    CHECK(card->param(pa::chamberOn) == 0.0f);
    chamber->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    CHECK(card->param(pa::chamberOn) == 1.0f);
    CHECK(sent.size() > commands);

    // A model change moves the controls without a command.
    const auto before = sent.size();
    insert.params[pa::chamberType] = static_cast<float>(pa::plate);
    card->refresh(insert);
    CHECK(visibleSelectors(*card)[0]->selected() == pa::plate);
    CHECK(sent.size() == before);

    // Every stage draws, at 2x too. With NORTHLIGHT_TEST_CAPTURE set to a
    // folder, the snapshots are saved there for review.
    const auto folder = juce::SystemStats::getEnvironmentVariable("NORTHLIGHT_TEST_CAPTURE", {});
    for (const auto* name : {"Heat", "Chamber", "Drift", "Grain", "Wear", "Crush"})
    {
        tileFor(*card, name)->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
        const auto shot = card->createComponentSnapshot(card->getLocalBounds(), true, 2.0f);
        CHECK(shot.isValid());
        if (folder.isNotEmpty())
        {
            const auto file = juce::File(folder).getChildFile(juce::String("patina-") + name + ".png");
            file.deleteFile();
            juce::FileOutputStream stream(file);
            juce::PNGImageFormat().writeImageToStream(shot, stream);
        }
    }
}

TEST_CASE("Patina's preset keys step through the factory presets as one undo step each", "[ui][patina]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    // An empty presets folder: the user's own presets never reach the test.
    const auto root =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("northlight-patina-presets-" + juce::String::toHexString(juce::Random().nextInt()));
    ui::strip::EffectPresetLibrary::instance().setRoot(root);
    const auto& theme = ui::Theme::dark();
    core::Insert insert;
    insert.id = 13;
    insert.type = pa::type;
    insert.params = plugins::defaultParameters(insert.type);
    std::vector<std::unique_ptr<core::Command>> sent;
    int gestures = 0;
    ui::strip::CardHost host;
    host.liveInsert = [](core::Id) { return std::shared_ptr<engine::InsertProcessor>(); };
    host.onCommand = [&](std::unique_ptr<core::Command> c) { sent.push_back(std::move(c)); };
    host.onGestureBegin = [&](const std::string&) { ++gestures; };
    host.onGestureEnd = [] {};
    auto card = ui::strip::createBuiltInCard(theme, host, core::masterId, insert);
    REQUIRE(card != nullptr);
    card->setBounds(0, 0, card->preferredWidth(), card->preferredBodyHeight());

    // A new Patina is the first preset; the display names it.
    auto* display = buttonFor(*card, "Presets");
    REQUIRE(display != nullptr);
    auto* next = buttonFor(*card, "Next preset");
    auto* previous = buttonFor(*card, "Previous preset");
    REQUIRE(next != nullptr);
    REQUIRE(previous != nullptr);

    next->onClick();
    const auto& second = pa::presets()[1];
    for (size_t i = 0; i < second.values.size(); ++i)
        CHECK(card->param(static_cast<int>(i)) == second.values[i]);
    CHECK(gestures == 1);
    CHECK(!sent.empty());
    CHECK(display->getButtonText() == juce::String(second.name));

    // Back past the first wraps to the last.
    previous->onClick();
    previous->onClick();
    CHECK(display->getButtonText() == juce::String(pa::presets().back().name));

    // A change of any value is no longer a preset.
    insert.params = pa::presets().back().values;
    insert.params[pa::heatDrive] += 1.0f;
    card->refresh(insert);
    CHECK(display->getButtonText() == "Custom");
}
