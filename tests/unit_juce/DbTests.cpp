#include "ui/Db.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace northlight::ui;
using Catch::Approx;

TEST_CASE("Gains read and convert the same in every view", "[ui][db]")
{
    CHECK(db::text(db::fromGain(0.0f)) == "-inf");
    CHECK(db::text(-6.02) == "-6.0");
    CHECK(db::text(2.5) == "+2.5");
    CHECK(db::text(0.0) == "0.0");
    // A gain far below a slider's travel keeps its value everywhere else.
    const float quiet = db::toGain(-80.0);
    CHECK(db::fromGain(quiet) == Approx(-80.0).margin(0.001));
    CHECK(db::text(db::fromGain(quiet)) == "-80.0");
    // A control whose travel stops at -60: its bottom is silence, and a quieter
    // gain shows at the bottom.
    CHECK(db::toGain(-60.0, -60.0) == 0.0f);
    CHECK(db::toGain(-59.0, -60.0) > 0.0f);
    CHECK(db::fromGain(quiet, -60.0, 6.0) == Approx(-60.0));
    CHECK(db::parse("-inf") == db::silence);
    CHECK(db::parse("-12.5") == Approx(-12.5));
    CHECK(db::toGain(db::silence) == 0.0f);
}
