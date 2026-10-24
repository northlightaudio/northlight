#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "plugins/Patina.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/EffectPresetLibrary.h"
#include "ui/strip/HardwareKey.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

using namespace northlight;
namespace pa = plugins::patina;

namespace
{
// Every component under `root` (the face holds the controls, not the card).
void forEachChild(juce::Component& root, const std::function<void(juce::Component&)>& visit)
{
    for (auto* child : root.getChildren())
    {
        visit(*child);
        forEachChild(*child, visit);
    }
}

// A stage's window, by its ID ("stage:Heat").
juce::Component* windowFor(juce::Component& card, const juce::String& name)
{
    juce::Component* found = nullptr;
    forEachChild(card,
                 [&](juce::Component& c)
                 {
                     if (c.getComponentID() == "stage:" + name)
                         found = &c;
                 });
    return found;
}

// The knob bound to a parameter (titled with its name).
ui::Knob* knobFor(juce::Component& card, int index)
{
    const auto name = juce::String(plugins::parameterInfo(pa::type)[static_cast<size_t>(index)].name);
    ui::Knob* found = nullptr;
    forEachChild(card,
                 [&](juce::Component& c)
                 {
                     if (auto* k = dynamic_cast<ui::Knob*>(&c); k != nullptr && k->getTitle() == name)
                         found = k;
                 });
    return found;
}

std::vector<ui::Knob*> visibleKnobs(juce::Component& card)
{
    std::vector<ui::Knob*> out;
    forEachChild(card,
                 [&](juce::Component& c)
                 {
                     if (auto* k = dynamic_cast<ui::Knob*>(&c); k != nullptr && k->isVisible())
                         out.push_back(k);
                 });
    return out;
}

juce::Button* buttonFor(juce::Component& card, const juce::String& tooltip)
{
    juce::Button* found = nullptr;
    forEachChild(card,
                 [&](juce::Component& c)
                 {
                     if (auto* b = dynamic_cast<juce::Button*>(&c);
                         b != nullptr && b->getTooltip() == tooltip)
                         found = b;
                 });
    return found;
}

ui::strip::HardwareKey* keyFor(juce::Component& card, const juce::String& tooltip)
{
    ui::strip::HardwareKey* found = nullptr;
    forEachChild(card,
                 [&](juce::Component& c)
                 {
                     if (auto* k = dynamic_cast<ui::strip::HardwareKey*>(&c);
                         k != nullptr && k->getTooltip() == tooltip)
                         found = k;
                 });
    return found;
}
} // namespace

TEST_CASE("Patina's card reads its stages live and shows every stage's controls at once", "[ui][patina]")
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
    card->setBounds(0, 0, card->preferredWidth(), card->preferredBodyHeight() + 40);
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

    // Six windows, in signal order.
    auto* heat = windowFor(*card, "Heat");
    auto* chamber = windowFor(*card, "Chamber");
    REQUIRE(heat != nullptr);
    REQUIRE(chamber != nullptr);
    for (const auto* name : {"Drift", "Grain", "Wear", "Crush"})
        CHECK(windowFor(*card, name) != nullptr);
    CHECK(heat->getX() < chamber->getX());

    // Every control shows at once: a knob for each parameter but the six switches.
    CHECK(visibleKnobs(*card).size() ==
          static_cast<size_t>(static_cast<int>(pa::count) - static_cast<int>(pa::stageCount)));

    // A stage's type selector clicks between its positions, set from the model.
    auto* character = knobFor(*card, pa::heatCharacter);
    REQUIRE(character != nullptr);
    CHECK(character->value() == static_cast<double>(insert.params[pa::heatCharacter]));
    auto* space = knobFor(*card, pa::chamberType);
    REQUIRE(space != nullptr);
    space->keyPressed(juce::KeyPress(juce::KeyPress::endKey));
    CHECK(card->param(pa::chamberType) == static_cast<float>(pa::room));
    space->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    CHECK(card->param(pa::chamberType) == static_cast<float>(pa::spring));

    // Space on a window switches its stage (with a command).
    const auto commands = sent.size();
    CHECK(card->param(pa::chamberOn) == 0.0f);
    chamber->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    CHECK(card->param(pa::chamberOn) == 1.0f);
    CHECK(sent.size() > commands);

    // A model change moves the controls without a command.
    const auto before = sent.size();
    insert.params[pa::chamberType] = static_cast<float>(pa::plate);
    card->refresh(insert);
    CHECK(space->value() == static_cast<double>(pa::plate));
    CHECK(sent.size() == before);

    // The face draws at 2x too. With NORTHLIGHT_TEST_CAPTURE set to a folder, the
    // snapshot is saved there for review.
    const auto shot = card->createComponentSnapshot(card->getLocalBounds(), true, 2.0f);
    CHECK(shot.isValid());
    const auto folder = juce::SystemStats::getEnvironmentVariable("NORTHLIGHT_TEST_CAPTURE", {});
    if (folder.isNotEmpty())
    {
        const auto file = juce::File(folder).getChildFile("patina.png");
        file.deleteFile();
        juce::FileOutputStream stream(file);
        juce::PNGImageFormat().writeImageToStream(shot, stream);
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
    auto* next = keyFor(*card, "Next preset");
    auto* previous = keyFor(*card, "Previous preset");
    REQUIRE(next != nullptr);
    REQUIRE(previous != nullptr);

    next->onPress();
    const auto& second = pa::presets()[1];
    for (size_t i = 0; i < second.values.size(); ++i)
        CHECK(card->param(static_cast<int>(i)) == second.values[i]);
    CHECK(gestures == 1);
    CHECK(!sent.empty());
    CHECK(display->getButtonText() == juce::String(second.name));

    // Back past the first wraps to the last.
    previous->onPress();
    previous->onPress();
    CHECK(display->getButtonText() == juce::String(pa::presets().back().name));

    // A change of any value is no longer a preset.
    insert.params = pa::presets().back().values;
    insert.params[pa::heatDrive] += 1.0f;
    card->refresh(insert);
    CHECK(display->getButtonText() == "Custom");
}
