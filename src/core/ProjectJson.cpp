#include "core/ProjectJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <unordered_set>

namespace northlight::core
{

namespace
{
using json = nlohmann::json;

// The key provider for the projectToJson call in progress (null: positions only).
thread_local const ParameterKeys* savingKeys = nullptr;

// The keys an insert is saved with: the processor's, else what the file had.
std::vector<std::string> keysFor(const Insert& insert)
{
    if (savingKeys != nullptr)
        if (auto layout = savingKeys->layout(insert); layout.keys.size() == insert.params.size())
            return std::move(layout.keys);
    return insert.paramKeys.size() == insert.params.size() ? insert.paramKeys : std::vector<std::string>{};
}

const Insert* findChannelInsert(const Channel& c, Id insertId)
{
    return const_cast<Channel&>(c).findInsert(insertId);
}

// Version upgrades, one step at a time from the file's version to the current
// one. 2 -> 3 only adds optional fields, so it has nothing to rewrite.
void migrate(json& j, int from)
{
    for (int version = from; version < Project::schemaVersion; ++version)
    {
        switch (version)
        {
            case 2: // 2 -> 3, ..., 6 -> 7 only add optional fields
            case 3:
            case 4:
            case 5:  // 6 adds the tempo track and clip stretch
            case 6:  // 7 adds take groups; a file without them has none (its clips stay as they are)
            case 7:  // 8 adds folder tracks and frozen tracks
            case 8:  // 9: MIDI clips gain an optional "controllers" array; a clip
                     // without one has no controller events, which is what older
                     // versions meant
            case 9:  // 10: tracks gain an optional "inputBus"; without one the
                     // track takes its single input channel, as before
            case 10: // 11: the master gains optional "inserts"; without them it has none
            case 11: // 12: lanes gain an optional "view" and the automation an optional
                     // "open"; without them a lane shows at the default height and
                     // the lanes start closed
                break;
            default:
                break;
        }
    }
    j["version"] = Project::schemaVersion;
}

const char* controllerTypeName(MidiControllerType type)
{
    switch (type)
    {
        case MidiControllerType::controller:
            return "cc";
        case MidiControllerType::pitchBend:
            return "bend";
        case MidiControllerType::channelPressure:
            return "pressure";
        case MidiControllerType::polyPressure:
            return "poly";
    }
    return "cc";
}

std::optional<MidiControllerType> controllerTypeFromName(const std::string& name)
{
    if (name == "cc")
        return MidiControllerType::controller;
    if (name == "bend")
        return MidiControllerType::pitchBend;
    if (name == "pressure")
        return MidiControllerType::channelPressure;
    if (name == "poly")
        return MidiControllerType::polyPressure;
    return std::nullopt;
}

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
    if (c.stretch != 1.0)
        j["stretch"] = c.stretch;
    if (c.transpose != 0.0)
        j["transpose"] = c.transpose;
    if (c.followTempo)
        j["followTempo"] = true;
    if (c.vocal.active())
    {
        json notes = json::array();
        for (const auto& n : c.vocal.notes)
        {
            json nj{
                {"id", n.id}, {"start", n.start}, {"end", n.end}, {"centre", n.centre}, {"target", n.target}};
            if (n.drift != 1.0f)
                nj["drift"] = n.drift;
            if (n.vibrato != 1.0f)
                nj["vibrato"] = n.vibrato;
            if (n.transitionIn > 0)
                nj["transitionIn"] = n.transitionIn;
            if (n.transitionOut > 0)
                nj["transitionOut"] = n.transitionOut;
            if (n.gainDb != 0.0f)
                nj["gainDb"] = n.gainDb;
            if (n.muted)
                nj["muted"] = true;
            notes.push_back(std::move(nj));
        }
        json segments = json::array();
        for (const auto& s : c.vocal.segments)
        {
            json sj{{"id", s.id},
                    {"start", s.start},
                    {"end", s.end},
                    {"kind",
                     s.kind == VocalSegmentKind::breath      ? "breath"
                     : s.kind == VocalSegmentKind::consonant ? "consonant"
                                                             : "silence"}};
            if (s.gainDb != 0.0f)
                sj["gainDb"] = s.gainDb;
            if (s.muted)
                sj["muted"] = true;
            segments.push_back(std::move(sj));
        }
        j["vocal"] = json{{"analysis", c.vocal.analysisVersion},
                          {"notes", std::move(notes)},
                          {"segments", std::move(segments)}};
    }
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
    c.stretch = std::clamp(j.value("stretch", 1.0), 0.05, 20.0);
    c.transpose = std::clamp(j.value("transpose", 0.0), -48.0, 48.0);
    c.followTempo = j.value("followTempo", false);
    if (const auto it = j.find("vocal"); it != j.end() && it->is_object())
    {
        c.vocal.analysisVersion = it->value("analysis", 0);
        if (const auto n = it->find("notes"); n != it->end() && n->is_array())
            for (const auto& nj : *n)
            {
                VocalNote note;
                note.id = nj.value("id", invalidId);
                note.start = nj.value("start", SamplePosition{0});
                note.end = nj.value("end", SamplePosition{0});
                note.centre = nj.value("centre", 6000.0f);
                note.target = nj.value("target", note.centre);
                note.drift = nj.value("drift", 1.0f);
                note.vibrato = nj.value("vibrato", 1.0f);
                note.transitionIn = nj.value("transitionIn", SamplePosition{0});
                note.transitionOut = nj.value("transitionOut", SamplePosition{0});
                note.gainDb = nj.value("gainDb", 0.0f);
                note.muted = nj.value("muted", false);
                c.vocal.notes.push_back(note);
            }
        if (const auto s = it->find("segments"); s != it->end() && s->is_array())
            for (const auto& sj : *s)
            {
                VocalSegment segment;
                segment.id = sj.value("id", invalidId);
                segment.start = sj.value("start", SamplePosition{0});
                segment.end = sj.value("end", SamplePosition{0});
                const auto kind = sj.value("kind", std::string{"silence"});
                segment.kind = kind == "breath"      ? VocalSegmentKind::breath
                               : kind == "consonant" ? VocalSegmentKind::consonant
                                                     : VocalSegmentKind::silence;
                segment.gainDb = sj.value("gainDb", 0.0f);
                segment.muted = sj.value("muted", false);
                c.vocal.segments.push_back(segment);
            }
    }
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
    if (c.height > 0)
        j["height"] = c.height;
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
            if (auto keys = keysFor(i); !keys.empty())
                ij["paramKeys"] = std::move(keys);
            if (!i.state.empty())
                ij["state"] = i.state;
            if (i.mix != 1.0f)
                ij["mix"] = i.mix;
            if (i.sidechain != invalidId)
                ij["sidechain"] = i.sidechain;
            if (i.sidechain != invalidId && i.sidechainPostFader)
                ij["sidechainPostFader"] = true;
            inserts.push_back(std::move(ij));
        }
        return inserts;
    };
    if (c.hasInstrument())
        j["instrument"] = insertsToJson({c.instrument}).front();
    if (!c.inserts.empty())
        j["inserts"] = insertsToJson(c.inserts);
    if (!c.strip.empty() || c.stripPreInserts)
        j["strip"] = json{{"pre", c.stripPreInserts}, {"modules", insertsToJson(c.strip)}};
    if (!c.automation.empty() || c.automationMode != AutomationMode::read || c.automationOpen)
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
                std::string key = lane.parameterKey;
                if (const auto* insert = findChannelInsert(c, lane.insert); insert != nullptr && key.empty())
                    if (const auto keys = keysFor(*insert);
                        lane.parameter >= 0 && static_cast<size_t>(lane.parameter) < keys.size())
                        key = keys[static_cast<size_t>(lane.parameter)];
                if (!key.empty())
                    lj["parameterKey"] = std::move(key);
            }
            if (lane.view != AutomationLaneView{})
            {
                json view = json::object();
                if (lane.view.height > 0)
                    view["height"] = lane.view.height;
                if (lane.view.collapsed)
                    view["collapsed"] = true;
                if (lane.view.hidden)
                    view["hidden"] = true;
                lj["view"] = std::move(view);
            }
            lanes.push_back(std::move(lj));
        }
        j["automation"] =
            json{{"mode", modes[static_cast<int>(c.automationMode)]}, {"lanes", std::move(lanes)}};
        if (c.automationOpen)
            j["automation"]["open"] = true;
    }
}

// The master keeps only what it uses: its gain and its inserts.
json masterToJson(const Master& m)
{
    json full;
    channelToJson(full, m);
    json j{{"gain", m.gain}};
    if (const auto it = full.find("inserts"); it != full.end())
        j["inserts"] = *it;
    return j;
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
    c.height = std::max(0, j.value("height", 0));
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
            insert.type = currentTypeName(i.value("type", std::string{}));
            insert.bypass = i.value("bypass", false);
            insert.state = i.value("state", std::string{});
            insert.mix = i.value("mix", 1.0f);
            insert.sidechain = i.value("sidechain", invalidId);
            insert.sidechainPostFader = insert.sidechain != invalidId && i.value("sidechainPostFader", false);
            if (const auto params = i.find("params"); params != i.end() && params->is_array())
                for (const auto& v : *params)
                    insert.params.push_back(v.get<float>());
            if (const auto keys = i.find("paramKeys");
                keys != i.end() && keys->is_array() && keys->size() == insert.params.size())
                for (const auto& k : *keys)
                    insert.paramKeys.push_back(k.get<std::string>());
            into.push_back(std::move(insert));
        }
    };
    if (const auto it = j.find("instrument"); it != j.end() && it->is_object())
    {
        std::vector<Insert> one;
        insertsFromJson(json::array({*it}), one);
        c.instrument = one.front();
    }
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
        c.automationOpen = it->value("open", false);
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
                lane.parameterKey = l.value("parameterKey", std::string{});
                if (const auto view = l.find("view"); view != l.end() && view->is_object())
                {
                    lane.view.height = std::max(0, view->value("height", 0));
                    lane.view.collapsed = view->value("collapsed", false);
                    lane.view.hidden = view->value("hidden", false);
                }
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
    {
        j["kind"] = "midi";
        if (t.midiTarget != invalidId)
            j["target"] = t.midiTarget;
        j["midiChannel"] = t.midiChannel;
    }
    else if (t.kind == TrackKind::instrument)
        j["kind"] = "instrument";
    else if (t.kind == TrackKind::aux)
    {
        j["kind"] = "aux";
        j["aux"] = json{{"track", t.auxTrack}, {"insert", t.auxInsert}, {"output", t.auxOutput}};
    }
    else if (t.kind == TrackKind::folder)
    {
        j["kind"] = "folder";
        if (!t.folderOpen)
            j["folderOpen"] = false;
    }
    if (t.folder != invalidId)
        j["folder"] = t.folder;
    if (!t.frozenMedia.empty())
        j["frozen"] = t.frozenMedia;
    j["input"] = t.inputChannel;
    if (!t.inputBus.empty())
        j["inputBus"] = t.inputBus;
    if (!t.midiInput.empty())
        j["midiInput"] = t.midiInput;
    if (t.midiOut)
        j["midiOut"] = json{{"channel", t.midiOutChannel}};
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
            json clip{{"id", c.id},
                      {"name", c.name},
                      {"start", c.start},
                      {"length", c.length},
                      {"notes", std::move(notes)}};
            // Controller events as compact arrays: [id, type, number, value, time].
            if (!c.controllers.empty())
            {
                json controllers = json::array();
                for (const auto& e : c.controllers)
                    controllers.push_back(
                        json::array({e.id, controllerTypeName(e.type), e.number, e.value, e.time}));
                clip["controllers"] = std::move(controllers);
            }
            midiClips.push_back(std::move(clip));
        }
        j["midiClips"] = std::move(midiClips);
    }
    if (!t.takeGroups.empty())
    {
        json groups = json::array();
        for (const auto& g : t.takeGroups)
        {
            json takes = json::array();
            for (const auto& take : g.takes)
            {
                auto tj = clipToJson(take.clip);
                tj["number"] = take.number;
                takes.push_back(std::move(tj));
            }
            json comp = json::array();
            for (const auto& s : g.comp)
                comp.push_back(json::array({s.start, s.end, s.take}));
            groups.push_back(json{{"id", g.id},
                                  {"takes", std::move(takes)},
                                  {"comp", std::move(comp)},
                                  {"clips", g.clips},
                                  {"crossfade", g.crossfade},
                                  {"expanded", g.expanded}});
        }
        j["takeGroups"] = std::move(groups);
    }
    return j;
}

json busToJson(const Bus& b)
{
    json j;
    channelToJson(j, b);
    if (b.fx)
        j["fx"] = true;
    return j;
}

Bus busFromJson(const json& j)
{
    Bus b;
    channelFromJson(j, b);
    b.fx = j.value("fx", false);
    return b;
}

Track trackFromJson(const json& j)
{
    Track t;
    channelFromJson(j, t);
    const auto kind = j.value("kind", std::string{"audio"});
    t.kind = kind == "midi"         ? TrackKind::midi
             : kind == "instrument" ? TrackKind::instrument
             : kind == "aux"        ? TrackKind::aux
             : kind == "folder"     ? TrackKind::folder
                                    : TrackKind::audio;
    t.folder = j.value("folder", invalidId);
    t.folderOpen = j.value("folderOpen", true);
    t.frozenMedia = j.value("frozen", std::string{});
    t.midiTarget = j.value("target", invalidId);
    t.midiChannel = std::clamp(j.value("midiChannel", 1), 1, 16);
    if (const auto aux = j.find("aux"); aux != j.end() && aux->is_object())
    {
        t.auxTrack = aux->value("track", invalidId);
        t.auxInsert = aux->value("insert", invalidId);
        t.auxOutput = aux->value("output", 0);
    }
    t.midiInput = j.value("midiInput", std::string{});
    if (const auto out = j.find("midiOut"); out != j.end() && out->is_object())
    {
        t.midiOut = true;
        t.midiOutChannel = std::clamp(out->value("channel", 1), 1, 16);
    }
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
            if (const auto ctl = cj.find("controllers"); ctl != cj.end() && ctl->is_array())
                for (const auto& ej : *ctl)
                {
                    if (!ej.is_array() || ej.size() != 5 || !ej[1].is_string())
                        continue;
                    const auto type = controllerTypeFromName(ej[1].get<std::string>());
                    if (!type.has_value())
                        continue; // a kind this version does not know
                    MidiControllerEvent e;
                    e.id = ej[0].get<Id>();
                    e.type = *type;
                    e.number = std::clamp(ej[2].get<int>(), 0, 127);
                    e.value = std::clamp(
                        ej[3].get<int>(), midiControllerMinimum(e.type), midiControllerMaximum(e.type));
                    e.time = ej[4].get<SamplePosition>();
                    c.controllers.push_back(e);
                }
            c.sortNotes();
            c.sortControllers();
            t.midiClips.push_back(std::move(c));
        }
        t.sortMidiClips();
    }
    t.inputChannel = j.value("input", -1);
    t.inputBus = j.value("inputBus", std::string{});
    if (!t.inputBus.empty())
        t.inputChannel = -1;
    t.armed = j.value("armed", false);
    const auto monitor = j.value("monitor", std::string{"auto"});
    t.monitor = monitor == "off"  ? MonitorMode::off
                : monitor == "on" ? MonitorMode::on
                                  : MonitorMode::automatic;
    if (const auto it = j.find("clips"); it != j.end() && it->is_array())
        for (const auto& c : *it)
            t.clips.push_back(clipFromJson(c));
    t.sortClips();
    if (const auto it = j.find("takeGroups"); it != j.end() && it->is_array())
        for (const auto& gj : *it)
        {
            TakeGroup g;
            g.id = gj.value("id", invalidId);
            if (const auto takes = gj.find("takes"); takes != gj.end() && takes->is_array())
                for (const auto& tj : *takes)
                    g.takes.push_back(Take{clipFromJson(tj), tj.value("number", 1)});
            if (const auto comp = gj.find("comp"); comp != gj.end() && comp->is_array())
                for (const auto& sj : *comp)
                    if (sj.is_array() && sj.size() == 3)
                        g.comp.push_back(CompSegment{
                            sj[0].get<SamplePosition>(), sj[1].get<SamplePosition>(), sj[2].get<Id>()});
            if (const auto clips = gj.find("clips"); clips != gj.end() && clips->is_array())
                for (const auto& id : *clips)
                    if (id.is_number_unsigned() && t.findClip(id.get<Id>()) != nullptr)
                        g.clips.push_back(id.get<Id>());
            g.crossfade = std::max<SamplePosition>(0, gj.value("crossfade", SamplePosition{0}));
            g.expanded = gj.value("expanded", false);
            if (!g.takes.empty())
                t.takeGroups.push_back(std::move(g));
        }
    return t;
}
} // namespace

std::string projectToJson(const Project& p, bool pretty, const ParameterKeys* keys)
{
    struct Scope
    {
        explicit Scope(const ParameterKeys* k) { savingKeys = k; }
        ~Scope() { savingKeys = nullptr; }
    } scope(keys);
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

    json j{{"format", "northlight.project"},
           {"version", Project::schemaVersion},
           {"name", p.name},
           {"sampleRate", p.sampleRate},
           {"tempo", p.tempoBpm},
           {"timeSignature",
            json{{"numerator", p.timeSignature.numerator}, {"denominator", p.timeSignature.denominator}}},
           {"nextId", p.nextId},
           {"master", masterToJson(p.master)},
           {"buses", std::move(buses)},
           {"groups", std::move(groups)},
           {"markers", std::move(markers)},
           {"tracks", std::move(tracks)}};
    if (p.key.isSet())
        j["key"] = json{{"root", p.key.root}, {"scale", static_cast<int>(p.key.scale)}};
    if (!p.tempoChanges.empty())
    {
        json changes = json::array();
        for (const auto& c : p.tempoChanges)
        {
            json cj{{"quarter", c.quarter}, {"bpm", c.bpm}};
            if (c.ramp)
                cj["ramp"] = true;
            changes.push_back(std::move(cj));
        }
        j["tempoChanges"] = std::move(changes);
    }
    if (!p.meterChanges.empty())
    {
        json changes = json::array();
        for (const auto& m : p.meterChanges)
            changes.push_back(json{{"bar", m.bar},
                                   {"numerator", m.signature.numerator},
                                   {"denominator", m.signature.denominator}});
        j["meterChanges"] = std::move(changes);
    }
    return pretty ? j.dump(2) : j.dump();
}

const char* legacyTypePrefix() noexcept
{
    return "aural.";
}

const char* currentTypePrefix() noexcept
{
    return "northlight.";
}

std::string currentTypeName(std::string type)
{
    const std::string old = legacyTypePrefix();
    if (type.compare(0, old.size(), old) == 0)
        type = currentTypePrefix() + type.substr(old.size());
    return type;
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
    if (const auto format = j.value("format", std::string{});
        format != "northlight.project" && format != "aural.project")
        return fail("not a Northlight project file");
    const int version = j.value("version", 0);
    if (version < Project::oldestReadableVersion)
        return fail("made by an older version of Northlight (project version " + std::to_string(version) +
                    "), which this one no longer opens");
    if (version > Project::schemaVersion)
        return fail("made by a newer version of Northlight (project version " + std::to_string(version) +
                    ")");
    migrate(j, version);

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
        if (const auto it = j.find("tempoChanges"); it != j.end() && it->is_array())
            for (const auto& c : *it)
                if (c.is_object())
                    p.tempoChanges.push_back(TempoChange{c.value("quarter", 0.0),
                                                         std::clamp(c.value("bpm", 120.0), 1.0, 999.0),
                                                         c.value("ramp", false)});
        std::sort(p.tempoChanges.begin(),
                  p.tempoChanges.end(),
                  [](const TempoChange& a, const TempoChange& b) { return a.quarter < b.quarter; });
        if (const auto it = j.find("meterChanges"); it != j.end() && it->is_array())
            for (const auto& m : *it)
                if (m.is_object())
                    p.meterChanges.push_back(
                        MeterChange{m.value("bar", 0),
                                    TimeSignature{std::clamp(m.value("numerator", 4), 1, 32),
                                                  std::clamp(m.value("denominator", 4), 1, 32)}});
        std::sort(p.meterChanges.begin(),
                  p.meterChanges.end(),
                  [](const MeterChange& a, const MeterChange& b) { return a.bar < b.bar; });
        p.nextId = j.value("nextId", Id{1});
        if (const auto it = j.find("key"); it != j.end() && it->is_object())
        {
            p.key.root = std::clamp(it->value("root", -1), -1, 11);
            p.key.scale = static_cast<ScaleKind>(
                std::clamp(it->value("scale", 0), 0, static_cast<int>(ScaleKind::chromatic)));
        }
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
        {
            Master read;
            channelFromJson(*it, read);
            p.master.gain = read.gain;
            p.master.inserts = std::move(read.inserts);
        }

        // Guarantee ids stay unique even if the file's nextId is stale, and give a
        // duplicated clip or insert id (a hand-edited or corrupted file) a fresh one
        // rather than letting two objects share one processor or clip identity.
        Id maxId = 0;
        for (const auto& t : p.tracks)
        {
            maxId = std::max(maxId, t.id);
            for (const auto& c : t.clips)
                maxId = std::max(maxId, c.id);
            for (const auto& g : t.takeGroups)
            {
                maxId = std::max(maxId, g.id);
                for (const auto& take : g.takes)
                    maxId = std::max(maxId, take.id());
            }
            for (const auto& c : t.midiClips)
            {
                maxId = std::max(maxId, c.id);
                for (const auto& n : c.notes)
                    maxId = std::max(maxId, n.id);
                for (const auto& e : c.controllers)
                    maxId = std::max(maxId, e.id);
            }
        }
        for (const auto& i : p.master.inserts)
            maxId = std::max(maxId, i.id);
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
            if (t.hasInstrument())
                maxId = std::max(maxId, t.instrument.id);
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
        std::unordered_set<Id> seen; // a set: clips can hold many thousands of events
        const auto fresh = [&](Id& id)
        {
            if (id == invalidId || seen.contains(id))
                id = p.allocateId();
            seen.insert(id);
        };
        for (auto& t : p.tracks)
        {
            fresh(t.id);
            for (auto& c : t.clips)
                fresh(c.id);
            for (auto& g : t.takeGroups)
            {
                fresh(g.id);
                for (auto& take : g.takes)
                {
                    const auto was = take.clip.id;
                    fresh(take.clip.id);
                    for (auto& s : g.comp)
                        s.take = s.take == was ? take.clip.id : s.take;
                }
                // A comp clip that had to change its id (a corrupt file) is no longer the group's.
                std::erase_if(g.clips, [&t](Id id) { return t.findClip(id) == nullptr; });
            }
            for (auto& c : t.midiClips)
            {
                fresh(c.id);
                for (auto& n : c.notes)
                    fresh(n.id);
                for (auto& e : c.controllers)
                    fresh(e.id);
            }
            if (t.hasInstrument())
                fresh(t.instrument.id);
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
        for (auto& i : p.master.inserts)
            fresh(i.id);
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

bool reconcileParameters(Project& project, const ParameterKeys& keys)
{
    bool changed = false;
    const auto reconcileChannel = [&](Channel& channel)
    {
        std::vector<Insert*> inserts;
        if (channel.hasInstrument())
            inserts.push_back(&channel.instrument);
        for (auto* list : {&channel.inserts, &channel.strip})
            for (auto& insert : *list)
                inserts.push_back(&insert);
        for (auto* insert : inserts)
        {
            const auto layout = keys.layout(*insert);
            if (layout.keys.empty() || layout.defaults.size() != layout.keys.size())
                continue; // not known yet: keep what the file said
            const auto indexOf = [&](const std::string& key) -> int
            {
                const auto it = std::find(layout.keys.begin(), layout.keys.end(), key);
                return it == layout.keys.end() ? -1 : static_cast<int>(it - layout.keys.begin());
            };
            const auto fileKeys = insert->paramKeys;
            if (!fileKeys.empty() || insert->params.size() != layout.keys.size())
            {
                std::vector<float> params = layout.defaults;
                for (size_t i = 0; i < insert->params.size(); ++i)
                {
                    // By key when the file had keys, else by position (older files).
                    const int to = fileKeys.empty() ? static_cast<int>(i) : indexOf(fileKeys[i]);
                    if (to >= 0 && static_cast<size_t>(to) < params.size())
                        params[static_cast<size_t>(to)] = insert->params[i];
                }
                insert->params = std::move(params);
                insert->paramKeys.clear();
                changed = true;
            }
            for (auto it = channel.automation.begin(); it != channel.automation.end();)
            {
                auto& lane = *it;
                if (lane.target != AutomationTarget::insertParameter || lane.insert != insert->id)
                {
                    ++it;
                    continue;
                }
                std::string key = lane.parameterKey;
                if (key.empty() && !fileKeys.empty() && lane.parameter >= 0 &&
                    static_cast<size_t>(lane.parameter) < fileKeys.size())
                    key = fileKeys[static_cast<size_t>(lane.parameter)];
                const int to = key.empty() ? lane.parameter : indexOf(key);
                if (to < 0 || static_cast<size_t>(to) >= layout.keys.size())
                {
                    it = channel.automation.erase(it); // its parameter is gone
                    changed = true;
                    continue;
                }
                if (lane.parameter != to || !lane.parameterKey.empty())
                    changed = true;
                lane.parameter = to;
                lane.parameterKey.clear();
                ++it;
            }
        }
    };
    for (auto& t : project.tracks)
        reconcileChannel(t);
    for (auto& b : project.buses)
        reconcileChannel(b);
    reconcileChannel(project.master);
    return changed;
}

} // namespace northlight::core
