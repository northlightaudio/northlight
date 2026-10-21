#include "version/Version.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <regex>
#include <string>

using namespace northlight;

TEST_CASE("The version number is MAJOR.MINOR.PATCH", "[version]")
{
    CHECK(std::regex_match(std::string(version::number()), std::regex(R"(\d+\.\d+\.\d+)")));
}

TEST_CASE("The full version is semantic: number, label, then the commit as build metadata", "[version]")
{
    const std::string full = version::full();
    std::string expected = version::number();
    if (std::strlen(version::label()) > 0)
        expected += std::string("-") + version::label();
    CHECK(full.rfind(expected, 0) == 0); // starts with it
    if (std::strlen(version::commit()) > 0)
    {
        CHECK(full.find(std::string("+") + version::commit()) != std::string::npos);
        CHECK((full.size() >= 6 && full.compare(full.size() - 6, 6, ".dirty") == 0) == version::dirty());
    }
    CHECK(std::regex_match(full, std::regex(R"(\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?(\+[0-9a-f]+(\.dirty)?)?)")));
}

TEST_CASE("A development build shows its commit; a release shows only the version", "[version]")
{
    const std::string shown = version::display();
    const bool dev = std::strcmp(version::label(), "dev") == 0;
    if (dev && std::strlen(version::commit()) > 0)
        CHECK(shown.find(version::commit()) != std::string::npos);
    if (!dev)
        CHECK(shown.find('+') == std::string::npos);
    CHECK(shown.rfind(version::number(), 0) == 0);
}
