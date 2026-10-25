#include "ui/strip/FeedbackHistory.h"

#include <catch2/catch_test_macros.hpp>

using northlight::ui::strip::FeedbackHistory;

TEST_CASE("A feedback history keeps the newest frames, oldest first, and counts every one", "[ui][strip]")
{
    FeedbackHistory<int> history(4);
    CHECK(history.empty());
    CHECK(history.total() == 0);
    for (int i = 1; i <= 3; ++i)
        history.push(i);
    REQUIRE(history.size() == 3);
    CHECK(history.at(0) == 1);
    CHECK(history.newest() == 3);

    // Past its capacity the oldest go; the total keeps counting.
    for (int i = 4; i <= 10; ++i)
        history.push(i);
    REQUIRE(history.size() == 4);
    CHECK(history.capacity() == 4);
    CHECK(history.total() == 10);
    for (int i = 0; i < 4; ++i)
        CHECK(history.at(i) == 7 + i);
    CHECK(history.newest() == 10);
}
