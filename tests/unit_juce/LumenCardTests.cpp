#include "core/Command.h"
#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "plugins/Lumen.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/HardwareLever.h"
#include "ui/strip/LumenFace.h"
#include "ui/strip/StripStyle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <vector>

using namespace northlight;
namespace lu = plugins::lumen;
namespace lf = ui::strip::lumenface;

namespace
{
struct Rig
{
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(lu::type);
    core::Insert insert;
    std::vector<std::unique_ptr<core::Command>> sent;
    ui::strip::CardHost host;
    std::unique_ptr<ui::strip::ModuleCard> card;

    Rig()
    {
        live->prepare(48000.0, 240);
        insert.id = 31;
        insert.type = lu::type;
        insert.bypass = false;
        insert.params = plugins::defaultParameters(insert.type);
        host.liveInsert = [this](core::Id) { return live; };
        host.onCommand = [this](std::unique_ptr<core::Command> c) { sent.push_back(std::move(c)); };
        card = ui::strip::createBuiltInCard(ui::Theme::dark(), host, core::masterId, insert);
        REQUIRE(card != nullptr);
        card->setBounds(
            0, 0, card->preferredWidth(), ui::strip::ModuleCard::headerHeight + card->preferredBodyHeight());
        card->tick(16.0);
    }

    juce::Component* control(const juce::String& title) const
    {
        std::function<juce::Component*(juce::Component&)> find = [&](juce::Component& c) -> juce::Component*
        {
            for (auto* child : c.getChildren())
            {
                if (child->getTitle() == title)
                    return child;
                if (auto* found = find(*child))
                    return found;
            }
            return nullptr;
        };
        return find(*card);
    }

    juce::Component* face() const
    {
        for (auto* child : card->getChildren())
            if (child->isVisible() && child->getWidth() == static_cast<int>(lf::width))
                return child;
        return nullptr;
    }
};
} // namespace

TEST_CASE("Lumen's face fits its body by scaling, the card narrowing with it", "[ui][lumen]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto* face = rig.face();
    REQUIRE(face != nullptr);
    CHECK(face->getTransform().mat00 == Catch::Approx(1.0f));
    const auto& theme = ui::Theme::dark();
    const int body = 300 + 2 * theme.spacing.xs;
    const int width = rig.card->preferredWidthAt(body);
    CHECK(width == 580 + 2 * theme.spacing.sm);
    rig.card->setEmbedded(true);
    rig.card->setBounds(0, 0, width, body);
    CHECK(face->getTransform().mat00 == Catch::Approx(0.5f));
    CHECK(ui::strip::moduleColour(theme, lu::type) != theme.colours.textSecondary);
}

TEST_CASE("Every Lumen parameter has a control on its face", "[ui][lumen]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    for (const auto& info : lu::parameterInfo())
    {
        INFO(info.name);
        const auto title = juce::String(info.name);
        CHECK((rig.control(title) != nullptr || title == "Tremolo"));
    }
    CHECK(rig.control("Vibrato") != nullptr); // the tremolo's lever
}

TEST_CASE("Lumen's knobs, lever and sync set the piano", "[ui][lumen]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto* bark = dynamic_cast<ui::Knob*>(rig.control("Bark"));
    REQUIRE(bark != nullptr);
    CHECK(bark->accessibleValueText() == "40%");
    bark->keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
    CHECK(rig.card->param(lu::bark) == 100.0f);
    CHECK_FALSE(rig.sent.empty());

    // The lever: PAN up (the default), OFF in the middle, AMP down.
    auto* lever = dynamic_cast<ui::strip::HardwareLever*>(rig.control("Vibrato"));
    REQUIRE(lever != nullptr);
    CHECK(lever->position() == 0);
    lever->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    CHECK(rig.card->param(lu::tremoloMode) == 0.0f);
    lever->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    CHECK(rig.card->param(lu::tremoloMode) == 2.0f);
    // A model change moves it without a command.
    const auto commands = rig.sent.size();
    rig.insert.params[lu::tremoloMode] = 0.0f;
    rig.card->refresh(rig.insert);
    CHECK(lever->position() == 1);
    CHECK(rig.sent.size() == commands);

    // SYNC steps through note values; the rate then shows the note value.
    auto* sync = dynamic_cast<ui::Knob*>(rig.control("Tremolo sync"));
    auto* rate = dynamic_cast<ui::Knob*>(rig.control("Tremolo rate"));
    REQUIRE(sync != nullptr);
    REQUIRE(rate != nullptr);
    CHECK(sync->accessibleValueText() == "Free");
    CHECK(rate->accessibleValueText() == "4.5 Hz");
    rig.insert.params[lu::tremoloSync] = 11.0f; // 1/4
    rig.card->refresh(rig.insert);
    CHECK(sync->accessibleValueText() == "1/4");
    CHECK(rate->accessibleValueText() == "1/4");

    auto* voices = dynamic_cast<ui::Knob*>(rig.control("Voices"));
    REQUIRE(voices != nullptr);
    CHECK(voices->accessibleValueText() == "32");
}

TEST_CASE("Lumen's tines blend the two frames nearest their swing, still at rest", "[ui][lumen]")
{
    CHECK(lf::atRest(0.0f));
    CHECK_FALSE(lf::atRest(0.5f));
    const auto down = lf::framesFor(-1.0f);
    CHECK(down.first == 0);
    CHECK(down.blend == 0.0f);
    const auto up = lf::framesFor(1.0f);
    CHECK(up.first == lf::tineFrames - 1);
    const auto rest = lf::framesFor(0.0f);
    CHECK(rest.first == lf::tineFrames / 2);
    CHECK(rest.blend == Catch::Approx(0.0f));
    const auto between = lf::framesFor(0.125f); // half a frame up from rest
    CHECK(between.first == lf::tineFrames / 2);
    CHECK(between.second == lf::tineFrames / 2 + 1);
    CHECK(between.blend == Catch::Approx(0.5f));
    CHECK(lf::swingFor(0.0f, 1.0f) == 0.0f);
    CHECK(lf::swingFor(1.0f, 3.14159265f / 2.0f) == Catch::Approx(1.0f));
    // The lever's positions against the tremolo's modes, both ways.
    for (int mode = 0; mode < 3; ++mode)
        CHECK(lf::leverModes[static_cast<size_t>(lf::leverPositionFor(mode))] == mode);
}
