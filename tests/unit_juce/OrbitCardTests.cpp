#include "core/Command.h"
#include "core/Model.h"
#include "plugins/BuiltIns.h"
#include "plugins/Orbit.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/HardwareKey.h"
#include "ui/strip/OrbitFace.h"
#include "ui/strip/StripStyle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

using namespace northlight;
namespace ob = plugins::orbit;
namespace of = ui::strip::orbitface;

namespace
{
struct Rig
{
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(ob::type);
    core::Insert insert;
    std::vector<std::unique_ptr<core::Command>> sent;
    ui::strip::CardHost host;
    std::unique_ptr<ui::strip::ModuleCard> card;

    Rig()
    {
        live->prepare(48000.0, 240);
        live->setBypassed(false);
        insert.id = 12;
        insert.type = ob::type;
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

    ui::strip::HardwareKey* key(const juce::String& title) const
    {
        return dynamic_cast<ui::strip::HardwareKey*>(control(title));
    }

    juce::Component* face() const
    {
        for (auto* child : card->getChildren())
            if (child->isVisible() && child->getWidth() == static_cast<int>(of::width))
                return child;
        return nullptr;
    }

    // A tone through the machine for `blocks` blocks, the song playing, the card watching.
    void play(int blocks, float amplitude, bool playing = true)
    {
        engine::PlayHead head;
        head.playing = playing;
        std::vector<float> l(240), r(240);
        for (int b = 0; b < blocks; ++b)
        {
            head.samples = static_cast<std::int64_t>(position);
            live->setPlayHead(head);
            for (int i = 0; i < 240; ++i)
                l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] =
                    amplitude * std::sin(6.2831853f * 440.0f * static_cast<float>(position + i) / 48000.0f);
            live->process(l.data(), r.data(), 240);
            if (playing)
                position += 240;
            card->tick(5.0);
        }
    }
    double position = 0.0;
};
} // namespace

TEST_CASE("Orbit's face fits its body by scaling, the card narrowing with it", "[ui][orbit]")
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
    CHECK(ui::strip::moduleColour(theme, ob::type) != theme.colours.textSecondary);
}

TEST_CASE("Orbit's knobs and keys set the machine, the keys lit by what is chosen", "[ui][orbit]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto* input = dynamic_cast<ui::Knob*>(rig.control("Input"));
    REQUIRE(input != nullptr);
    CHECK(input->accessibleValueText() == juce::String::fromUTF8("+0.0 dB"));
    input->keyPressed(juce::KeyPress(juce::KeyPress::homeKey));
    CHECK(rig.card->param(ob::input) == 18.0f);
    CHECK_FALSE(rig.sent.empty());
    auto* bias = dynamic_cast<ui::Knob*>(rig.control("Bias"));
    REQUIRE(bias != nullptr);
    CHECK(bias->accessibleValueText() == "Cal");
    auto* hiss = dynamic_cast<ui::Knob*>(rig.control("Hiss"));
    REQUIRE(hiss != nullptr);
    CHECK(hiss->accessibleValueText() == "Off");

    // The keys: one lit per row, a press chooses.
    auto* ips15 = rig.key("Speed: 15 ips");
    auto* ips30 = rig.key("Speed: 30 ips");
    REQUIRE(ips15 != nullptr);
    REQUIRE(ips30 != nullptr);
    CHECK(ips15->lit());
    CHECK_FALSE(ips30->lit());
    ips30->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    CHECK(rig.card->param(ob::speed) == static_cast<float>(ob::ips30));
    CHECK(ips30->lit());
    CHECK_FALSE(ips15->lit());

    auto* ccir = rig.key("EQ: CCIR");
    REQUIRE(ccir != nullptr);
    ccir->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
    CHECK(rig.card->param(ob::eq) == static_cast<float>(ob::ccir));
    auto* hot = rig.key("Tape: Hot");
    REQUIRE(hot != nullptr);
    hot->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    CHECK(rig.card->param(ob::tape) == static_cast<float>(ob::hot));
    auto* plus9 = rig.key("Operating level: +9 dB");
    REQUIRE(plus9 != nullptr);
    plus9->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    CHECK(rig.card->param(ob::level) == static_cast<float>(ob::plus9));
    auto* output = rig.key("Meters read the output");
    REQUIRE(output != nullptr);
    output->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    CHECK(rig.card->param(ob::meter) == static_cast<float>(ob::meterOutput));

    // LINK latches.
    auto* link = rig.key("Link: output follows input");
    REQUIRE(link != nullptr);
    CHECK(link->lit());
    link->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    CHECK(rig.card->param(ob::link) == 0.0f);
    CHECK_FALSE(link->lit());

    // A model change moves the controls without a command.
    const auto commands = rig.sent.size();
    rig.insert.params[ob::speed] = static_cast<float>(ob::ips7);
    rig.card->refresh(rig.insert);
    CHECK(rig.key("Speed: 7.5 ips")->lit());
    CHECK(rig.sent.size() == commands);
}

TEST_CASE("Orbit's meters, reels and counter follow the machine", "[ui][orbit]")
{
    // The VU scale: -20 near the left stop, 0 VU about 71% across, +3 at the right.
    CHECK(of::vuAngle(3.0f) == Catch::Approx(47.0f));
    CHECK(of::vuAngle(0.0f) == Catch::Approx(-47.0f + 94.0f * std::pow(10.0f, -3.0f / 20.0f)));
    CHECK(of::vuAngle(-90.0f) == Catch::Approx(-47.0f).margin(0.1f));
    CHECK(of::vuAngle(20.0f) == 50.0f);
    // A level of -18 dBFS (a sine's rms) is 0 VU.
    CHECK(of::vuOf(std::pow(10.0f, -18.0f / 20.0f), ob::vuReferenceDbfs) ==
          Catch::Approx(0.0f).margin(0.05f));

    // The reels: 30 frames over 120 degrees, the nearest two blended.
    CHECK(of::framesFor(0.0f).first == 0);
    CHECK(of::framesFor(0.0f).blend == 0.0f);
    const auto between = of::framesFor(6.0f);
    CHECK(between.first == 1);
    CHECK(between.second == 2);
    CHECK(between.blend == Catch::Approx(0.5f));
    CHECK(of::framesFor(118.0f).second == 0); // round to the first again
    CHECK(of::framesFor(240.0f).first == 0);
    CHECK(of::atRest(0.0f));
    CHECK(of::atRest(360.0f));
    CHECK_FALSE(of::atRest(30.0f));
    // The supply reel, fuller, turns slower than the take-up; both faster at 30 ips.
    CHECK(of::reelDegreesPerSecond(15.0, of::supplyPackInches) <
          of::reelDegreesPerSecond(15.0, of::takeUpPackInches));
    CHECK(of::reelDegreesPerSecond(30.0, of::supplyPackInches) ==
          Catch::Approx(2.0 * of::reelDegreesPerSecond(15.0, of::supplyPackInches)));
    // Stopped, a reel coasts on to its next rest and stops there.
    double angle = 50.0;
    for (int i = 0; i < 200 && !of::atRest(static_cast<float>(angle)); ++i)
    {
        const double next = of::coast(angle, 0.016);
        CHECK(next > angle);
        angle = next;
    }
    CHECK(angle == Catch::Approx(120.0));
    CHECK(of::coast(240.0, 0.1) == 240.0);
    CHECK(of::counterText(0.0) == "00:00:00");
    CHECK(of::counterText(3723.9) == "01:02:03");

    // The needle takes about 300 ms to 99% of a step, and never jumps.
    ui::hardware::VuBallistics needle;
    double t = 0.0;
    float previous = 0.0f;
    while (needle.level < 0.99f && t < 1.0)
    {
        needle.advance(1.0f, 0.005);
        CHECK(needle.level - previous < 0.05f);
        previous = needle.level;
        t += 0.005;
    }
    CHECK(t == Catch::Approx(0.3).margin(0.03));

    // Through the card: a tone moves the meters off their stops.
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto* left = rig.control("Left meter");
    REQUIRE(left != nullptr);
    rig.play(200, 0.18f); // about 0 VU at the tape
    const auto reading = left->getDescription();
    CHECK(reading.endsWith(" VU"));
    CHECK(reading.getFloatValue() > -6.0f);
    CHECK(reading.getFloatValue() < 4.0f);
    // Bypassed, they fall back.
    rig.insert.bypass = true;
    rig.card->refresh(rig.insert);
    rig.play(200, 0.18f);
    CHECK(left->getDescription().getFloatValue() < -15.0f);
}

// Renders the card in a few states to PNGs at 100% and 200%, for review (not run
// by default): set NORTHLIGHT_CARD_PNGS to a folder.
TEST_CASE("Orbit card states to PNG", "[.capture]")
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
        {"driven", {{ob::input, 9.0f}, {ob::level, 2.0f}, {ob::tape, 1.0f}, {ob::bias, -40.0f}}},
        {"worn",
         {{ob::speed, 0.0f}, {ob::hiss, 45.0f}, {ob::flutter, 0.15f}, {ob::eq, 1.0f}, {ob::link, 0.0f}}},
        {"off", {}, true},
        {"compact", {{ob::input, 4.5f}}},
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
        rig.play(400, 0.2f);
        if (std::string(state.name) == "compact")
        {
            // The Editor dock: the face at half its size.
            const int body = 300 + 2 * ui::Theme::dark().spacing.xs;
            rig.card->setEmbedded(true);
            rig.card->setBounds(0, 0, rig.card->preferredWidthAt(body), body);
        }
        for (const float scale : {1.0f, 2.0f})
        {
            const auto image = rig.card->createComponentSnapshot(rig.card->getLocalBounds(), true, scale);
            juce::File file(folder + "/orbit-" + state.name + (scale > 1.5f ? "-2x" : "-1x") + ".png");
            file.deleteFile();
            juce::FileOutputStream out(file);
            REQUIRE(out.openedOk());
            juce::PNGImageFormat().writeImageToStream(image, out);
        }
    }
}

TEST_CASE("Orbit's controls sit on its face, their tooltips brief", "[ui][orbit]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto* face = rig.face();
    REQUIRE(face != nullptr);
    const auto bounds = face->getLocalBounds();
    for (auto* child : face->getChildren())
    {
        if (!child->isVisible())
            continue;
        CHECK(bounds.contains(child->getBounds()));
        if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(child))
            CHECK(tip->getTooltip().length() <= 28);
    }
}
