#include "core/NoteNames.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

TEST_CASE("Middle C is C3 everywhere", "[core][notes]")
{
    CHECK(core::noteName(core::middleC) == "C3");
    CHECK(core::octaveOf(60) == 3);
    CHECK(core::noteName(69) == "A3");
    CHECK(core::noteName(61) == "C#3");
    CHECK(core::noteName(0) == "C-2");
    CHECK(core::noteName(127) == "G8");
}

TEST_CASE("Notes below zero name their octave by floor division", "[core][notes]")
{
    CHECK(core::noteName(-1) == "B-3");
    CHECK(core::octaveOf(-12) == -3);
    CHECK(core::octaveOf(-13) == -4);
}

TEST_CASE("Black keys are the five sharps of each octave", "[core][notes]")
{
    int black = 0;
    for (int n = 60; n < 72; ++n)
        black += core::isBlackKey(n) ? 1 : 0;
    CHECK(black == 5);
    CHECK(core::isBlackKey(61));
    CHECK_FALSE(core::isBlackKey(64));
    CHECK(core::isBlackKey(-11)); // C#, below zero
}
