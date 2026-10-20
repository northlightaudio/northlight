#include "core/ProjectJson.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

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
    t.clips.push_back({3, "snare", "media/snare.wav", 88200, 22050, 100, 0.5f, 441, 882});
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
    u.height = 96; // a row resized in the arrange view
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
    verb.height = 40;
    p.buses.push_back(verb);
    p.groups.push_back(Group{7, "Rhythm", 0xff22aa66u});
    Track keys;
    keys.id = 11;
    keys.name = "Keys";
    keys.kind = TrackKind::midi;
    keys.midiInput = "Keystation";
    MidiClip riff;
    riff.id = 12;
    riff.name = "riff";
    riff.start = 44100;
    riff.length = 22050;
    riff.notes.push_back({13, 60, 100, 0, 11025});
    riff.notes.push_back({14, 64, 90, 11025, 11025});
    keys.midiClips.push_back(riff);
    p.tracks.push_back(keys);
    p.markers.push_back(Marker{9, "Intro", 0, 44100});
    p.markers.push_back(Marker{10, "Drop", 88200, 0});
    p.buses[0].inserts.push_back(Insert{8, "vst3:VST3-abc", false, {}, "AAECAw==", 0.4f, 1});
    p.nextId = 15;
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
    CHECK(error == "made by a newer version of Aural (project version 99)");
}

TEST_CASE("Missing optional fields take defaults and nextId is repaired", "[core][json]")
{
    const auto p = projectFromJson(R"({
        "format": "aural.project",
        "version": 2,
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
    const auto p = projectFromJson(R"({"format":"aural.project","version":2,"tempo":"fast"})", &error);
    CHECK_FALSE(p.has_value());
    CHECK(error.starts_with("malformed project"));
}

TEST_CASE("An aux track's source survives a JSON round trip", "[core][json][aux]")
{
    Project p;
    Track keys;
    keys.id = 1;
    keys.kind = TrackKind::midi;
    Insert synth;
    synth.id = 2;
    synth.type = "vst3:some-instrument";
    keys.inserts.push_back(synth);
    Track aux;
    aux.id = 3;
    aux.kind = TrackKind::aux;
    aux.name = "Keys out 2";
    aux.auxTrack = 1;
    aux.auxInsert = 2;
    aux.auxOutput = 1;
    p.tracks = {keys, aux};
    p.nextId = 10;
    const auto text = projectToJson(p);
    const auto back = projectFromJson(text);
    REQUIRE(back.has_value());
    REQUIRE(back->tracks.size() == 2);
    CHECK(back->tracks[1].isAux());
    CHECK(back->tracks[1].auxTrack == 1);
    CHECK(back->tracks[1].auxInsert == 2);
    CHECK(back->tracks[1].auxOutput == 1);
    CHECK(*back == p);
}

TEST_CASE("A track's instrument round-trips through JSON", "[core][json][instrument]")
{
    Project p;
    Track t;
    t.id = p.allocateId();
    t.kind = TrackKind::instrument;
    t.instrument.id = p.allocateId();
    t.instrument.type = "aural.aurora";
    t.instrument.params = {0.25f, 3.0f};
    t.instrument.state = "abc";
    p.tracks.push_back(t);
    const auto loaded = projectFromJson(projectToJson(p));
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->tracks.size() == 1);
    CHECK(loaded->tracks[0].instrument == p.tracks[0].instrument);
    CHECK(loaded->nextId > t.instrument.id);
}

TEST_CASE("MIDI tracks keep their target and channel; older project versions are refused",
          "[core][json][instrument]")
{
    Project p;
    Track channel;
    channel.id = p.allocateId();
    channel.kind = TrackKind::instrument;
    Track midi;
    midi.id = p.allocateId();
    midi.kind = TrackKind::midi;
    midi.midiTarget = channel.id;
    midi.midiChannel = 10;
    p.tracks = {midi, channel};
    const auto loaded = projectFromJson(projectToJson(p));
    REQUIRE(loaded.has_value());
    CHECK(loaded->tracks[0].midiTarget == channel.id);
    CHECK(loaded->tracks[0].midiChannel == 10);
    CHECK(loaded->tracks[1].isInstrument());

    std::string error;
    CHECK_FALSE(projectFromJson(R"({"format":"aural.project","version":1})", &error).has_value());
    CHECK(error.find("older version of Aural") != std::string::npos);
}

namespace
{
// A processor layout for the tests: keys and defaults by insert type.
class TestKeys final : public ParameterKeys
{
public:
    std::map<std::string, ParameterLayout> layouts;
    ParameterLayout layout(const Insert& insert) const override
    {
        const auto it = layouts.find(insert.type);
        return it == layouts.end() ? ParameterLayout{} : it->second;
    }
};

Project keyedProject()
{
    Project p;
    Track t;
    t.id = p.allocateId();
    Insert synth;
    synth.id = p.allocateId();
    synth.type = "test.synth";
    synth.params = {0.1f, 0.2f, 0.3f}; // cutoff, drive, level
    t.inserts.push_back(synth);
    AutomationLane cutoff;
    cutoff.id = p.allocateId();
    cutoff.target = AutomationTarget::insertParameter;
    cutoff.insert = synth.id;
    cutoff.parameter = 0;
    cutoff.points = {{0, 0.5f}};
    AutomationLane drive = cutoff;
    drive.id = p.allocateId();
    drive.parameter = 1;
    t.automation = {cutoff, drive};
    p.tracks.push_back(t);
    return p;
}
} // namespace

TEST_CASE("Parameters and lanes follow their keys when a processor reorders, adds and removes parameters",
          "[core][json][parameters]")
{
    TestKeys before;
    before.layouts["test.synth"] = {{"cutoff", "drive", "level"}, {1.0f, 0.0f, 1.0f}};
    const auto text = projectToJson(keyedProject(), true, &before);
    CHECK(text.find("\"paramKeys\"") != std::string::npos);
    CHECK(text.find("\"parameterKey\": \"drive\"") != std::string::npos);

    // A later version: "drive" removed, "resonance" added, the order changed.
    TestKeys after;
    after.layouts["test.synth"] = {{"level", "resonance", "cutoff"}, {1.0f, 0.7f, 1.0f}};
    auto loaded = projectFromJson(text);
    REQUIRE(loaded.has_value());
    CHECK(loaded->tracks[0].inserts[0].paramKeys.size() == 3); // kept until reconciled
    CHECK(reconcileParameters(*loaded, after));
    const auto& insert = loaded->tracks[0].inserts[0];
    CHECK(insert.params == std::vector<float>{0.3f, 0.7f, 0.1f}); // level, the new default, cutoff
    CHECK(insert.paramKeys.empty());
    REQUIRE(loaded->tracks[0].automation.size() == 1);     // the drive lane went with its parameter
    CHECK(loaded->tracks[0].automation[0].parameter == 2); // cutoff, now third
    CHECK(loaded->tracks[0].automation[0].parameterKey.empty());
    CHECK_FALSE(reconcileParameters(*loaded, after)); // settled
}

TEST_CASE("A processor not known yet keeps its keys through a re-save", "[core][json][parameters]")
{
    TestKeys before;
    before.layouts["test.synth"] = {{"cutoff", "drive", "level"}, {1.0f, 0.0f, 1.0f}};
    auto loaded = projectFromJson(projectToJson(keyedProject(), true, &before));
    REQUIRE(loaded.has_value());
    const TestKeys none; // the plugin is not installed here
    CHECK_FALSE(reconcileParameters(*loaded, none));
    auto again = projectFromJson(projectToJson(*loaded, true, &none));
    REQUIRE(again.has_value());
    CHECK(again->tracks[0].inserts[0].paramKeys == std::vector<std::string>{"cutoff", "drive", "level"});
    CHECK(again->tracks[0].automation[1].parameterKey == "drive");
}

TEST_CASE("Parameters without keys are positional, padded with defaults or trimmed",
          "[core][json][parameters]")
{
    auto p = keyedProject();
    p.tracks[0].inserts[0].params = {0.1f, 0.2f}; // an older layout, one short
    TestKeys keys;
    keys.layouts["test.synth"] = {{"cutoff", "drive", "level"}, {1.0f, 0.0f, 0.9f}};
    CHECK(reconcileParameters(p, keys));
    CHECK(p.tracks[0].inserts[0].params == std::vector<float>{0.1f, 0.2f, 0.9f});
    CHECK(p.tracks[0].automation.size() == 2);
}

TEST_CASE("A version 2 project is upgraded on load and saved as the current version", "[core][json]")
{
    auto text = projectToJson(keyedProject());
    const auto current = "\"version\": " + std::to_string(Project::schemaVersion);
    const auto at = text.find(current);
    REQUIRE(at != std::string::npos);
    text.replace(at, current.size(), "\"version\": 2");
    const auto loaded = projectFromJson(text);
    REQUIRE(loaded.has_value());
    CHECK(projectToJson(*loaded).find(current) != std::string::npos);
}
