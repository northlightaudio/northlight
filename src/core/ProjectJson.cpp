#include "core/ProjectJson.h"

#include <nlohmann/json.hpp>

namespace aural::core
{

namespace
{
using json = nlohmann::json;

json clipToJson(const AudioClip& c)
{
    return json{{"id", c.id},
                {"name", c.name},
                {"media", c.mediaPath},
                {"start", c.start},
                {"length", c.length},
                {"sourceOffset", c.sourceOffset},
                {"gain", c.gain}};
}

AudioClip clipFromJson(const json& j)
{
    AudioClip c;
    c.id = j.value("id", invalidId);
    c.name = j.value("name", std::string{});
    c.mediaPath = j.value("media", std::string{});
    c.start = j.value("start", SamplePosition{0});
    c.length = j.value("length", SamplePosition{0});
    c.sourceOffset = j.value("sourceOffset", SamplePosition{0});
    c.gain = j.value("gain", 1.0f);
    return c;
}

json trackToJson(const Track& t)
{
    json clips = json::array();
    for (const auto& c : t.clips)
        clips.push_back(clipToJson(c));
    const char* monitor = t.monitor == MonitorMode::off  ? "off"
                          : t.monitor == MonitorMode::on ? "on"
                                                         : "auto";
    return json{{"id", t.id},
                {"name", t.name},
                {"gain", t.gain},
                {"pan", t.pan},
                {"mute", t.mute},
                {"solo", t.solo},
                {"input", t.inputChannel},
                {"armed", t.armed},
                {"monitor", monitor},
                {"clips", std::move(clips)}};
}

Track trackFromJson(const json& j)
{
    Track t;
    t.id = j.value("id", invalidId);
    t.name = j.value("name", std::string{});
    t.gain = j.value("gain", 1.0f);
    t.pan = j.value("pan", 0.0f);
    t.mute = j.value("mute", false);
    t.solo = j.value("solo", false);
    t.inputChannel = j.value("input", -1);
    t.armed = j.value("armed", false);
    const auto monitor = j.value("monitor", std::string{"auto"});
    t.monitor = monitor == "off"  ? MonitorMode::off
                : monitor == "on" ? MonitorMode::on
                                  : MonitorMode::automatic;
    if (const auto it = j.find("clips"); it != j.end() && it->is_array())
        for (const auto& c : *it)
            t.clips.push_back(clipFromJson(c));
    t.sortClips();
    return t;
}
} // namespace

std::string projectToJson(const Project& p, bool pretty)
{
    json tracks = json::array();
    for (const auto& t : p.tracks)
        tracks.push_back(trackToJson(t));

    const json j{
        {"format", "aural.project"},
        {"version", Project::schemaVersion},
        {"name", p.name},
        {"sampleRate", p.sampleRate},
        {"tempo", p.tempoBpm},
        {"timeSignature",
         json{{"numerator", p.timeSignature.numerator}, {"denominator", p.timeSignature.denominator}}},
        {"nextId", p.nextId},
        {"tracks", std::move(tracks)}};
    return pretty ? j.dump(2) : j.dump();
}

std::optional<Project> projectFromJson(const std::string& text, std::string* error)
{
    const auto fail = [error](const std::string& message) -> std::optional<Project>
    {
        if (error != nullptr)
            *error = message;
        return std::nullopt;
    };

    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return fail("not valid JSON");
    if (j.value("format", std::string{}) != "aural.project")
        return fail("not an Aural project file");
    const int version = j.value("version", 0);
    if (version < 1 || version > Project::schemaVersion)
        return fail("unsupported project version " + std::to_string(version));

    try
    {
        Project p;
        p.name = j.value("name", std::string{"Untitled"});
        p.sampleRate = j.value("sampleRate", 48000.0);
        p.tempoBpm = j.value("tempo", 120.0);
        if (const auto it = j.find("timeSignature"); it != j.end() && it->is_object())
        {
            p.timeSignature.numerator = it->value("numerator", 4);
            p.timeSignature.denominator = it->value("denominator", 4);
        }
        p.nextId = j.value("nextId", Id{1});
        if (const auto it = j.find("tracks"); it != j.end() && it->is_array())
            for (const auto& t : *it)
                p.tracks.push_back(trackFromJson(t));

        // Guarantee ids stay unique even if the file's nextId is stale.
        Id maxId = 0;
        for (const auto& t : p.tracks)
        {
            maxId = std::max(maxId, t.id);
            for (const auto& c : t.clips)
                maxId = std::max(maxId, c.id);
        }
        p.nextId = std::max(p.nextId, maxId + 1);
        return p;
    }
    catch (const json::exception& e)
    {
        return fail(std::string("malformed project: ") + e.what());
    }
}

} // namespace aural::core
