#include "core/Model.h"
#include "plugins/BeatSync.h"
#include "plugins/BuiltIns.h"
#include "plugins/TapeEcho.h"
#include "ui/Theme.h"
#include "ui/components/Knob.h"
#include "ui/strip/BuiltInCards.h"
#include "ui/strip/StripStyle.h"
#include "ui/strip/TapeEchoFace.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
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

    // A control on the console by its accessible title ("Heads", "Stereo").
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

    // The console itself: the card's one child drawn scaled.
    juce::Component* face() const
    {
        for (auto* child : card->getChildren())
            if (child->isVisible() && child->getWidth() == static_cast<int>(ui::strip::tapeface::width))
                return child;
        return nullptr;
    }
};
} // namespace

TEST_CASE("The tape echo's console fits its body by scaling, the card narrowing with it", "[ui][tapeecho]")
{
    namespace tf = ui::strip::tapeface;
    CHECK(tf::fitScale(1160.0f, 600.0f) == 1.0f);
    CHECK(tf::fitScale(2000.0f, 900.0f) == 1.0f); // never drawn larger than it was made
    CHECK(tf::fitScale(1160.0f, 300.0f) == Catch::Approx(0.5f));
    CHECK(tf::fitScale(580.0f, 600.0f) == Catch::Approx(0.5f));
    CHECK(tf::widthForHeight(300) == 580);
    CHECK(tf::widthForHeight(900) == 1160);

    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto* face = rig.face();
    REQUIRE(face != nullptr);
    CHECK(face->getTransform().mat00 == Catch::Approx(1.0f)); // at its preferred size, 1:1

    // A dock half as tall: the console at half size, the card half as wide.
    const auto& theme = ui::Theme::dark();
    const int body = 300 + 2 * theme.spacing.xs;
    const int width = rig.card->preferredWidthAt(body);
    CHECK(width == 580 + 2 * theme.spacing.sm);
    rig.card->setEmbedded(true);
    rig.card->setBounds(0, 0, width, body);
    CHECK(face->getTransform().mat00 == Catch::Approx(0.5f));
    const auto shown = face->getBounds().toFloat().transformedBy(face->getTransform());
    CHECK(rig.card->getLocalBounds().toFloat().expanded(1.0f).contains(shown));
}

TEST_CASE("The tape echo's console reads its machine and its controls set the parameters", "[ui][tapeecho]")
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

    // HEADS is a knob with eight positions, named in full.
    auto* heads = dynamic_cast<ui::Knob*>(rig.control("Heads"));
    REQUIRE(heads != nullptr);
    CHECK(heads->accessibleValueText() == "Heads 1 + 3");
    heads->keyPressed(juce::KeyPress(juce::KeyPress::homeKey)); // a knob's Home is its top
    CHECK(rig.card->param(te::heads) == static_cast<float>(te::headModes.size() - 1));
    CHECK_FALSE(rig.sent.empty());

    // STEREO is a three-way lever: Pan, Ping, L/R from the top.
    auto* stereo = rig.control("Stereo");
    REQUIRE(stereo != nullptr);
    stereo->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    CHECK(rig.card->param(te::stereo) == static_cast<float>(te::stereoPingPong));
    stereo->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    CHECK(rig.card->param(te::stereo) == static_cast<float>(te::stereoTracks));

    // DRY down kills the dry signal.
    auto* dry = rig.control("Dry");
    REQUIRE(dry != nullptr);
    dry->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    CHECK(rig.card->param(te::killDry) == 1.0f);

    // A model change moves the controls without a command.
    const auto commands = rig.sent.size();
    rig.insert.params[te::heads] = 2.0f;
    rig.card->refresh(rig.insert);
    CHECK(heads->value() == 2.0);
    CHECK(rig.sent.size() == commands);
}

TEST_CASE("With SYNC on, the tape echo's REPEAT RATE steps through note values", "[ui][tapeecho]")
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    Rig rig;
    auto* rate = dynamic_cast<ui::Knob*>(rig.control("Repeat rate"));
    auto* sync = rig.control("Sync");
    REQUIRE(rate != nullptr);
    REQUIRE(sync != nullptr);
    CHECK(rate->accessibleValueText() == "180 ms"); // free: the motor's time

    // SYNC up: tempo sync on, at an eighth the first time.
    sync->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    CHECK(plugins::sync::choiceOf(rig.card->param(te::sync)) == 8);
    CHECK(rate->accessibleValueText() == "1/8");
    // The knob now steps the note values, one at a time.
    rate->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    CHECK(plugins::sync::choiceOf(rig.card->param(te::sync)) == 9);
    CHECK(rate->accessibleValueText() == "1/8 D");
    CHECK(rig.card->param(te::time) == Catch::Approx(rig.insert.params[te::time])); // the motor's time kept

    // SYNC down frees the motor; up again returns to the last note value.
    sync->keyPressed(juce::KeyPress(juce::KeyPress::downKey));
    CHECK(rig.card->param(te::sync) == 0.0f);
    CHECK(rate->accessibleValueText() == "180 ms");
    sync->keyPressed(juce::KeyPress(juce::KeyPress::upKey));
    CHECK(plugins::sync::choiceOf(rig.card->param(te::sync)) == 9);
}

TEST_CASE("The tape echo's meter, lamp, neons and rollers move as the machine does", "[ui][tapeecho]")
{
    namespace tf = ui::strip::tapeface;
    // The VU scale: -20 near the left stop, 0 VU about 71% across, +3 at the right.
    CHECK(tf::vuAngle(3.0f) == Catch::Approx(50.0f));
    CHECK(tf::vuAngle(0.0f) == Catch::Approx(-50.0f + 100.0f * std::pow(10.0f, -3.0f / 20.0f)));
    CHECK(tf::vuAngle(-20.0f) < -40.0f);
    CHECK(tf::vuAngle(20.0f) == 53.0f);                               // pinned past the end
    CHECK(tf::vuAngle(-90.0f) == Catch::Approx(-50.0f).margin(0.1f)); // at rest
    // 0 VU is -10 dBFS on the tape, which saturates towards -6: a healthy record level reads
    // about 0, a hot one reaches the red.
    CHECK(tf::vuOf(std::pow(10.0f, -10.0f / 20.0f)) == Catch::Approx(0.0f).margin(1.0e-4));
    CHECK(tf::vuOf(std::pow(10.0f, -6.0f / 20.0f)) == Catch::Approx(4.0f).margin(1.0e-4));

    // The loop gain lamp: dark while the loop is steady, amber near the edge, red past it.
    CHECK(tf::lampFor(0.45f, true) == tf::Lamp::dark);
    CHECK(tf::lampFor(0.9f, true) == tf::Lamp::near);
    CHECK(tf::lampFor(1.05f, true) == tf::Lamp::singing);
    CHECK(tf::lampFor(1.05f, false) == tf::Lamp::dark);

    // The needle takes about 300 ms to 99% of a step, and never jumps.
    tf::VuNeedle needle;
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

    // A neon strikes 42 dB down, is full at full scale, lights at once and dies away.
    CHECK(tf::neonBrightness(0.0f) == 0.0f);
    CHECK(tf::neonBrightness(1.0f) == 1.0f);
    CHECK(tf::neonBrightness(std::pow(10.0f, -21.0f / 20.0f)) == Catch::Approx(0.5f));
    CHECK(tf::neonFollow(0.2f, 0.9f, 0.016) == 0.9f);
    const float fading = tf::neonFollow(0.9f, 0.0f, 0.12);
    CHECK(fading == Catch::Approx(0.9f * std::exp(-1.0f)));

    // The rollers turn with the tape: the smaller faster, both slowed alike when
    // the fastest would strobe.
    const double slow = tf::rollerSpeed(1.0, tf::pinchRadius);
    CHECK(slow == Catch::Approx(1.2 / 0.28));
    CHECK(tf::rollerSpeed(1.0, tf::tensionRadius) / slow == Catch::Approx(0.28 / 0.15));
    CHECK(tf::rollerSpeed(0.02, tf::tensionRadius) == Catch::Approx(tf::rollerCap));
    CHECK(tf::rollerSpeed(0.02, tf::tensionRadius) / tf::rollerSpeed(0.02, tf::pinchRadius) ==
          Catch::Approx(0.28 / 0.15));
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
        {"synced", {{te::sync, 9.0f}, {te::stereo, 1.0f}, {te::heads, 6.0f}, {te::drive, 12.0f}}},
        {"killdry", {{te::sync, 16.0f}, {te::feedback, 0.0f}, {te::killDry, 1.0f}, {te::spring, 50.0f}}},
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

TEST_CASE("The tape echo's controls sit on its console, their tooltips brief", "[ui][tapeecho]")
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
