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

void channelToJson(json& j, const Channel& c)
{
    j["id"] = c.id;
    j["name"] = c.name;
    j["gain"] = c.gain;
    j["pan"] = c.pan;
    j["mute"] = c.mute;
    j["solo"] = c.solo;
    if (c.phaseInvert)
        j["phaseInvert"] = true;
    if (c.colour != 0)
        j["colour"] = c.colour;
    if (c.outputBus != invalidId)
        j["output"] = c.outputBus;
    if (!c.sends.empty())
    {
        json sends = json::array();
        for (const auto& s : c.sends)
            sends.push_back(json{{"bus", s.bus}, {"level", s.level}, {"pre", s.preFader}});
        j["sends"] = std::move(sends);
    }
    if (!c.inserts.empty())
    {
        json inserts = json::array();
        for (const auto& i : c.inserts)
            inserts.push_back(
                json{{"id", i.id}, {"type", i.type}, {"bypass", i.bypass}, {"params", i.params}});
        j["inserts"] = std::move(inserts);
    }
}

void channelFromJson(const json& j, Channel& c)
{
    c.id = j.value("id", invalidId);
    c.name = j.value("name", std::string{});
    c.gain = j.value("gain", 1.0f);
    c.pan = j.value("pan", 0.0f);
    c.mute = j.value("mute", false);
    c.solo = j.value("solo", false);
    c.phaseInvert = j.value("phaseInvert", false);
    c.colour = j.value("colour", std::uint32_t{0});
    c.outputBus = j.value("output", invalidId);
    if (const auto it = j.find("sends"); it != j.end() && it->is_array())
        for (const auto& s : *it)
            c.sends.push_back(Send{s.value("bus", invalidId), s.value("level", 1.0f), s.value("pre", false)});
    if (const auto it = j.find("inserts"); it != j.end() && it->is_array())
        for (const auto& i : *it)
        {
            Insert insert;
            insert.id = i.value("id", invalidId);
            insert.type = i.value("type", std::string{});
            insert.bypass = i.value("bypass", false);
            if (const auto params = i.find("params"); params != i.end() && params->is_array())
                for (const auto& v : *params)
                    insert.params.push_back(v.get<float>());
            c.inserts.push_back(std::move(insert));
        }
}

json trackToJson(const Track& t)
{
    json clips = json::array();
    for (const auto& c : t.clips)
        clips.push_back(clipToJson(c));
    const char* monitor = t.monitor == MonitorMode::off  ? "off"
                          : t.monitor == MonitorMode::on ? "on"
                                                         : "auto";
    json j;
    channelToJson(j, t);
    j["input"] = t.inputChannel;
    j["armed"] = t.armed;
    j["monitor"] = monitor;
    j["clips"] = std::move(clips);
    return j;
}

json busToJson(const Bus& b)
{
    json j;
    channelToJson(j, b);
    return j;
}

Bus busFromJson(const json& j)
{
    Bus b;
    channelFromJson(j, b);
    return b;
}

Track trackFromJson(const json& j)
{
    Track t;
    channelFromJson(j, t);
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
    json buses = json::array();
    for (const auto& b : p.buses)
        buses.push_back(busToJson(b));

    const json j{
        {"format", "aural.project"},
        {"version", Project::schemaVersion},
        {"name", p.name},
        {"sampleRate", p.sampleRate},
        {"tempo", p.tempoBpm},
        {"timeSignature",
         json{{"numerator", p.timeSignature.numerator}, {"denominator", p.timeSignature.denominator}}},
        {"nextId", p.nextId},
        {"master", json{{"gain", p.master.gain}}},
        {"buses", std::move(buses)},
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
        if (const auto it = j.find("buses"); it != j.end() && it->is_array())
            for (const auto& b : *it)
                p.buses.push_back(busFromJson(b));
        if (const auto it = j.find("master"); it != j.end() && it->is_object())
            p.master.gain = it->value("gain", 1.0f);

        // Guarantee ids stay unique even if the file's nextId is stale.
        Id maxId = 0;
        for (const auto& t : p.tracks)
        {
            maxId = std::max(maxId, t.id);
            for (const auto& c : t.clips)
                maxId = std::max(maxId, c.id);
        }
        for (const auto& b : p.buses)
        {
            maxId = std::max(maxId, b.id);
            for (const auto& i : b.inserts)
                maxId = std::max(maxId, i.id);
        }
        for (const auto& t : p.tracks)
            for (const auto& i : t.inserts)
                maxId = std::max(maxId, i.id);
        p.nextId = std::max(p.nextId, maxId + 1);
        return p;
    }
    catch (const json::exception& e)
    {
        return fail(std::string("malformed project: ") + e.what());
    }
}

} // namespace aural::core
