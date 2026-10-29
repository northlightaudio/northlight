// Cirrus's face (screens/plugins.md 7.4h): selecting an operator, choosing an
// algorithm, and the cells that depend on others.
#include "core/Command.h"
#include "core/Model.h"
#include "engine/Midi.h"
#include "plugins/BuiltIns.h"
#include "plugins/Cirrus.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/strip/BuiltInCards.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <vector>

using namespace northlight;
namespace cr = plugins::cirrus;

namespace
{
// A live Cirrus and its card, recording the parameter commands the card sends.
struct Rig
{
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(cr::type);
    core::Insert insert;
    ui::strip::CardHost host;
    std::unique_ptr<ui::strip::ModuleCard> card;
    std::vector<std::pair<int, float>> sent;

    explicit Rig(core::Id id = 31)
    {
        live->prepare(48000.0, 256);
        live->setBypassed(false);
        insert.id = id;
        insert.type = cr::type;
        insert.params = plugins::defaultParameters(insert.type);
        host.liveInsert = [this](core::Id) { return live; };
        host.onCommand = [this](std::unique_ptr<core::Command> c)
        {
            if (auto* set = dynamic_cast<core::SetInsertParameter*>(c.get()))
                sent.emplace_back(set->parameter(), set->value());
        };
        host.onGestureBegin = [](const std::string&) {};
        host.onGestureEnd = [] {};
        make();
    }

    void make()
    {
        card = ui::strip::createBuiltInCard(ui::Theme::dark(), host, core::masterId, insert);
        REQUIRE(card != nullptr);
        card->setBounds(0, 0, card->preferredWidth(), card->preferredBodyHeight() + 40);
        REQUIRE(card->createComponentSnapshot(card->getLocalBounds(), true, 1.0f).isValid());
        card->tick(16.0);
    }

    // A part of the face by its ID, `within` another part if given.
    juce::Component& part(const juce::String& id, const juce::String& within = {}) const
    {
        juce::Component* parent = card.get();
        if (within.isNotEmpty())
            parent = card->findChildWithID(within);
        REQUIRE(parent != nullptr);
        auto* c = parent->findChildWithID(id);
        INFO(id);
        REQUIRE(c != nullptr);
        return *c;
    }

    // The knob for a parameter, by its name.
    ui::Knob& knob(int index) const
    {
        const auto name = juce::String(card->info(index).name);
        ui::Knob* found = nullptr;
        for (auto* child : card->getChildren())
            if (auto* k = dynamic_cast<ui::Knob*>(child); k != nullptr && k->getTitle() == name)
                found = k;
        INFO(name);
        REQUIRE(found != nullptr);
        return *found;
    }

    void modelChange(int index, float value)
    {
        insert.params[static_cast<size_t>(index)] = value;
        card->refresh(insert);
    }
};

void click(juce::Component& c, juce::Point<float> at)
{
    const auto now = juce::Time::getCurrentTime();
    const juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),
                             at,
                             juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                             0.0f,
                             0.0f,
                             0.0f,
                             0.0f,
                             0.0f,
                             &c,
                             &c,
                             now,
                             at,
                             now,
                             1,
                             false);
    c.mouseDown(e);
    c.mouseUp(e);
}
} // namespace

TEST_CASE("Cirrus's face edits the envelope of the operator selected and keeps the choice", "[ui][cirrus]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig(41);
    CHECK(rig.part("envelope-attack", "display").getTitle() == "Op 1 attack");

    // A click on the third row's number selects it.
    click(rig.part("operator3"), {12.0f, 13.0f});
    CHECK(rig.part("envelope-attack", "display").getTitle() == "Op 3 attack");
    CHECK(rig.sent.empty()); // selecting is not an edit

    // A card made again for the same insert opens on the same operator.
    rig.make();
    CHECK(rig.part("envelope-attack", "display").getTitle() == "Op 3 attack");
}

TEST_CASE("Clicking one of Cirrus's algorithm buttons sets it", "[ui][cirrus]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto& buttons = rig.part("algorithms");
    // The second button: algorithm 2.
    click(buttons, {22.0f + 4.0f + 11.0f, 11.0f});
    REQUIRE(rig.sent.size() == 1);
    CHECK(rig.sent[0].first == cr::algorithm);
    CHECK(rig.sent[0].second == 1.0f);
    // The chosen one again sends nothing.
    rig.sent.clear();
    click(buttons, {22.0f + 4.0f + 11.0f, 11.0f});
    CHECK(rig.sent.empty());
}

TEST_CASE("Fixed on a Cirrus operator turns its ratio cell to the frequency and disables Fine",
          "[ui][cirrus]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    CHECK(rig.part("op1-ratio", "operator1").getTitle() == "Op 1 ratio");
    CHECK(rig.part("op1-fine", "operator1").isEnabled());
    rig.modelChange(cr::opParam(0, cr::fixed), 1.0f);
    CHECK(rig.part("op1-ratio", "operator1").getTitle() == "Op 1 frequency");
    CHECK(!rig.part("op1-fine", "operator1").isEnabled());
    // Glide only works in Mono.
    CHECK(!rig.knob(cr::glide).isEnabled());
    rig.modelChange(cr::voiceMode, 1.0f);
    CHECK(rig.knob(cr::glide).isEnabled());
}

TEST_CASE("Cirrus's face follows a note as it plays", "[ui][cirrus]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    std::vector<float> l(256), r(256);
    for (int b = 0; b < 40; ++b)
    {
        engine::MidiEventList events(8);
        if (b == 0)
            events.noteOn(0, 67, 110);
        rig.live->processMidi(events, l.data(), r.data(), 256);
        rig.card->tick(16.0);
    }
    const auto* activity = cr::activity(*rig.live);
    REQUIRE(activity != nullptr);
    CHECK(activity->note.load() == 67);
    CHECK(activity->level[0].load() > 0.0f);
    CHECK(rig.card->createComponentSnapshot(rig.card->getLocalBounds(), true, 2.0f).isValid());
}
