#include "app/WindowPlacement.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

TEST_CASE("A saved window state reads back its bounds and whether it was maximised", "[app][window]")
{
    const auto normal = parsePlacement("100 80 1600 900");
    REQUIRE(normal.has_value());
    CHECK(normal->bounds == juce::Rectangle<int>(100, 80, 1600, 900));
    CHECK_FALSE(normal->maximised);

    const auto maximised = parsePlacement("fs -1800 40 1400 800");
    REQUIRE(maximised.has_value());
    CHECK(maximised->maximised);
    CHECK(maximised->bounds == juce::Rectangle<int>(-1800, 40, 1400, 800)); // a screen left of the main one

    CHECK_FALSE(parsePlacement("").has_value());
    CHECK_FALSE(parsePlacement("10 20 30").has_value());
    CHECK_FALSE(parsePlacement("a b c d").has_value());
    CHECK_FALSE(parsePlacement("10 20 0 400").has_value());
}

TEST_CASE("A window is reachable only while its top strip is on a screen", "[app][window]")
{
    const std::vector<juce::Rectangle<int>> screens{{0, 0, 1920, 1040}, {1920, 0, 2560, 1400}};
    CHECK(reachable({100, 80, 1600, 900}, screens));
    CHECK(reachable({1700, 100, 1600, 900}, screens));        // across both
    CHECK(reachable({-1400, 100, 1600, 900}, screens));       // mostly off the left edge, 200 px still on
    CHECK_FALSE(reachable({-1550, 100, 1600, 900}, screens)); // only 50 px of it left
    CHECK_FALSE(reachable({100, 1030, 1600, 900}, screens));  // header bar below the work area
    CHECK_FALSE(reachable({100, -20, 1600, 900}, screens));   // header bar above the top
    CHECK_FALSE(reachable({5000, 100, 1600, 900}, screens));  // a monitor that is gone
}
