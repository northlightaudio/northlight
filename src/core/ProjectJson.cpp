#include "core/ProjectJson.h"

#include <nlohmann/json.hpp>

namespace aural::core
{

namespace
{
using json = nlohmann::json;

json clipToJson(const AudioClip& c)
{
    json j{{"id", c.id},
           {"name", c.name},
           {"media", c.mediaPath},
           {"start", c.start},
           {"length", c.length},
           {"sourceOffset", c.sourceOffset},
           {"gain", c.gain}};
    if (c.fadeIn > 0)
        j["fadeIn"] = c.fadeIn;
    if (c.fadeOut > 0)
        j["fadeOut"] = c.fadeOut;
    return j;
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
    c.fadeIn = j.value("fadeIn", SamplePosition{0});
    c.fadeOut = j.value("fadeOut", SamplePosition{0});
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
    if (c.group != invalidId)
        j["group"] = c.group;
    if (!c.sends.empty())
    {
        json sends = json::array();
        for (const auto& s : c.sends)
            sends.push_back(json{{"bus", s.bus}, {"level", s.level}, {"pre", s.preFader}});
        j["sends"] = std::move(sends);
    }
    const auto insertsToJson = [](const std::vector<Insert>& list)
    {
        json inserts = json::array();
        for (const auto& i : list)
        {
            json ij{{"id", i.id}, {"type", i.type}, {"bypass", i.bypass}, {"params", i.params}};
            if (!i.state.empty())
                ij["state"] = i.state;
            if (i.mix != 1.0f)
                ij["mix"] = i.mix;
            if (i.sidechain != invalidId)
                ij["sidechain"] = i.sidechain;
            inserts.push_back(std::move(ij));
        }
        return inserts;
    };
    if (!c.inserts.empty())
        j["inserts"] = insertsToJson(c.inserts);
    if (!c.strip.empty() || c.stripPreInserts)
        j["strip"] = json{{"pre", c.stripPreInserts}, {"modules", insertsToJson(c.strip)}};
    if (!c.automation.empty() || c.automationMode != AutomationMode::read)
    {
        static const char* modes[] = {"off", "read", "touch", "latch", "write"};
        static const char* targets[] = {"gain", "pan", "parameter"};
        json lanes = json::array();
        for (const auto& lane : c.automation)
        {
            json points = json::array();
            for (const auto& point : lane.points)
                points.push_back(json::array({point.position, point.value}));
            json lj{{"id", lane.id},
                    {"target", targets[static_cast<int>(lane.target)]},
                    {"points", std::move(points)}};
            if (lane.target == AutomationTarget::insertParameter)
            {
                lj["insert"] = lane.insert;
                lj["parameter"] = lane.parameter;
            }
            lanes.push_back(std::move(lj));
        }
        j["automation"] =
            json{{"mode", modes[static_cast<int>(c.automationMode)]}, {"lanes", std::move(lanes)}};
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
    c.group = j.value("group", invalidId);
    if (const auto it = j.find("sends"); it != j.end() && it->is_array())
        for (const auto& s : *it)
            c.sends.push_back(Send{s.value("bus", invalidId), s.value("level", 1.0f), s.value("pre", false)});
    const auto insertsFromJson = [](const json& list, std::vector<Insert>& into)
    {
        for (const auto& i : list)
        {
            Insert insert;
            insert.id = i.value("id", invalidId);
            insert.type = i.value("type", std::string{});
            insert.bypass = i.value("bypass", false);
            insert.state = i.value("state", std::string{});
            insert.mix = i.value("mix", 1.0f);
            insert.sidechain = i.value("sidechain", invalidId);
            if (const auto params = i.find("params"); params != i.end() && params->is_array())
                for (const auto& v : *params)
                    insert.params.push_back(v.get<float>());
            into.push_back(std::move(insert));
        }
    };
    if (const auto it = j.find("inserts"); it != j.end() && it->is_array())
        insertsFromJson(*it, c.inserts);
    if (const auto it = j.find("strip"); it != j.end() && it->is_object())
    {
        c.stripPreInserts = it->value("pre", false);
        if (const auto modules = it->find("modules"); modules != it->end() && modules->is_array())
            insertsFromJson(*modules, c.strip);
    }
    if (const auto it = j.find("automation"); it != j.end() && it->is_object())
    {
        const auto mode = it->value("mode", std::string("read"));
        c.automationMode = mode == "off"     ? AutomationMode::off
                           : mode == "touch" ? AutomationMode::touch
                           : mode == "latch" ? AutomationMode::latch
                           : mode == "write" ? AutomationMode::write
                                             : AutomationMode::read;
        if (const auto lanes = it->find("lanes"); lanes != it->end() && lanes->is_array())
            for (const auto& l : *lanes)
            {
                AutomationLane lane;
                lane.id = l.value("id", invalidId);
                const auto target = l.value("target", std::string("gain"));
                lane.target = target == "pan"         ? AutomationTarget::pan
                              : target == "parameter" ? AutomationTarget::insertParameter
                                                      : AutomationTarget::gain;
                lane.insert = l.value("insert", invalidId);
                lane.parameter = l.value("parameter", 0);
                if (const auto points = l.find("points"); points != l.end() && points->is_array())
                    for (const auto& pt : *points)
                        if (pt.is_array() && pt.size() >= 2)
                            lane.points.push_back({pt[0].get<SamplePosition>(), pt[1].get<float>()});
                std::stable_sort(lane.points.begin(),
                                 lane.points.end(),
                                 [](const AutomationPoint& a, const AutomationPoint& b)
                                 { return a.position < b.position; });
                c.automation.push_back(std::move(lane));
            }
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
    if (t.kind == TrackKind::midi)
        j["kind"] = "midi";
    else if (t.kind == TrackKind::aux)
    {
        j["kind"] = "aux";
        j["aux"] = json{{"track", t.auxTrack}, {"insert", t.auxInsert}, {"output", t.auxOutput}};
    }
    j["input"] = t.inputChannel;
    if (!t.midiInput.empty())
        j["midiInput"] = t.midiInput;
    j["armed"] = t.armed;
    j["monitor"] = monitor;
    j["clips"] = std::move(clips);
    if (!t.midiClips.empty())
    {
        json midiClips = json::array();
        for (const auto& c : t.midiClips)
        {
            json notes = json::array();
            for (const auto& n : c.notes)
                notes.push_back(json::array({n.id, n.note, n.velocity, n.start, n.length}));
            midiClips.push_back(json{{"id", c.id},
                                     {"name", c.name},
                                     {"start", c.start},
                                     {"length", c.length},
                                     {"notes", std::move(notes)}});
        }
        j["midiClips"] = std::move(midiClips);
    }
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
    const auto kind = j.value("kind", std::string{"audio"});
    t.kind = kind == "midi" ? TrackKind::midi : kind == "aux" ? TrackKind::aux : TrackKind::audio;
    if (const auto aux = j.find("aux"); aux != j.end() && aux->is_object())
    {
        t.auxTrack = aux->value("track", invalidId);
        t.auxInsert = aux->value("insert", invalidId);
        t.auxOutput = aux->value("output", 0);
    }
    t.midiInput = j.value("midiInput", std::string{});
    if (const auto it = j.find("midiClips"); it != j.end() && it->is_array())
    {
        for (const auto& cj : *it)
        {
            MidiClip c;
            c.id = cj.value("id", invalidId);
            c.name = cj.value("name", std::string{});
            c.start = cj.value("start", SamplePosition{0});
            c.length = cj.value("length", SamplePosition{0});
            if (const auto notes = cj.find("notes"); notes != cj.end() && notes->is_array())
                for (const auto& nj : *notes)
                    if (nj.is_array() && nj.size() == 5)
                        c.notes.push_back(MidiNote{nj[0].get<Id>(),
                                                   nj[1].get<int>(),
                                                   nj[2].get<int>(),
                                                   nj[3].get<SamplePosition>(),
                                                   nj[4].get<SamplePosition>()});
            c.sortNotes();
            t.midiClips.push_back(std::move(c));
        }
        t.sortMidiClips();
    }
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
    json groups = json::array();
    for (const auto& g : p.groups)
    {
        json gj{{"id", g.id}, {"name", g.name}};
        if (g.colour != 0)
            gj["colour"] = g.colour;
        groups.push_back(std::move(gj));
    }

    json markers = json::array();
    for (const auto& m : p.markers)
    {
        json mj{{"id", m.id}, {"name", m.name}, {"position", m.position}};
        if (m.length > 0)
            mj["length"] = m.length;
        markers.push_back(std::move(mj));
    }

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
        {"groups", std::move(groups)},
        {"markers", std::move(markers)},
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
        if (const auto it = j.find("groups"); it != j.end() && it->is_array())
            for (const auto& g : *it)
                p.groups.push_back(Group{g.value("id", invalidId),
                                         g.value("name", std::string{}),
                                         g.value("colour", std::uint32_t{0})});
        if (const auto it = j.find("markers"); it != j.end() && it->is_array())
            for (const auto& m : *it)
                p.markers.push_back(Marker{m.value("id", invalidId),
                                           m.value("name", std::string{}),
                                           m.value("position", SamplePosition{0}),
                                           m.value("length", SamplePosition{0})});
        p.sortMarkers();
        if (const auto it = j.find("master"); it != j.end() && it->is_object())
            p.master.gain = it->value("gain", 1.0f);

        // Guarantee ids stay unique even if the file's nextId is stale, and give a
        // duplicated clip or insert id (a hand-edited or corrupted file) a fresh one
        // rather than letting two objects share one processor or clip identity.
        Id maxId = 0;
        for (const auto& t : p.tracks)
        {
            maxId = std::max(maxId, t.id);
            for (const auto& c : t.clips)
                maxId = std::max(maxId, c.id);
            for (const auto& c : t.midiClips)
            {
                maxId = std::max(maxId, c.id);
                for (const auto& n : c.notes)
                    maxId = std::max(maxId, n.id);
            }
        }
        for (const auto& b : p.buses)
        {
            maxId = std::max(maxId, b.id);
            for (const auto& i : b.inserts)
                maxId = std::max(maxId, i.id);
            for (const auto& i : b.strip)
                maxId = std::max(maxId, i.id);
            for (const auto& l : b.automation)
                maxId = std::max(maxId, l.id);
        }
        for (const auto& t : p.tracks)
        {
            for (const auto& i : t.inserts)
                maxId = std::max(maxId, i.id);
            for (const auto& i : t.strip)
                maxId = std::max(maxId, i.id);
            for (const auto& l : t.automation)
                maxId = std::max(maxId, l.id);
        }
        for (const auto& g : p.groups)
            maxId = std::max(maxId, g.id);
        for (const auto& m : p.markers)
            maxId = std::max(maxId, m.id);
        p.nextId = std::max(p.nextId, maxId + 1);
        std::vector<Id> seen;
        const auto fresh = [&](Id& id)
        {
            if (id == invalidId || std::find(seen.begin(), seen.end(), id) != seen.end())
                id = p.allocateId();
            seen.push_back(id);
        };
        for (auto& t : p.tracks)
        {
            fresh(t.id);
            for (auto& c : t.clips)
                fresh(c.id);
            for (auto& c : t.midiClips)
            {
                fresh(c.id);
                for (auto& n : c.notes)
                    fresh(n.id);
            }
            for (auto& i : t.inserts)
                fresh(i.id);
            for (auto& i : t.strip)
                fresh(i.id);
            for (auto& l : t.automation)
                fresh(l.id);
        }
        for (auto& b : p.buses)
        {
            fresh(b.id);
            for (auto& i : b.inserts)
                fresh(i.id);
            for (auto& i : b.strip)
                fresh(i.id);
            for (auto& l : b.automation)
                fresh(l.id);
        }
        for (auto& g : p.groups)
            fresh(g.id);
        for (auto& m : p.markers)
            fresh(m.id);
        return p;
    }
    catch (const json::exception& e)
    {
        return fail(std::string("malformed project: ") + e.what());
    }
}

} // namespace aural::core
