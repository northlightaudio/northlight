#include "core/Settings.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <set>
#include <string>

using namespace northlight;

TEST_CASE("Every setting has its own key and a default its type can read", "[core][settings]")
{
    std::set<std::string_view> keys;
    for (const auto* s : settings::all())
    {
        INFO(s->key);
        CHECK(keys.insert(s->key).second);
        if (s->type == settings::Type::flag)
            CHECK((s->fallback == "1" || s->fallback == "0"));
        if (s->type == settings::Type::number || s->type == settings::Type::choice)
            CHECK_FALSE(s->fallback.empty());
    }
    CHECK(settings::pluginsOnTop.key == "plugins.pin"); // files written before the schema keep reading
    CHECK(settings::recordingFormat.comingSoon);        // shown, read by nothing yet
}

TEST_CASE("A setting reads its stored text, else its default, as its type", "[core][settings]")
{
    std::map<std::string, std::string> stored{
        {"recording.countIn", "4"}, {"editing.snap", "0"}, {"general.recentCount", "12.00"}};
    const settings::Reader read([&](const std::string& key)
                                { return stored.count(key) ? stored.at(key) : std::string{}; });
    CHECK(read.integer(settings::countIn) == 4);
    CHECK_FALSE(read.flag(settings::snap));
    CHECK(read.integer(settings::recentCount) == 12);
    CHECK(read.number(settings::metronomeLevel) == -6.0); // the default
    CHECK(read.integer(settings::autosaveMinutes) == 2);
    CHECK(read.flag(settings::followPlayhead));
    CHECK(read.text(settings::namePattern) == "{track} {take}");
    CHECK(settings::Reader({}).text(settings::contrast) == "standard");
    CHECK(settings::flagText(true) == "1");
}
