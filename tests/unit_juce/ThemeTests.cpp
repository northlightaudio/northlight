#include "core/Model.h"
#include "ui/Theme.h"
#include "ui/components/Fader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace northlight;
using Catch::Approx;

TEST_CASE("The embedded tokens load with the design system's values", "[ui][theme]")
{
    const auto theme = ui::Theme::defaults();
    CHECK(theme.name == "Machair Dusk");
    CHECK(theme.colours.canvas == juce::Colour(0xff0f1311));
    CHECK(theme.colours.accent == juce::Colour(0xff93abc2));
    CHECK(theme.colours.lineSubtle.getAlpha() == 0x0f); // #E8E5DA0F: moon cream, faint
    CHECK(theme.colours.lineSubtle.getRed() == 0xe8);
    CHECK(theme.colours.overlay.getAlpha() == 0xb3);
    REQUIRE(theme.track.palette.size() == 12);
    CHECK(theme.track.palette[0].name == "Overcast");
    CHECK(theme.track.palette[11].name == "Slate");
    CHECK(theme.track.named("Slate") == theme.track.palette[11].value);
    CHECK(theme.track.forTrack(0, 0) == theme.track.palette[0].value);
    CHECK(theme.track.forTrack(0, 11) == theme.track.palette[0].value); // cycles through eleven
    CHECK(theme.track.forTrack(0xff123456, 3) == juce::Colour(0xff123456));
    CHECK(theme.fonts.size.body == 13.0f);
    CHECK(theme.fonts.size.micro == 10.0f); // a display's axis labels, a size under caption
    CHECK(theme.fonts.size.timeLarge == 22.0f);
    CHECK(theme.spacing.lg == 16);
    CHECK(theme.radius.md == 6.0f);
    CHECK(theme.sizes.faderTravel == 200);
    CHECK(theme.sizes.faderCapWidth == 36);
    CHECK(theme.sizes.faderCapHeight == 14);
    CHECK(theme.sizes.control == 28);
    CHECK(theme.sizes.controlHeight == 28); // the pre-token name follows
    CHECK(theme.motion.panelMs == 180);
    CHECK(theme.motion.meterFallDbPerSec == 20.0f);
    REQUIRE(theme.meter.zones.size() == 3);
    CHECK(theme.meter.colourAt(-30.0f) == theme.colours.meterLow);
    CHECK(theme.meter.colourAt(-10.0f) == theme.colours.meterMid);
    CHECK(theme.meter.colourAt(-1.0f) == theme.colours.meterHot);
    CHECK(theme.meter.scaleTicksDb.size() == 8);
    CHECK(theme.meter.loudnessTargetLufs == -14.0f);
    CHECK(theme.elevation.popupShadowRadius == 24.0f);
    // The tactile layer.
    CHECK(theme.material.enabled);
    CHECK(theme.material.controlTop == juce::Colour(0xff2a312b));
    CHECK(theme.material.controlShadow.y == 1.0f);
    CHECK(theme.material.controlShadow.blur == 1.0f);
    CHECK(theme.material.controlShadow.colour.getAlpha() == 0x80);
    CHECK(theme.material.pressedInset.inset);
    CHECK(theme.material.pressedInset.blur == 2.0f);
    CHECK(theme.material.glowRecord.blur == 12.0f);
    CHECK(theme.material.ledPitchPx == 2);
    CHECK(theme.material.readoutText == juce::Colour(0xff93abc2));
    // The pre-token aliases map onto tokens.
    CHECK(theme.colours.background == theme.colours.canvas);
    CHECK(theme.colours.surfaceRaised == theme.colours.surface2);
    CHECK(theme.colours.border == theme.colours.lineSubtle);
}

TEST_CASE("A partial override changes only the keys it holds and reports bad values", "[ui][theme]")
{
    auto theme = ui::Theme::defaults();
    std::string error;
    CHECK(ui::Theme::applyText(R"({"color": {"accent": "#FF0000"}, "space": {"lg": 20}})", theme, error));
    CHECK(error.empty());
    CHECK(theme.colours.accent == juce::Colour(0xffff0000));
    CHECK(theme.colours.accentText == theme.colours.textOnAccent);
    CHECK(theme.spacing.lg == 20);
    CHECK(theme.spacing.md == 12);                           // untouched
    CHECK(theme.colours.canvas == juce::Colour(0xff0f1311)); // untouched
    CHECK_FALSE(ui::Theme::applyText(R"({"color": {"accent": "red"}})", theme, error));
    CHECK(error.find("accent") != std::string::npos);
    CHECK(theme.colours.accent == juce::Colour(0xffff0000)); // the bad value was skipped
    CHECK_FALSE(ui::Theme::applyText("not json", theme, error));
    CHECK_FALSE(ui::Theme::applyText("[1,2]", theme, error));
}

TEST_CASE("The fader law puts 0 dB at three quarters of the travel and -inf at the bottom", "[ui][fader]")
{
    CHECK(ui::Fader::dbForFraction(0.75f, 6.0f) == Approx(0.0f));
    CHECK(ui::Fader::dbForFraction(1.0f, 6.0f) == Approx(6.0f));
    CHECK(ui::Fader::dbForFraction(0.0f, 6.0f) <= ui::Fader::minusInfinityDb);
    CHECK(ui::Fader::fractionForDb(0.0f, 6.0f) == Approx(0.75f));
    CHECK(ui::Fader::fractionForDb(6.0f, 6.0f) == Approx(1.0f));
    CHECK(ui::Fader::fractionForDb(-12.0f, 6.0f) < 0.75f);
    CHECK(ui::Fader::fractionForDb(-12.0f, 6.0f) > ui::Fader::fractionForDb(-24.0f, 6.0f));
    // Round trip through the curve.
    for (const float db : {-3.0f, -6.0f, -12.0f, -24.0f, -40.0f, 3.0f})
        CHECK(ui::Fader::dbForFraction(ui::Fader::fractionForDb(db, 6.0f), 6.0f) == Approx(db).margin(0.05f));
}

TEST_CASE("Every channel takes its colour from one rule", "[ui][theme]")
{
    const auto theme = ui::Theme::defaults();
    core::Project p;
    core::Track t;
    t.id = 1;
    p.tracks.push_back(t);
    t.id = 2;
    t.colour = 0xff123456;
    p.tracks.push_back(t);
    core::Bus group;
    group.id = 3;
    core::Bus fx;
    fx.id = 4;
    fx.fx = true;
    core::Bus coloured;
    coloured.id = 5;
    coloured.fx = true;
    coloured.colour = 0xff654321;
    p.buses = {group, fx, coloured};
    p.master.colour = 0xffabcdef; // the master is Slate whatever it holds

    CHECK(theme.track.forChannel(p, 1) == theme.track.forTrack(0, 0));
    CHECK(theme.track.forChannel(p, 2) == juce::Colour(0xff123456));
    CHECK(theme.track.forChannel(p, 3) == theme.track.named("Overcast"));
    CHECK(theme.track.forChannel(p, 4) == theme.track.named("Lichen"));
    CHECK(theme.track.forChannel(p, 5) == juce::Colour(0xff654321));
    CHECK(theme.track.forChannel(p, core::masterId) == theme.track.named("Slate"));
}
