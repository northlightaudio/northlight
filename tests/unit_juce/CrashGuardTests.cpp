#include "hosting/CrashGuard.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using namespace northlight;

namespace
{
int* volatile nowhere = nullptr;

void writeThroughNull(void*)
{
    *nowhere = 1; // an access violation, as a broken plugin would raise
}

void doNothing(void* context)
{
    *static_cast<int*>(context) = 42;
}
} // namespace

TEST_CASE("The crash guard contains a plugin's access violation and thrown exception", "[hosting][crash]")
{
    int value = 0;
    CHECK(hosting::callGuarded(&doNothing, &value));
    CHECK(value == 42);
#if defined(_MSC_VER)
    CHECK_FALSE(hosting::callGuarded(&writeThroughNull, nullptr));
    auto throws = [] { throw std::runtime_error("from inside a plugin"); };
    CHECK_FALSE(hosting::callGuarded(throws));
#endif
    int calls = 0;
    auto fine = [&] { ++calls; };
    CHECK(hosting::callGuarded(fine));
    CHECK(calls == 1);
}
