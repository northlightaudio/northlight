#include "app/Migration.h"

#include <catch2/catch_test_macros.hpp>

using namespace northlight;

TEST_CASE("Aural's settings and folders are copied to Northlight once", "[app][migration]")
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("northlight-migration", "", false);
    const auto appData = root.getChildFile("AppData");
    const auto documents = root.getChildFile("Documents");
    REQUIRE(appData.getChildFile("Aural/Autosave").createDirectory().wasOk());
    REQUIRE(appData.getChildFile("Aural/Aural.settings")
                .replaceWithText(
                    R"(<VALUE name="browser.favourites" val="aural.eq;clap:x.y;aural.strip.gate"/>)"));
    REQUIRE(appData.getChildFile("Aural/Autosave/song.aural").replaceWithText("{}"));
    REQUIRE(documents.getChildFile("Aural/Templates").createDirectory().wasOk());
    REQUIRE(documents.getChildFile("Aural/Templates/Band.aural").replaceWithText("{}"));
    REQUIRE(documents.getChildFile("Aural/Recordings").createDirectory().wasOk());

    const auto first = migration::fromAural(appData, documents);
    CHECK(first.settingsCopied);
    CHECK(first.documentsCopied.contains("Templates"));
    const auto settings = appData.getChildFile("Northlight/Northlight.settings");
    REQUIRE(settings.existsAsFile());
    CHECK(settings.loadFileAsString() ==
          R"(<VALUE name="browser.favourites" val="northlight.eq;clap:x.y;northlight.strip.gate"/>)");
    CHECK_FALSE(appData.getChildFile("Northlight/Aural.settings").exists());
    CHECK(appData.getChildFile("Northlight/Autosave/song.aural").existsAsFile());
    CHECK(documents.getChildFile("Northlight/Templates/Band.aural").existsAsFile());
    CHECK_FALSE(documents.getChildFile("Northlight/Recordings").exists()); // left where projects point
    // The originals stay.
    CHECK(appData.getChildFile("Aural/Aural.settings").existsAsFile());

    // A second run changes nothing.
    REQUIRE(settings.replaceWithText("changed"));
    const auto second = migration::fromAural(appData, documents);
    CHECK_FALSE(second.settingsCopied);
    CHECK(second.documentsCopied.isEmpty());
    CHECK(settings.loadFileAsString() == "changed");
    root.deleteRecursively();
}

TEST_CASE("Only built-in names are rewritten in the settings", "[app][migration]")
{
    CHECK(migration::migrateSettingsText(R"(val="aural.eq")") == R"(val="northlight.eq")");
    CHECK(migration::migrateSettingsText("plugin:aural.reverb") == "plugin:northlight.reverb");
    // Inside a path or a word it stays.
    CHECK(migration::migrateSettingsText(R"(val="C:\Music\x.aural.bak")") == R"(val="C:\Music\x.aural.bak")");
    CHECK(migration::migrateSettingsText("binaural.eq") == "binaural.eq");
}
