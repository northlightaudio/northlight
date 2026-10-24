#include "core/Command.h"
#include "core/Model.h"
#include "plugins/Amp.h"
#include "plugins/BuiltIns.h"
#include "plugins/Cabinet.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/components/RenderedFace.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/CoronaFace.h"
#include "ui/strip/ModuleCard.h"
#include "ui/strip/StripStyle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

using namespace northlight;
namespace pa = plugins::amp;
namespace pc = plugins::cabinet;
namespace ef = ui::strip::coronaface;

namespace
{
juce::MouseEvent mouseAt(juce::Component& c, float x, float y)
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
                            1,
                            false);
}

struct Rig
{
    std::shared_ptr<engine::InsertProcessor> live = plugins::BuiltInFactory().create(pa::type);
    core::Insert insert;
    std::vector<std::unique_ptr<core::Command>> sent;
    ui::strip::CardHost host;
    std::unique_ptr<ui::strip::ModuleCard> card;

    explicit Rig(core::Id id = 21)
    {
        live->prepare(48000.0, 240);
        live->setBypassed(false);
        insert.id = id;
        insert.type = pa::type;
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
            if (child->isVisible() && child->getWidth() == static_cast<int>(ef::width))
                return child;
        return nullptr;
    }

    void press(const juce::String& title) const
    {
        auto* c = control(title);
        REQUIRE(c != nullptr);
        c->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
    }
};
} // namespace

TEST_CASE("Corona's face fits its body by scaling, the card narrowing with it", "[ui][corona]")
{
    CHECK(ef::fitScale(1160.0f, 600.0f) == 1.0f);
    CHECK(ef::fitScale(2000.0f, 900.0f) == 1.0f);
    CHECK(ef::fitScale(1160.0f, 300.0f) == Catch::Approx(0.5f));
    CHECK(ef::widthForHeight(300) == 580);

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
    CHECK(ui::strip::moduleColour(theme, pa::type) != theme.colours.textSecondary);
}

TEST_CASE("Corona's page keys swap the amp for the cabinet, and each card remembers its page", "[ui][corona]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    {
        Rig rig(31);
        auto* volume = rig.control("Volume");
        auto* cabinet = rig.control("Cabinet");
        REQUIRE(volume != nullptr);
        REQUIRE(cabinet != nullptr);
        CHECK(volume->isVisible());
        CHECK_FALSE(cabinet->isVisible());
        // The strip is on both pages.
        auto* input = rig.control("Input");
        REQUIRE(input != nullptr);
        CHECK(input->isVisible());

        rig.press("Show the cabinet");
        CHECK_FALSE(volume->isVisible());
        CHECK(cabinet->isVisible());
        CHECK(input->isVisible());
        CHECK(rig.control("Mic A")->isVisible());
        CHECK(rig.sent.empty()); // a page is not a parameter: nothing to undo
    }
    // The same insert's card made again opens on the cabinet; another starts on the amp.
    {
        Rig again(31);
        CHECK(again.control("Cabinet")->isVisible());
        Rig other(32);
        CHECK(other.control("Volume")->isVisible());
    }
}

TEST_CASE("Corona's knobs, levers, keys and footswitch set the amp's parameters", "[ui][corona]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto* volume = dynamic_cast<ui::Knob*>(rig.control("Volume"));
    REQUIRE(volume != nullptr);
    CHECK(volume->accessibleValueText() == "4.0");
    volume->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    CHECK(rig.card->param(pa::volume) > 4.0f);
    CHECK_FALSE(rig.sent.empty());

    // BRIGHT is a lever: up on, down off.
    auto* bright = rig.control("Bright");
    REQUIRE(bright != nullptr);
    bright->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    CHECK(rig.card->param(pa::bright) == 0.0f);
    bright->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    CHECK(rig.card->param(pa::bright) == 1.0f);

    // The footswitch: reverb off and on, tremolo on.
    rig.press("Reverb footswitch");
    CHECK(rig.card->param(pa::reverbOn) == 0.0f);
    rig.press("Tremolo footswitch");
    CHECK(rig.card->param(pa::tremoloOn) == 1.0f);

    // QUALITY's keys choose the oversampling.
    rig.press("Quality: 8x oversampling");
    CHECK(rig.card->param(pa::quality) == 2.0f);

    // On the cabinet page: the cabinet knob steps through the models, a mic's
    // type keys choose its type, its ON key switches it.
    rig.press("Show the cabinet");
    auto* cabinet = dynamic_cast<ui::Knob*>(rig.control("Cabinet"));
    REQUIRE(cabinet != nullptr);
    CHECK(cabinet->accessibleValueText() == "2x12 Open");
    cabinet->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    CHECK(rig.card->param(pa::cabinet) == 1.0f);
    rig.press("Mic A: Condenser");
    CHECK(rig.card->param(pa::micParam(0, pa::micType)) == static_cast<float>(pc::condenser));
    rig.press("Mic B on");
    CHECK(rig.card->param(pa::micParam(1, pa::micOn)) == 1.0f);

    // SPEAKER down is the line out: the mics' controls have nothing to do.
    auto* speaker = rig.control("Speaker");
    REQUIRE(speaker != nullptr);
    speaker->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    CHECK(rig.card->param(pa::cabinetOn) == 0.0f);
    CHECK_FALSE(rig.control("Mic A on")->isEnabled());
    CHECK(rig.card->createComponentSnapshot(rig.card->getLocalBounds(), true, 1.0f).isValid());
}

TEST_CASE("A mic dragged on the baffle moves across the cone; keys move it and back it off", "[ui][corona]")
{
    // The ray's arithmetic: a place on the cone and back.
    for (int model = 0; model < pc::custom; ++model)
    {
        const auto speakers = ef::speakers(model);
        REQUIRE_FALSE(speakers.empty());
        for (const auto& s : speakers)
        {
            // Every speaker sits inside the baffle.
            CHECK(s.x - s.diameter * 0.5f >= ef::baffle.x);
            CHECK(s.x + s.diameter * 0.5f <= ef::baffle.x + ef::baffle.w);
            CHECK(s.y - s.diameter * 0.5f >= ef::baffle.y);
            CHECK(s.y + s.diameter * 0.5f <= ef::baffle.y + ef::baffle.h);
        }
        for (int mic = 0; mic < 2; ++mic)
            for (float position : {0.0f, 0.4f, 1.0f})
                CHECK(ef::positionAt(speakers[0], mic, ef::micPoint(speakers[0], mic, position)) ==
                      Catch::Approx(position).margin(1.0e-4));
    }
    CHECK(ef::capsuleScale(1.0f) == Catch::Approx(1.0f));
    CHECK(ef::capsuleScale(100.0f) == Catch::Approx(0.45f));
    CHECK(ef::capsuleScale(10.0f) < ef::capsuleScale(3.0f));

    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    rig.press("Show the cabinet");
    auto* mic = rig.control("Mic A");
    REQUIRE(mic != nullptr);
    REQUIRE(mic->isEnabled());
    const float before = rig.card->param(pa::micParam(0, pa::micPosition));
    mic->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
    CHECK(rig.card->param(pa::micParam(0, pa::micPosition)) == Catch::Approx(before + 5.0f));
    const float near = rig.card->param(pa::micParam(0, pa::micDistance));
    mic->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    CHECK(rig.card->param(pa::micParam(0, pa::micDistance)) > near);
    // The handle follows the capsule.
    const auto speakers = ef::speakers(pc::open2x12);
    const auto c = ef::micPoint(speakers[0], 0, rig.card->param(pa::micParam(0, pa::micPosition)) * 0.01f);
    CHECK(mic->getBounds().contains(juce::roundToInt(c.x), juce::roundToInt(c.y)));
    // A custom response leaves the mics nothing to do.
    rig.insert.params[pa::cabinet] = static_cast<float>(pc::custom);
    rig.card->refresh(rig.insert);
    CHECK_FALSE(mic->isVisible());
}

TEST_CASE("Corona's jewel and input meter follow the amp", "[ui][corona]")
{
    CHECK(ef::jewelBrightness(0.0f, false) == 0.0f);
    CHECK(ef::jewelBrightness(0.0f, true) == Catch::Approx(0.3f));
    CHECK(ef::jewelBrightness(2.0f, true) == Catch::Approx(1.0f));
    CHECK(ef::litSteps(-60.0f) == 0);
    CHECK(ef::litSteps(-12.0f) == 7);
    CHECK(ef::litSteps(0.0f) == 10);

    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    // A loud chord through the amp: the frames carry the drive and the input.
    std::vector<float> l(240), r(240);
    for (int b = 0; b < 100; ++b)
    {
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = 0.5f * std::sin(static_cast<float>(b * 240 + static_cast<int>(i)) * 0.03f);
        rig.live->process(l.data(), r.data(), 240);
        if (b % 4 == 0)
            rig.card->tick(20.0);
    }
    const auto& last = rig.card->historyAt(rig.card->historySize() - 1);
    CHECK(last.input > 0.3f);
    CHECK(last.extra > 0.0f);
    auto* jewel = rig.control("Power amp drive");
    REQUIRE(jewel != nullptr);
    CHECK(jewel->getDescription().isNotEmpty());
    const auto image = rig.card->createComponentSnapshot(rig.card->getLocalBounds(), true, 1.0f);
    CHECK(image.isValid());
}

TEST_CASE("Corona's controls sit on its face on both pages, their tooltips brief", "[ui][corona]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto* face = rig.face();
    REQUIRE(face != nullptr);
    for (int page = 0; page < 2; ++page)
    {
        if (page == 1)
            rig.press("Show the cabinet");
        const auto bounds = face->getLocalBounds();
        for (auto* child : face->getChildren())
        {
            if (!child->isVisible())
                continue;
            INFO(child->getTitle());
            CHECK(bounds.contains(child->getBounds()));
            if (auto* tip = dynamic_cast<juce::SettableTooltipClient*>(child))
                CHECK(tip->getTooltip().length() <= 32);
        }
    }
}

TEST_CASE("A two-way lever flips wherever it is clicked, not just on its nut", "[ui][corona]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto* bright = rig.control("Bright");
    REQUIRE(bright != nullptr);
    REQUIRE(rig.card->param(pa::bright) == 1.0f); // up: on
    // The bat above the nut, and below it: each click flips it.
    const float x = static_cast<float>(bright->getWidth()) * 0.5f;
    const float nutY = x; // the nut is centred in the image's top square
    REQUIRE(bright->hitTest(juce::roundToInt(x), juce::roundToInt(nutY - 25.0f)));
    bright->mouseDown(mouseAt(*bright, x, nutY - 25.0f));
    CHECK(rig.card->param(pa::bright) == 0.0f);
    bright->mouseDown(mouseAt(*bright, x, nutY + 25.0f));
    CHECK(rig.card->param(pa::bright) == 1.0f);
    // The image's empty margins beside the switch do not take the mouse.
    CHECK_FALSE(bright->hitTest(1, juce::roundToInt(nutY)));
}

TEST_CASE("In the dock Corona goes compact, and still paints", "[ui][corona]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto* face = dynamic_cast<ui::RenderedFace*>(rig.face());
    REQUIRE(face != nullptr);
    CHECK_FALSE(face->isCompact());
    const auto& theme = ui::Theme::dark();
    const int body = 300 + 2 * theme.spacing.xs; // half height
    rig.card->setEmbedded(true);
    rig.card->setBounds(0, 0, rig.card->preferredWidthAt(body), body);
    CHECK(face->isCompact());
    // A knob in hand: its value takes its name's place (painted without fault).
    face->setActiveControl(pa::volume);
    CHECK(rig.card->createComponentSnapshot(rig.card->getLocalBounds(), true, 1.0f).isValid());
    rig.press("Show the cabinet");
    CHECK(rig.card->createComponentSnapshot(rig.card->getLocalBounds(), true, 1.0f).isValid());
}
