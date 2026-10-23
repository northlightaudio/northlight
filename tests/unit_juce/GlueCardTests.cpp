#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "plugins/Glue.h"
#include "ui/Theme.h"
#include "ui/components/RotarySelector.h"
#include "ui/strip/BuiltInCards.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace northlight;
namespace glue = plugins::glue;

TEST_CASE("A rotary selector steps by key, returns to its default and reports user changes only",
          "[ui][selector]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    ui::RotarySelector selector(ui::Theme::dark());
    selector.setPositions({"1.5", "2", "4", "10"}, {"1.5:1", "2:1", "4:1", "10:1"}, "Ratio");
    selector.setDefault(1);
    selector.setBounds(0, 0, selector.preferredWidth(), selector.preferredHeight());
    std::vector<int> changes;
    selector.onChange = [&](int index) { changes.push_back(index); };

    selector.setSelected(1, juce::dontSendNotification); // from the model: no report
    CHECK(changes.empty());
    CHECK(selector.getTooltip() == "Ratio: 2:1");

    CHECK(selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(selector.selected() == 2);
    CHECK(selector.keyPressed(juce::KeyPress(juce::KeyPress::endKey)));
    CHECK(selector.selected() == 3);
    CHECK(selector.keyPressed(juce::KeyPress(juce::KeyPress::rightKey))); // at the end: stays
    CHECK(selector.selected() == 3);
    CHECK(selector.keyPressed(juce::KeyPress(juce::KeyPress::homeKey)));
    CHECK(changes == std::vector<int>{2, 3, 0});
    CHECK(selector.getTooltip() == "Ratio: 1.5:1");

    // The labels sit around the top, mirrored about the middle, inside the bounds.
    const auto first = selector.labelCentre(0), last = selector.labelCentre(3);
    CHECK(std::abs((first.x + last.x) * 0.5f - selector.getWidth() * 0.5f) < 0.5f);
    CHECK(std::abs(first.y - last.y) < 0.5f);
    for (int i = 0; i < 4; ++i)
        CHECK(selector.getLocalBounds().toFloat().reduced(4.0f).contains(selector.labelCentre(i)));
}

TEST_CASE("The glue card reads its processor and its selectors set the stepped parameters", "[ui][glue]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(glue::type);
    live->prepare(48000.0, 240);
    live->setBypassed(false);
    core::Insert insert;
    insert.id = 11;
    insert.type = glue::type;
    insert.params = plugins::defaultParameters(insert.type);
    for (size_t i = 0; i < insert.params.size(); ++i)
        live->setParameter(static_cast<int>(i), insert.params[i]);
    std::vector<std::unique_ptr<core::Command>> sent;
    ui::strip::CardHost host;
    host.liveInsert = [&](core::Id) { return live; };
    host.onCommand = [&](std::unique_ptr<core::Command> c) { sent.push_back(std::move(c)); };
    auto card = ui::strip::createBuiltInCard(theme, host, core::masterId, insert);
    REQUIRE(card != nullptr);
    card->setBounds(0, 0, card->preferredWidth(), 300);
    card->tick(16.0); // starts watching

    // Loud hits over a quieter bed: the compressor works and the card shows it.
    std::vector<float> l(240), r(240);
    for (int b = 0; b < 400; ++b) // two seconds of 5 ms blocks
    {
        const float level = b % 40 < 4 ? 0.9f : 0.2f;
        std::fill(l.begin(), l.end(), level);
        std::fill(r.begin(), r.end(), level);
        live->process(l.data(), r.data(), 240);
        if (b % 4 == 0)
            card->tick(20.0);
    }
    CHECK(card->reductionDb() > 1.0f);
    const auto image = card->createComponentSnapshot(card->getLocalBounds(), true, 1.0f);
    CHECK(image.isValid());

    // The three stepped controls are rotary selectors, in parameter order.
    std::vector<ui::RotarySelector*> selectors;
    for (auto* child : card->getChildren())
        if (auto* s = dynamic_cast<ui::RotarySelector*>(child))
            selectors.push_back(s);
    REQUIRE(selectors.size() == 3);
    CHECK(selectors[0]->selected() == static_cast<int>(insert.params[glue::ratio]));
    CHECK(selectors[2]->selected() == glue::autoRelease);
    // Stepping the ratio sets the parameter (and sends the command).
    selectors[0]->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    CHECK(card->param(glue::ratio) == insert.params[glue::ratio] + 1.0f);
    CHECK_FALSE(sent.empty());
    // A model change moves the selector without a command.
    const auto commands = sent.size();
    insert.params[glue::attack] = 0.0f;
    card->refresh(insert);
    CHECK(selectors[1]->selected() == 0);
    CHECK(sent.size() == commands);
}
