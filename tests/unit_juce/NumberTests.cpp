#include "ui/Number.h"

#include <catch2/catch_test_macros.hpp>

using northlight::ui::fixedText;
namespace ui = northlight::ui;

TEST_CASE("A fixed number of decimals means exactly that many, none for zero", "[ui]")
{
    CHECK(fixedText(-23.4182, 0) == "-23");
    CHECK(fixedText(-23.6, 0) == "-24");
    CHECK(fixedText(9.04, 1) == "9.0");
    CHECK(fixedText(120.0, 0) == "120");
    CHECK(fixedText(1.23456, 2) == "1.23");
}

TEST_CASE("Readouts write a value one way on every card", "[ui][number]")
{
    const auto minus = juce::String::fromUTF8("\xe2\x88\x92");
    CHECK(ui::signedText(1.5, 1) == "+1.5");
    CHECK(ui::signedText(-1.5, 1) == minus + "1.5");
    CHECK(ui::signedText(-0.01, 1) == "0.0");
    CHECK(ui::dbText(-6.0) == minus + "6.0 dB");
    CHECK(ui::dbText(-150.0).startsWith(minus));
    CHECK(ui::reductionText(3.24) == minus + "3.2 dB");
    CHECK(ui::reductionText(0.05).isEmpty());
    CHECK(ui::reductionText(0.05, "0.0 dB") == "0.0 dB");
    CHECK(ui::hzText(440.0) == "440 Hz");
    CHECK(ui::hzText(2450.0) == "2.45 kHz");
    CHECK(ui::hzText(12500.0) == "12.5 kHz");
    CHECK(ui::compactHzText(2450.0) == "2.5 kHz");
    CHECK(ui::compactHzText(12500.0) == "13 kHz");
    CHECK(ui::secondsText(0.25) == "250 ms");
    CHECK(ui::secondsText(1.84) == "1.8 s");
    CHECK(ui::secondsText(12.4) == "12 s");
    CHECK(ui::percentText(39.6) == "40%");
    CHECK(ui::signedPercentText(-40.0) == minus + "40%");
    CHECK(ui::signedPercentText(0.2) == "0%");
    CHECK(ui::sampleRateText(48000.0) == "48 kHz");
    CHECK(ui::sampleRateText(44100.0) == "44.1 kHz");
}
