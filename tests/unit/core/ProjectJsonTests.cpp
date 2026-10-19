#include "core/ProjectJson.h"

#include <catch2/catch_test_macros.hpp>

using namespace aural::core;

namespace
{
Project sampleProject()
{
    Project p;
    p.name = "Song";
    p.sampleRate = 44100.0;
    p.tempoBpm = 97.5;
    p.timeSignature = {3, 4};
    Track t;
    t.id = 1;
    t.name = "Drums";
    t.gain = 0.8f;
    t.pan = 0.1f;
    t.mute = true;
    t.clips.push_back({2, "kick", "media/kick.wav", 0, 44100, 0, 1.0f});
    t.clips.push_back({3, "snare", "media/snare.wav", 88200, 22050, 100, 0.5f});
    p.tracks.push_back(t);
    Track u;
    u.id = 4;
    u.name = "Bass";
    u.solo = true;
    u.inputChannel = 2;
    u.armed = true;
    u.monitor = MonitorMode::on;
    u.phaseInvert = true;
    u.colour = 0xffaa5500u;
    u.outputBus = 5;
    u.sends.push_back({6, 0.3f, true});
    p.tracks.push_back(u);
    Bus drums;
    drums.id = 5;
    drums.name = "Drum bus";
    drums.gain = 0.9f;
    drums.sends.push_back({6, 0.5f, false});
    p.buses.push_back(drums);
    Bus verb;
    verb.id = 6;
    verb.name = "Reverb";
    p.buses.push_back(verb);
    p.groups.push_back(Group{7, "Rhythm", 0xff22aa66u});
    p.buses[0].inserts.push_back(Insert{8, "vst3:VST3-abc", false, {}, "AAECAw==", 0.4f});
    p.nextId = 9;
    p.tracks[0].group = 7;
    p.buses[0].group = 7;
    p.master.gain = 0.8f;
    return p;
}
} // namespace

TEST_CASE("A project round-trips through JSON unchanged", "[core][json]")
{
    const auto original = sampleProject();
    const auto text = projectToJson(original);
    std::string error;
    const auto restored = projectFromJson(text, &error);
    REQUIRE(restored.has_value());
    CHECK(error.empty());
    CHECK(*restored == original);
}

TEST_CASE("Compact and pretty JSON parse the same", "[core][json]")
{
    const auto original = sampleProject();
    const auto compact = projectFromJson(projectToJson(original, false));
    REQUIRE(compact.has_value());
    CHECK(*compact == original);
}

TEST_CASE("Invalid text, wrong format and future versions are rejected with a reason", "[core][json]")
{
    std::string error;
    CHECK_FALSE(projectFromJson("not json {", &error).has_value());
    CHECK(error == "not valid JSON");

    CHECK_FALSE(projectFromJson(R"({"format":"something.else","version":1})", &error).has_value());
    CHECK(error == "not an Aural project file");

    CHECK_FALSE(projectFromJson(R"({"format":"aural.project","version":99})", &error).has_value());
    CHECK(error == "unsupported project version 99");
}

TEST_CASE("Missing optional fields take defaults and nextId is repaired", "[core][json]")
{
    const auto p = projectFromJson(R"({
        "format": "aural.project",
        "version": 1,
        "tracks": [ { "id": 10, "clips": [ { "id": 42, "media": "x.wav", "length": 5 } ] } ]
    })");
    REQUIRE(p.has_value());
    CHECK(p->name == "Untitled");
    CHECK(p->sampleRate == 48000.0);
    CHECK(p->tempoBpm == 120.0);
    CHECK(p->timeSignature == TimeSignature{4, 4});
    REQUIRE(p->tracks.size() == 1);
    CHECK(p->tracks[0].gain == 1.0f);
    REQUIRE(p->tracks[0].clips.size() == 1);
    CHECK(p->tracks[0].clips[0].id == 42);
    CHECK(p->tracks[0].clips[0].start == 0);
    CHECK(p->nextId == 43);
}

TEST_CASE("Wrongly typed fields are reported rather than crashing", "[core][json]")
{
    std::string error;
    const auto p = projectFromJson(R"({"format":"aural.project","version":1,"tempo":"fast"})", &error);
    CHECK_FALSE(p.has_value());
    CHECK(error.starts_with("malformed project"));
}
