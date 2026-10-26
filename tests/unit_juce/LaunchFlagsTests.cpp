#include "app/LaunchFlags.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using namespace northlight;

namespace
{
struct Run
{
    std::vector<std::string> log;
    bool stop = false;
};

const std::vector<LaunchFlag<Run>>& testFlags()
{
    static const std::vector<LaunchFlag<Run>> flags{
        {"--start-up", "", 0, "read before the window exists"},
        {"--open",
         "<file>",
         1,
         "open a file",
         [](Run& r, LaunchArguments& a) { r.log.push_back("open " + a.text().toStdString()); }},
        {"--loop",
         "<from> <to>",
         2,
         "a loop",
         [](Run& r, LaunchArguments& a)
         {
             const auto from = a.integer();
             const auto to = a.integer();
             r.log.push_back("loop " + std::to_string(from) + "-" + std::to_string(to));
         }},
        {"--prefs",
         "[page]",
         0,
         "open Preferences, on a page if one follows",
         [](Run& r, LaunchArguments& a)
         {
             const auto* page = a.value();
             r.log.push_back("prefs " + (page != nullptr ? page->text.toStdString() : std::string("-")));
         }},
        {"--key",
         "<root> [minor]",
         1,
         "a key",
         [](Run& r, LaunchArguments& a)
         {
             const auto root = a.integer();
             r.log.push_back("key " + std::to_string(root) + (a.word("minor") ? " minor" : ""));
         }},
        {"--quit",
         "",
         0,
         "stop here",
         [](Run& r, LaunchArguments&)
         {
             r.log.push_back("quit");
             r.stop = true;
         }},
    };
    return flags;
}

std::vector<std::string> run(const juce::String& commandLine)
{
    Run r;
    runLaunchFlags(testFlags(), r, commandLine, r.stop);
    return r.log;
}
} // namespace

TEST_CASE("Launch flags run in order with their arguments", "[app][launch]")
{
    CHECK(run("--open a.northlight --loop 1 4 --key 9 minor --key 2") ==
          std::vector<std::string>{"open a.northlight", "loop 1-4", "key 9 minor", "key 2"});
    // An optional value is taken only when it is not another flag.
    CHECK(run("--prefs audio --prefs --open b") ==
          std::vector<std::string>{"prefs audio", "prefs -", "open b"});
    // A flag missing its arguments is skipped; unknown tokens and start-up flags are ignored.
    CHECK(run("--start-up whatever --loop 1") == std::vector<std::string>{});
    // A flag can end the run.
    CHECK(run("--quit --open c") == std::vector<std::string>{"quit"});
}

TEST_CASE("The launch help lists every flag with its arguments, wrapped", "[app][launch]")
{
    const auto help = launchHelp(testFlags(), 60);
    for (const auto& flag : testFlags())
        CHECK(help.contains(flag.name));
    CHECK(help.contains("  --loop <from> <to>"));
    for (const auto& line : juce::StringArray::fromLines(help))
        CHECK(line.length() <= 60);
}

TEST_CASE("Work a launch flag scheduled does nothing once the schedule is cancelled", "[app][launch]")
{
    LaunchSchedule schedule;
    int runs = 0;
    const auto call = schedule.guard([&runs] { ++runs; });
    call();
    CHECK(runs == 1);
    schedule.cancel();
    CHECK(schedule.cancelled());
    call();
    CHECK(runs == 1);
}

TEST_CASE("A bare project path is the file to open; developer flags make an instance of its own",
          "[app][launch]")
{
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("northlight-launch-tests")
                            .getChildFile(juce::Uuid().toString());
    REQUIRE(folder.createDirectory().wasOk());
    const auto song = folder.getChildFile("My Song.northlight");
    REQUIRE(song.replaceWithText("{}"));
    const auto quoted = "\"" + song.getFullPathName() + "\"";

    CHECK(launchProjectFile(quoted) == song); // as Explorer passes it, spaces and all
    CHECK(launchProjectFile("") == juce::File());
    CHECK(launchProjectFile("--open " + quoted) == juce::File()); // a flag's argument, not a bare path
    CHECK(launchProjectFile("\"" + folder.getChildFile("gone.northlight").getFullPathName() + "\"") ==
          juce::File());
    CHECK(launchProjectFile("\"" + folder.getChildFile("notes.txt").getFullPathName() + "\"") ==
          juce::File());

    CHECK_FALSE(hasDeveloperFlags(quoted));
    CHECK_FALSE(hasDeveloperFlags(""));
    CHECK(hasDeveloperFlags("--offscreen --capture out 4"));
    CHECK(hasDeveloperFlags(quoted + " --no-splash"));
    folder.deleteRecursively();
}
