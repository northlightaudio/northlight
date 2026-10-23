#include "core/Model.h"
#include "plugins/Bloom.h"
#include "plugins/BuiltIns.h"
#include "ui/Theme.h"
#include "ui/components/RotarySelector.h"
#include "ui/strip/BuiltInCards.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace northlight;
namespace bloom = plugins::bloom;

TEST_CASE("Bloom's card reads its processor, latches Freeze and steps the pitch", "[ui][bloom]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(bloom::type);
    live->prepare(48000.0, 240);
    live->setBypassed(false);
    core::Insert insert;
    insert.id = 12;
    insert.type = bloom::type;
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
    // Paint once so the tail field sizes its spectrogram, then start watching.
    CHECK(card->createComponentSnapshot(card->getLocalBounds(), true, 1.0f).isValid());
    card->tick(16.0);

    // Hits then silence: the tail rings on after the input stops.
    std::vector<float> l(240), r(240);
    for (int b = 0; b < 400; ++b) // two seconds of 5 ms blocks
    {
        const float level = b < 100 && b % 20 < 6 ? 0.8f : 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = level * ((i / 55) % 2 == 0 ? 1.0f : -1.0f); // a 436 Hz square
        live->process(l.data(), r.data(), 240);
        if (b % 4 == 0)
            card->tick(20.0);
    }
    CHECK(card->historySize() > 100);
    // The newest frames carry the tail's level after the dry input has stopped.
    const auto& newest = card->historyAt(card->historySize() - 1);
    CHECK(newest.extra > 0.001f);
    CHECK(newest.extra2 < 0.001f);
    const auto image = card->createComponentSnapshot(card->getLocalBounds(), true, 2.0f);
    CHECK(image.isValid());

    // The tail field's spectrogram has filled across its width as the tail
    // played (two seconds of a five second field: the right two fifths).
    juce::Component* field = nullptr;
    for (auto* child : card->getChildren())
        if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(child);
            tip != nullptr && tip->getTooltip().startsWith("The tail over"))
            field = child;
    REQUIRE(field != nullptr);
    const auto shot = field->createComponentSnapshot(field->getLocalBounds(), true, 1.0f);
    const auto lit = [&](int x)
    {
        int n = 0;
        for (int y = 4; y < shot.getHeight() / 2; ++y)
            n += shot.getPixelAt(x, y).getBrightness() > 0.2f ? 1 : 0;
        return n;
    };
    CHECK(lit(shot.getWidth() * 7 / 10) > 0);
    CHECK(lit(shot.getWidth() * 9 / 10) > 0);

    // Freeze is a latch: a click sets the parameter and sends the command.
    juce::Button* freeze = nullptr;
    for (auto* child : card->getChildren())
        if (auto* b = dynamic_cast<juce::Button*>(child); b != nullptr && b->getName() == "Freeze")
            freeze = b;
    REQUIRE(freeze != nullptr);
    REQUIRE(freeze->onClick);
    const auto before = sent.size();
    freeze->onClick();
    CHECK(card->param(bloom::freeze) == 1.0f);
    CHECK(freeze->getToggleState());
    CHECK(sent.size() > before);
    // A model change releases it without a command.
    insert.params[bloom::freeze] = 1.0f; // the command applied
    card->refresh(insert);
    const auto commands = sent.size();
    insert.params[bloom::freeze] = 0.0f;
    card->refresh(insert);
    CHECK_FALSE(freeze->getToggleState());
    CHECK(sent.size() == commands);

    // The one stepped control is the pitch selector, at +12 by default.
    std::vector<ui::RotarySelector*> selectors;
    for (auto* child : card->getChildren())
        if (auto* s = dynamic_cast<ui::RotarySelector*>(child))
            selectors.push_back(s);
    REQUIRE(selectors.size() == 1);
    CHECK(selectors[0]->selected() == 3);
    selectors[0]->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    CHECK(card->param(bloom::pitch) == 4.0f);

    // Nothing is laid out past the card's edges.
    for (auto* child : card->getChildren())
    {
        if (!child->isVisible() || child->getWidth() == 0)
            continue;
        CHECK(card->getLocalBounds().contains(child->getBounds()));
    }
}
