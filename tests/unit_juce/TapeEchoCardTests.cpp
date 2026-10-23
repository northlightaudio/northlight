#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "plugins/TapeEcho.h"
#include "ui/Theme.h"
#include "ui/components/RotarySelector.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/StripStyle.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace northlight;
namespace te = plugins::tapeecho;

namespace
{
struct Rig
{
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(te::type);
    core::Insert insert;
    std::vector<std::unique_ptr<core::Command>> sent;
    ui::strip::CardHost host;
    std::unique_ptr<ui::strip::ModuleCard> card;

    Rig()
    {
        live->prepare(48000.0, 240);
        live->setBypassed(false);
        insert.id = 12;
        insert.type = te::type;
        insert.bypass = false;
        insert.params = plugins::defaultParameters(insert.type);
        for (size_t i = 0; i < insert.params.size(); ++i)
            live->setParameter(static_cast<int>(i), insert.params[i]);
        host.liveInsert = [this](core::Id) { return live; };
        host.onCommand = [this](std::unique_ptr<core::Command> c) { sent.push_back(std::move(c)); };
        card = ui::strip::createBuiltInCard(ui::Theme::dark(), host, core::masterId, insert);
        REQUIRE(card != nullptr);
        card->setBounds(
            0, 0, card->preferredWidth(), ui::strip::ModuleCard::headerHeight + card->preferredBodyHeight());
        card->tick(16.0); // starts watching
    }

    std::vector<ui::RotarySelector*> selectors() const
    {
        std::vector<ui::RotarySelector*> found;
        for (auto* child : card->getChildren())
            if (auto* s = dynamic_cast<ui::RotarySelector*>(child))
                found.push_back(s);
        return found;
    }
};
} // namespace

TEST_CASE("The tape echo card reads its machine and its selectors set heads and stereo", "[ui][tapeecho]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    CHECK(ui::strip::moduleColour(ui::Theme::dark(), te::type) !=
          ui::Theme::dark().colours.textSecondary); // it has its own hue

    // Hits through the machine: the frames carry what the record head wrote and the motor.
    std::vector<float> l(240), r(240);
    for (int b = 0; b < 200; ++b)
    {
        const float level = b % 40 < 2 ? 0.8f : 0.0f;
        std::fill(l.begin(), l.end(), level);
        std::fill(r.begin(), r.end(), level);
        rig.live->process(l.data(), r.data(), 240);
        if (b % 4 == 0)
            rig.card->tick(20.0);
    }
    const auto& last = rig.card->historyAt(rig.card->historySize() - 1);
    CHECK(last.extra2 > 0.17f); // the motor at the default 180 ms
    CHECK(last.extra2 < 0.19f);
    const auto image = rig.card->createComponentSnapshot(rig.card->getLocalBounds(), true, 1.0f);
    CHECK(image.isValid());

    // Heads (eight combinations) and Stereo (three) are rotary selectors, in that order.
    const auto selectors = rig.selectors();
    REQUIRE(selectors.size() == 2);
    CHECK(selectors[0]->selected() == static_cast<int>(rig.insert.params[te::heads]));
    CHECK(selectors[0]->getTooltip() == "Heads: Heads 1 + 3");
    selectors[0]->keyPressed(juce::KeyPress(juce::KeyPress::endKey));
    CHECK(rig.card->param(te::heads) == static_cast<float>(te::headModes.size() - 1));
    CHECK_FALSE(rig.sent.empty());
    selectors[1]->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    CHECK(rig.card->param(te::stereo) == static_cast<float>(te::stereoPingPong));

    // A model change moves the selector without a command.
    const auto commands = rig.sent.size();
    rig.insert.params[te::heads] = 2.0f;
    rig.card->refresh(rig.insert);
    CHECK(selectors[0]->selected() == 2);
    CHECK(rig.sent.size() == commands);
}

// Renders the card in a few states to PNGs at 100% and 200%, for review (not run
// by default): set NORTHLIGHT_CARD_PNGS to a folder.
TEST_CASE("Tape echo card states to PNG", "[.capture]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto folder = juce::SystemStats::getEnvironmentVariable("NORTHLIGHT_CARD_PNGS", {});
    REQUIRE(folder.isNotEmpty());
    struct State
    {
        const char* name;
        std::vector<std::pair<int, float>> params;
        bool bypass = false;
    };
    const std::vector<State> states{
        {"default", {}},
        {"selfosc", {{te::feedback, 115.0f}, {te::heads, 7.0f}, {te::bass, 4.0f}, {te::age, 60.0f}}},
        {"warning", {{te::feedback, 92.0f}, {te::heads, 0.0f}, {te::wow, 100.0f}}},
        {"synced", {{te::sync, 9.0f}, {te::stereo, 1.0f}, {te::heads, 6.0f}, {te::drive, 12.0f}}},
        {"clamped", {{te::sync, 16.0f}, {te::feedback, 0.0f}, {te::killDry, 1.0f}, {te::spring, 50.0f}}},
        {"off", {}, true},
    };
    for (const auto& state : states)
    {
        Rig rig;
        for (const auto& [index, value] : state.params)
        {
            rig.insert.params[static_cast<size_t>(index)] = value;
            rig.live->setParameter(index, value);
        }
        rig.insert.bypass = state.bypass;
        rig.card->refresh(rig.insert);
        std::vector<float> l(240), r(240);
        for (int b = 0; b < 600; ++b) // three seconds: hits every 200 ms and a chord
        {
            for (int i = 0; i < 240; ++i)
            {
                const float t = static_cast<float>(b * 240 + i) / 48000.0f;
                const float hit = b % 40 < 3 ? 0.7f * std::exp(-static_cast<float>(b % 40) * 0.8f) : 0.0f;
                l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
                    hit * std::sin(6.2831853f * 180.0f * t) +
                    (b > 300 && b < 420 ? 0.2f : 0.0f) * std::sin(6.2831853f * 330.0f * t);
            }
            rig.live->process(l.data(), r.data(), 240);
            rig.card->tick(5.0);
        }
        for (const float scale : {1.0f, 2.0f})
        {
            const auto image = rig.card->createComponentSnapshot(rig.card->getLocalBounds(), true, scale);
            juce::File file(folder + "/card-" + state.name + (scale > 1.5f ? "-2x" : "-1x") + ".png");
            file.deleteFile();
            juce::FileOutputStream out(file);
            REQUIRE(out.openedOk());
            juce::PNGImageFormat().writeImageToStream(image, out);
        }
    }
}

TEST_CASE("The tape echo card's controls fit inside it, every one with a tooltip", "[ui][tapeecho]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    const auto bounds = rig.card->getLocalBounds();
    for (auto* child : rig.card->getChildren())
    {
        if (!child->isVisible())
            continue;
        CHECK(bounds.contains(child->getBounds()));
        if (dynamic_cast<juce::Label*>(child) != nullptr)
            continue;
        if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(child))
            CHECK(tip->getTooltip().isNotEmpty());
    }
}
