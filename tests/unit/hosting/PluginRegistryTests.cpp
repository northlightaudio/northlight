#include "hosting/PluginRegistry.h"

#include <catch2/catch_test_macros.hpp>

using namespace aural::hosting;

namespace
{
PluginInfo plugin(const char* id, const char* name, const char* path)
{
    PluginInfo p;
    p.id = id;
    p.name = name;
    p.manufacturer = "Maker";
    p.category = "Fx";
    p.version = "1.2";
    p.path = path;
    p.uniqueId = 42;
    p.deprecatedUid = 7;
    p.numInputs = 2;
    p.numOutputs = 2;
    return p;
}
} // namespace

TEST_CASE("A scanned file's plugins are listed by name and found by insert type", "[hosting][registry]")
{
    PluginRegistry r;
    r.setFileResults(
        "C:/VST3/b.vst3", 100, {plugin("VST3-zeta", "Zeta", ""), plugin("VST3-alpha", "Alpha", "")});
    r.setFileResults("C:/VST3/a.vst3", 200, {plugin("VST3-mid", "Mid", "")});
    REQUIRE(r.plugins().size() == 3);
    CHECK(r.plugins()[0].name == "Alpha");
    CHECK(r.plugins()[1].name == "Mid");
    CHECK(r.plugins()[2].name == "Zeta");
    CHECK(r.plugins()[0].path == "C:/VST3/b.vst3"); // the file path is stamped on each
    const auto* found = r.findByType("vst3:VST3-mid");
    REQUIRE(found != nullptr);
    CHECK(found->path == "C:/VST3/a.vst3");
    CHECK(PluginRegistry::typeFor(*found) == "vst3:VST3-mid");
    CHECK(r.findByType("aural.eq") == nullptr);
    CHECK(r.findByType("vst3:nope") == nullptr);
    CHECK(PluginRegistry::isHostedType("vst3:x"));
    CHECK_FALSE(PluginRegistry::isHostedType("aural.gain"));

    // Rescanning a file replaces what it held before.
    r.setFileResults("C:/VST3/b.vst3", 300, {plugin("VST3-zeta", "Zeta 2", "")});
    REQUIRE(r.plugins().size() == 2);
    CHECK(r.find("VST3-alpha") == nullptr);
    r.removeFile("C:/VST3/b.vst3");
    REQUIRE(r.plugins().size() == 1);
}

TEST_CASE("Only new or changed files need a scan, and blocked files never do", "[hosting][registry]")
{
    PluginRegistry r;
    CHECK(r.needsScan("C:/VST3/a.vst3", 100));
    r.setFileResults("C:/VST3/a.vst3", 100, {});
    CHECK_FALSE(r.needsScan("C:/VST3/a.vst3", 100));
    CHECK(r.needsScan("C:/VST3/a.vst3", 101)); // modified since

    r.setFileResults("C:/VST3/bad.vst3", 5, {plugin("VST3-bad", "Bad", "")});
    r.block("C:/VST3/bad.vst3", "timed out");
    CHECK(r.isBlocked("C:/VST3/bad.vst3"));
    CHECK(r.find("VST3-bad") == nullptr); // its plugins are gone
    CHECK_FALSE(r.needsScan("C:/VST3/bad.vst3", 999));
    r.clearBlocklist();
    CHECK(r.needsScan("C:/VST3/bad.vst3", 999));
    // A successful scan of a formerly blocked file unblocks it.
    r.block("C:/VST3/bad.vst3", "crashed");
    r.setFileResults("C:/VST3/bad.vst3", 6, {});
    CHECK_FALSE(r.isBlocked("C:/VST3/bad.vst3"));
}

TEST_CASE("The registry round-trips through JSON and rejects other files", "[hosting][registry]")
{
    PluginRegistry r;
    r.setFileResults(
        "C:/VST3/a.vst3", 123456789012LL, {plugin("VST3-a", "A", ""), plugin("VST3-b", "B", "")});
    r.block("C:/VST3/bad.vst3", "timed out");
    const auto text = r.toJson();
    std::string error;
    const auto back = PluginRegistry::fromJson(text, &error);
    REQUIRE(back.has_value());
    CHECK(*back == r);
    CHECK(back->blocked().at("C:/VST3/bad.vst3") == "timed out");
    CHECK_FALSE(back->needsScan("C:/VST3/a.vst3", 123456789012LL));

    CHECK_FALSE(PluginRegistry::fromJson("not json", &error).has_value());
    CHECK(error == "not valid JSON");
    CHECK_FALSE(PluginRegistry::fromJson(R"({"format":"aural.project"})", &error).has_value());
    CHECK_FALSE(PluginRegistry::fromJson(R"({"format":"aural.plugins","version":2})", &error).has_value());
    const auto empty = PluginRegistry::fromJson(R"({"format":"aural.plugins","version":1})", &error);
    REQUIRE(empty.has_value());
    CHECK(empty->plugins().empty());
}
