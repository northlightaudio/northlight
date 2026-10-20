#include "app/CrashReports.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <system_error>

using namespace aural;

TEST_CASE("A crash report is written, listed until acknowledged, then set aside", "[app][crash]")
{
    const auto folder =
        std::filesystem::temp_directory_path() / "aural-crash-tests" /
        std::to_string(std::filesystem::file_time_type::clock::now().time_since_epoch().count());
    CHECK(crash::pending(folder).empty()); // no folder yet
#if defined(_WIN32)
    const auto dump = crash::writeReportNow(folder);
    REQUIRE_FALSE(dump.empty());
    CHECK(std::filesystem::file_size(dump) > 0);
    auto note = dump;
    note += ".txt";
    CHECK(std::filesystem::exists(note));
    const auto reports = crash::pending(folder);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0] == dump);
    crash::acknowledge(folder);
    CHECK(crash::pending(folder).empty());
    CHECK(std::filesystem::exists(folder / "Seen" / dump.filename()));
#endif
    std::error_code ec;
    std::filesystem::remove_all(folder, ec);
}
