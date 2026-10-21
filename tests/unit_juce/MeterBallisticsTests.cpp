#include "ui/MeterBallistics.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace northlight::ui;
using Catch::Approx;

TEST_CASE("A meter rises over its time constant and falls at its rate", "[ui][meter]")
{
    // A lone 5 ms spike to 0 dB from -40 moves the bar only part of the way.
    const float afterSpike = followLevel(-40.0f, 0.0f, 5.0, 40.0, 20.0f);
    CHECK(afterSpike > -40.0f);
    CHECK(afterSpike < -30.0f);
    // A held level gets there: after 200 ms (five time constants) it is within 1 dB.
    float shown = -40.0f;
    for (int i = 0; i < 12; ++i)
        shown = followLevel(shown, 0.0f, 16.7, 40.0, 20.0f);
    CHECK(shown > -1.0f);
    // It falls 20 dB a second, no faster.
    CHECK(followLevel(0.0f, -60.0f, 500.0, 40.0, 20.0f) == Approx(-10.0f));
    CHECK(followLevel(-5.0f, -6.0f, 500.0, 40.0, 20.0f) == Approx(-6.0f));
    // No rise time: instant.
    CHECK(followLevel(-40.0f, -3.0f, 1.0, 0.0, 20.0f) == -3.0f);
}

TEST_CASE("Gain reduction moves quickly in and slowly out", "[ui][meter]")
{
    const float in = followReduction(0.0f, 6.0f, 20.0, 20.0, 300.0);
    const float out = followReduction(6.0f, 0.0f, 20.0, 20.0, 300.0);
    CHECK(in > 3.0f);                                                // most of the way in a time constant
    CHECK(out > 5.5f);                                               // barely let go
    CHECK(followReduction(0.005f, 0.0f, 16.0, 20.0, 300.0) == 0.0f); // settles
}

TEST_CASE("A readout shows the loudest reading of each period", "[ui][meter]")
{
    MeterReadout r;
    CHECK(r.update(-12.0f, 16.0)); // the first reading shows at once
    CHECK(r.value() == -12.0f);
    CHECK_FALSE(r.update(-3.0f, 16.0)); // held for the period
    CHECK_FALSE(r.update(-20.0f, 16.0));
    CHECK(r.value() == -12.0f);
    for (int i = 0; i < 15; ++i)
        r.update(-20.0f, 16.0);
    CHECK(r.value() == -3.0f); // the period's loudest
    MeterReadout gr(0.0f);
    CHECK(gr.value() == 0.0f);
}
