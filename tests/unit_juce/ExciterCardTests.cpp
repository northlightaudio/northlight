#include "core/Command.h"
#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "plugins/Exciter.h"
#include "ui/Theme.h"
#include "ui/components/RotarySelector.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/StripStyle.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <numbers>
#include <typeinfo>
#include <vector>

using namespace northlight;
namespace ex = plugins::exciter;

namespace
{
juce::MouseEvent mouseAt(juce::Component& c, float x, float y, int clicks = 1, bool dragged = false)
{
    const juce::Point<float> at(x, y);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
                            at,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                            1.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            0.0f,
                            &c,
                            &c,
                            juce::Time::getCurrentTime(),
                            at,
                            juce::Time::getCurrentTime(),
                            clicks,
                            dragged);
}

struct CardFixture
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(ex::type);
    core::Insert insert;
    std::vector<std::unique_ptr<core::Command>> sent;
    ui::strip::CardHost host;
    std::unique_ptr<ui::strip::ModuleCard> card;

    CardFixture()
    {
        live->prepare(48000.0, 240);
        live->setBypassed(false);
        insert.id = 12;
        insert.type = ex::type;
        insert.params = plugins::defaultParameters(insert.type);
        for (size_t i = 0; i < insert.params.size(); ++i)
            live->setParameter(static_cast<int>(i), insert.params[i]);
        host.liveInsert = [this](core::Id) { return live; };
        host.onCommand = [this](std::unique_ptr<core::Command> c) { sent.push_back(std::move(c)); };
        card = ui::strip::createBuiltInCard(ui::Theme::dark(), host, core::masterId, insert);
        REQUIRE(card != nullptr);
        card->setBounds(
            0, 0, card->preferredWidth(), ui::strip::ModuleCard::headerHeight + card->preferredBodyHeight());
    }

    // Two seconds of a bright chord through the live processor, the card ticking.
    void play()
    {
        std::vector<float> l(240), r(240);
        double phase = 0.0;
        for (int b = 0; b < 400; ++b)
        {
            for (size_t i = 0; i < l.size(); ++i, phase += 1.0 / 48000.0)
            {
                const float v = 0.2f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 4000.0 * phase) +
                                                          std::sin(2.0 * std::numbers::pi * 220.0 * phase));
                l[i] = r[i] = v;
            }
            live->process(l.data(), r.data(), 240);
            if (b % 4 == 0)
                card->tick(20.0);
        }
    }

    juce::Component* child(const juce::String& name) const
    {
        for (auto* c : card->getChildren())
            if (c->getName() == name)
                return c;
        return nullptr;
    }
};
} // namespace

TEST_CASE("The exciter card shows what is added and fits its controls", "[ui][exciter]")
{
    CardFixture f;
    CHECK(ui::strip::moduleColour(ui::Theme::dark(), ex::type) ==
          ui::strip::paletteColour(ui::Theme::dark(), "Machair"));
    f.card->tick(16.0); // starts watching
    f.play();
    // The spectrum asked for the taps, and the processor measured what it adds.
    CHECK(f.live->feedback()->tapsEnabled());
    const auto frame = f.live->feedback()->latest();
    CHECK(frame.extra2 > 0.0f);
    CHECK(frame.extra2 < frame.extra);
    const auto image = f.card->createComponentSnapshot(f.card->getLocalBounds(), true, 1.0f);
    CHECK(image.isValid());

    // Every control lies inside the card, and none overlaps another.
    std::vector<juce::Rectangle<int>> placed;
    for (auto* c : f.card->getChildren())
    {
        if (!c->isVisible() || c->getBounds().isEmpty())
            continue;
        INFO(typeid(*c).name() << " " << c->getBounds().toString() << " in "
                               << f.card->getLocalBounds().toString());
        CHECK(f.card->getLocalBounds().contains(c->getBounds()));
        if (dynamic_cast<ui::Knob*>(c) != nullptr || dynamic_cast<ui::RotarySelector*>(c) != nullptr)
        {
            for (const auto& other : placed)
                CHECK_FALSE(other.intersects(c->getBounds()));
            placed.push_back(c->getBounds());
        }
    }
    CHECK(placed.size() == 12); // eleven knobs and the character selector
}

TEST_CASE("The exciter card's character selector and spectrum handles set their parameters", "[ui][exciter]")
{
    CardFixture f;
    f.card->tick(16.0);
    ui::RotarySelector* selector = nullptr;
    for (auto* c : f.card->getChildren())
        if (auto* s = dynamic_cast<ui::RotarySelector*>(c))
            selector = s;
    REQUIRE(selector != nullptr);
    CHECK(selector->selected() == ex::tube);
    selector->keyPressed(juce::KeyPress(juce::KeyPress::endKey));
    CHECK(f.card->param(ex::character) == static_cast<float>(ex::crisp));
    CHECK_FALSE(f.sent.empty());
    // A model change moves the selector without a command.
    const auto commands = f.sent.size();
    f.insert.params[ex::character] = static_cast<float>(ex::air);
    f.card->refresh(f.insert);
    CHECK(selector->selected() == ex::air);
    CHECK(f.sent.size() == commands);

    // Dragging the crossover's handle moves the frequency along the log axis.
    auto* spectrum = f.child("Harmonic spectrum");
    REQUIRE(spectrum != nullptr);
    const auto plot = spectrum->getLocalBounds().toFloat().reduced(1.0f).withTrimmedBottom(14.0f);
    const float from = ui::strip::hzToX(f.card->param(ex::frequency), plot);
    const float to = ui::strip::hzToX(8000.0, plot);
    spectrum->mouseDown(mouseAt(*spectrum, from, plot.getCentreY()));
    spectrum->mouseDrag(mouseAt(*spectrum, to, plot.getCentreY(), 1, true));
    spectrum->mouseUp(mouseAt(*spectrum, to, plot.getCentreY(), 1, true));
    CHECK(std::abs(f.card->param(ex::frequency) - 8000.0f) < 200.0f);
    // A press near the bass handle takes the bass frequency instead.
    const float bassX = ui::strip::hzToX(f.card->param(ex::bassFrequency), plot);
    const float bassTo = ui::strip::hzToX(150.0, plot);
    spectrum->mouseDown(mouseAt(*spectrum, bassX + 2.0f, plot.getCentreY()));
    spectrum->mouseDrag(mouseAt(*spectrum, bassTo, plot.getCentreY(), 1, true));
    spectrum->mouseUp(mouseAt(*spectrum, bassTo, plot.getCentreY(), 1, true));
    CHECK(std::abs(f.card->param(ex::bassFrequency) - 150.0f) < 5.0f);
    CHECK(std::abs(f.card->param(ex::frequency) - 8000.0f) < 200.0f);
    // Double-click resets the nearer one.
    spectrum->mouseDoubleClick(mouseAt(*spectrum, ui::strip::hzToX(8000.0, plot), plot.getCentreY(), 2));
    CHECK(f.card->param(ex::frequency) == f.card->info(ex::frequency).defaultValue);
}
