#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "ui/Theme.h"
#include "ui/strip/BuiltInCards.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <vector>

using namespace northlight;

TEST_CASE("The effect cards' tooltips are a few words at most", "[ui][tooltips]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    plugins::BuiltInFactory factory;
    ui::strip::CardHost host;
    host.liveInsert = [](core::Id) { return std::shared_ptr<engine::InsertProcessor>(); };
    host.onCommand = [](std::unique_ptr<core::Command>) {};
    for (const auto& type : factory.typeNames())
    {
        if (plugins::BuiltInFactory::isInstrument(type))
            continue;
        INFO(type);
        core::Insert insert;
        insert.id = 7;
        insert.type = type;
        insert.params = plugins::defaultParameters(type);
        auto card = ui::strip::createBuiltInCard(theme, host, core::masterId, insert);
        // Effects still on the generic editor (no card yet) have no tooltips of their own.
        if (type == "northlight.vesper")
        {
            CHECK(card == nullptr);
            continue;
        }
        REQUIRE(card != nullptr);
        card->setBounds(0, 0, card->preferredWidth(), card->preferredBodyHeight() + 40);
        // Every part of the card, all the way down: a short hint or nothing.
        std::function<void(juce::Component&)> visit = [&](juce::Component& c)
        {
            if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(&c))
            {
                INFO(tip->getTooltip());
                CHECK(tip->getTooltip().length() <= 28);
            }
            for (auto* child : c.getChildren())
                visit(*child);
        };
        visit(*card);
    }
}
