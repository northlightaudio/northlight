#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "plugins/Widener.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/components/SegmentedControl.h"
#include "ui/strip/BuiltInCards.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace northlight;
namespace wd = plugins::widener;

namespace
{
template <typename T> T* findChild(juce::Component& parent, const juce::String& title = {})
{
    for (auto* child : parent.getChildren())
        if (auto* c = dynamic_cast<T*>(child); c != nullptr && (title.isEmpty() || c->getTitle() == title))
            return c;
    return nullptr;
}
} // namespace

TEST_CASE("The widener card watches its processor's image and guard, and its controls set the parameters",
          "[ui][widener]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto& theme = ui::Theme::dark();
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(wd::type);
    live->prepare(48000.0, 240);
    live->setBypassed(false);
    core::Insert insert;
    insert.id = 12;
    insert.type = wd::type;
    insert.params = plugins::defaultParameters(insert.type);
    insert.params[wd::midWidth] = 200.0f;
    insert.params[wd::highWidth] = 200.0f;
    insert.params[wd::guard] = 1.0f;
    for (size_t i = 0; i < insert.params.size(); ++i)
        live->setParameter(static_cast<int>(i), insert.params[i]);
    std::vector<std::unique_ptr<core::Command>> sent;
    ui::strip::CardHost host;
    host.liveInsert = [&](core::Id) { return live; };
    host.onCommand = [&](std::unique_ptr<core::Command> c) { sent.push_back(std::move(c)); };
    auto card = ui::strip::createBuiltInCard(theme, host, core::masterId, insert);
    REQUIRE(card != nullptr);
    card->setBounds(
        0, 0, card->preferredWidth(), card->preferredBodyHeight() + ui::strip::ModuleCard::headerHeight);
    card->tick(16.0); // starts watching

    // Wide, uncorrelated noise made wider still: the guard has to narrow it.
    std::uint32_t a = 1u, b = 2u;
    const auto noise = [](std::uint32_t& s)
    {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / static_cast<float>(1u << 23) - 1.0f;
    };
    std::vector<float> l(240), r(240);
    for (int block = 0; block < 400; ++block) // two seconds of 5 ms blocks
    {
        for (size_t i = 0; i < l.size(); ++i)
        {
            l[i] = 0.3f * noise(a);
            r[i] = 0.3f * noise(b);
        }
        live->process(l.data(), r.data(), 240);
        if (block % 4 == 0)
            card->tick(20.0);
    }
    // The card asked for the taps (the scope and the width spectrum read them)
    // and follows the guard's narrowing through the frames.
    CHECK(live->feedback()->tapsEnabled());
    CHECK(card->reductionDb() > 10.0f);
    const auto image = card->createComponentSnapshot(card->getLocalBounds(), true, 1.0f);
    CHECK(image.isValid());
    const auto doubled = card->createComponentSnapshot(card->getLocalBounds(), true, 2.0f);
    CHECK(doubled.isValid());

    // Haas time only counts in Haas mode: dimmed in Diffuse.
    auto* haasTime = findChild<ui::Knob>(*card, "Haas time");
    REQUIRE(haasTime != nullptr);
    CHECK_FALSE(haasTime->isEnabled());
    auto* mode = findChild<ui::SegmentedControl>(*card, "Spread mode");
    REQUIRE(mode != nullptr);
    CHECK(mode->selected() == wd::diffuse);
    mode->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    CHECK(card->param(wd::spreadMode) == static_cast<float>(wd::haas));
    CHECK(haasTime->isEnabled());
    CHECK_FALSE(sent.empty());

    // A model change moves the controls without a command (first the model takes
    // the edit, as the command would, then something else, such as an undo).
    const auto commands = sent.size();
    insert.params[wd::spreadMode] = static_cast<float>(wd::haas);
    card->refresh(insert);
    insert.params[wd::spreadMode] = static_cast<float>(wd::diffuse);
    insert.params[wd::lowWidth] = 0.0f;
    card->refresh(insert);
    CHECK(mode->selected() == wd::diffuse);
    CHECK_FALSE(haasTime->isEnabled());
    auto* low = findChild<ui::Knob>(*card, "Low width");
    REQUIRE(low != nullptr);
    CHECK(low->value() == 0.0);
    CHECK(low->format(0.0) == "Mono");
    CHECK(sent.size() == commands);

    // Hidden, the card lets the processor stop tapping.
    card->stopWatching();
    CHECK_FALSE(live->feedback()->tapsEnabled());
}
