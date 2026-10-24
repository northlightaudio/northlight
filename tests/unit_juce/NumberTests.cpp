#include "ui/Number.h"

#include <catch2/catch_test_macros.hpp>

using northlight::ui::fixedText;

TEST_CASE("A fixed number of decimals means exactly that many, none for zero", "[ui]")
{
    CHECK(fixedText(-23.4182, 0) == "-23");
    CHECK(fixedText(-23.6, 0) == "-24");
    CHECK(fixedText(9.04, 1) == "9.0");
    CHECK(fixedText(120.0, 0) == "120");
    CHECK(fixedText(1.23456, 2) == "1.23");
}
