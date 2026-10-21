#include "core/Model.h"

#include <algorithm>
#include <cmath>

namespace aural::core
{

Send* Channel::findSend(Id busId) noexcept
{
    for (auto& s : sends)
        if (s.bus == busId)
            return &s;
    return nullptr;
}

const Send* Channel::findSend(Id busId) const noexcept
{
    for (const auto& s : sends)
        if (s.bus == busId)
            return &s;
    return nullptr;
}

const Insert* Channel::findInsert(Id insertId) const noexcept
{
    if (hasInstrument() && instrument.id == insertId)
        return &instrument;
    for (const auto& i : inserts)
        if (i.id == insertId)
            return &i;
    for (const auto& i : strip)
        if (i.id == insertId)
            return &i;
    return nullptr;
}

Insert* Channel::findInsert(Id insertId) noexcept
{
    if (hasInstrument() && instrument.id == insertId)
        return &instrument;
    for (auto& i : inserts)
        if (i.id == insertId)
            return &i;
    for (auto& i : strip)
        if (i.id == insertId)
            return &i;
    return nullptr;
}

int Channel::stripIndex(Id insertId) const noexcept
{
    for (size_t i = 0; i < strip.size(); ++i)
        if (strip[i].id == insertId)
            return static_cast<int>(i);
    return -1;
}

AutomationLane* Channel::findLane(Id laneId) noexcept
{
    for (auto& l : automation)
        if (l.id == laneId)
            return &l;
    return nullptr;
}

const AutomationLane* Channel::findLane(Id laneId) const noexcept
{
    for (const auto& l : automation)
        if (l.id == laneId)
            return &l;
    return nullptr;
}

const AutomationLane* Channel::findLane(AutomationTarget target, Id insertId, int parameter) const noexcept
{
    for (const auto& l : automation)
        if (l.sameTarget(target, insertId, parameter))
            return &l;
    return nullptr;
}

std::vector<const Insert*> Channel::processingChain() const
{
    std::vector<const Insert*> chain;
    chain.reserve(inserts.size() + strip.size() + 1);
    if (hasInstrument())
        chain.push_back(&instrument);
    const auto add = [&](const std::vector<Insert>& list)
    {
        for (const auto& i : list)
            chain.push_back(&i);
    };
    if (stripPreInserts)
        add(strip);
    add(inserts);
    if (!stripPreInserts)
        add(strip);
    return chain;
}

int Channel::insertIndex(Id insertId) const noexcept
{
    for (size_t i = 0; i < inserts.size(); ++i)
        if (inserts[i].id == insertId)
            return static_cast<int>(i);
    return -1;
}

std::vector<const Track*> Project::midiFeeds(Id instrumentChannel) const
{
    std::vector<const Track*> feeds;
    for (const auto& t : tracks)
        if (t.isMidi() && t.midiTarget == instrumentChannel && instrumentChannel != invalidId)
            feeds.push_back(&t);
    return feeds;
}

AudioClip* Track::findClip(Id clipId) noexcept
{
    for (auto& c : clips)
        if (c.id == clipId)
            return &c;
    return nullptr;
}

const AudioClip* Track::findClip(Id clipId) const noexcept
{
    for (const auto& c : clips)
        if (c.id == clipId)
            return &c;
    return nullptr;
}

void Track::sortClips()
{
    std::stable_sort(
        clips.begin(), clips.end(), [](const AudioClip& a, const AudioClip& b) { return a.start < b.start; });
}

SamplePosition TakeGroup::start() const noexcept
{
    if (takes.empty())
        return 0;
    SamplePosition s = takes.front().clip.start;
    for (const auto& t : takes)
        s = std::min(s, t.clip.start);
    return s;
}

SamplePosition TakeGroup::end() const noexcept
{
    SamplePosition e = 0;
    for (const auto& t : takes)
        e = std::max(e, t.clip.end());
    return e;
}

Take* TakeGroup::findTake(Id takeId) noexcept
{
    for (auto& t : takes)
        if (t.id() == takeId)
            return &t;
    return nullptr;
}

const Take* TakeGroup::findTake(Id takeId) const noexcept
{
    return const_cast<TakeGroup*>(this)->findTake(takeId);
}

int TakeGroup::takeIndex(Id takeId) const noexcept
{
    for (size_t i = 0; i < takes.size(); ++i)
        if (takes[i].id() == takeId)
            return static_cast<int>(i);
    return -1;
}

bool TakeGroup::ownsClip(Id clipId) const noexcept
{
    return std::find(clips.begin(), clips.end(), clipId) != clips.end();
}

TakeGroup* Track::findTakeGroup(Id groupId) noexcept
{
    for (auto& g : takeGroups)
        if (g.id == groupId)
            return &g;
    return nullptr;
}

const TakeGroup* Track::findTakeGroup(Id groupId) const noexcept
{
    return const_cast<Track*>(this)->findTakeGroup(groupId);
}

const TakeGroup* Track::takeGroupOwning(Id clipId) const noexcept
{
    for (const auto& g : takeGroups)
        if (g.ownsClip(clipId))
            return &g;
    return nullptr;
}

AudioClip* Track::findTakeClip(Id takeId) noexcept
{
    for (auto& g : takeGroups)
        if (auto* t = g.findTake(takeId))
            return &t->clip;
    return nullptr;
}

Track* Project::findTrack(Id trackId) noexcept
{
    for (auto& t : tracks)
        if (t.id == trackId)
            return &t;
    return nullptr;
}

const Track* Project::findTrack(Id trackId) const noexcept
{
    for (const auto& t : tracks)
        if (t.id == trackId)
            return &t;
    return nullptr;
}

int Project::trackIndex(Id trackId) const noexcept
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == trackId)
            return static_cast<int>(i);
    return -1;
}

Bus* Project::findBus(Id busId) noexcept
{
    for (auto& b : buses)
        if (b.id == busId)
            return &b;
    return nullptr;
}

const Bus* Project::findBus(Id busId) const noexcept
{
    for (const auto& b : buses)
        if (b.id == busId)
            return &b;
    return nullptr;
}

int Project::busIndex(Id busId) const noexcept
{
    for (size_t i = 0; i < buses.size(); ++i)
        if (buses[i].id == busId)
            return static_cast<int>(i);
    return -1;
}

Channel* Project::findChannel(Id channelId) noexcept
{
    if (auto* t = findTrack(channelId))
        return t;
    return findBus(channelId);
}

const Channel* Project::findChannel(Id channelId) const noexcept
{
    if (const auto* t = findTrack(channelId))
        return t;
    return findBus(channelId);
}

MidiNote* MidiClip::findNote(Id noteId) noexcept
{
    for (auto& n : notes)
        if (n.id == noteId)
            return &n;
    return nullptr;
}

const MidiNote* MidiClip::findNote(Id noteId) const noexcept
{
    for (const auto& n : notes)
        if (n.id == noteId)
            return &n;
    return nullptr;
}

void MidiClip::sortNotes()
{
    std::stable_sort(
        notes.begin(), notes.end(), [](const MidiNote& a, const MidiNote& b) { return a.start < b.start; });
}

MidiClip* Track::findMidiClip(Id clipId) noexcept
{
    for (auto& c : midiClips)
        if (c.id == clipId)
            return &c;
    return nullptr;
}

const MidiClip* Track::findMidiClip(Id clipId) const noexcept
{
    for (const auto& c : midiClips)
        if (c.id == clipId)
            return &c;
    return nullptr;
}

void Track::sortMidiClips()
{
    std::stable_sort(midiClips.begin(),
                     midiClips.end(),
                     [](const MidiClip& a, const MidiClip& b) { return a.start < b.start; });
}

Group* Project::findGroup(Id groupId) noexcept
{
    for (auto& g : groups)
        if (g.id == groupId)
            return &g;
    return nullptr;
}

const Group* Project::findGroup(Id groupId) const noexcept
{
    for (const auto& g : groups)
        if (g.id == groupId)
            return &g;
    return nullptr;
}

int Project::groupIndex(Id groupId) const noexcept
{
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i].id == groupId)
            return static_cast<int>(i);
    return -1;
}

std::vector<Channel*> Project::groupMembers(Id groupId) noexcept
{
    std::vector<Channel*> members;
    if (groupId == invalidId)
        return members;
    for (auto& t : tracks)
        if (t.group == groupId)
            members.push_back(&t);
    for (auto& b : buses)
        if (b.group == groupId)
            members.push_back(&b);
    return members;
}

Marker* Project::findMarker(Id markerId) noexcept
{
    for (auto& m : markers)
        if (m.id == markerId)
            return &m;
    return nullptr;
}

const Marker* Project::findMarker(Id markerId) const noexcept
{
    for (const auto& m : markers)
        if (m.id == markerId)
            return &m;
    return nullptr;
}

void Project::sortMarkers()
{
    std::stable_sort(markers.begin(),
                     markers.end(),
                     [](const Marker& a, const Marker& b) { return a.position < b.position; });
}

const Marker* Project::markerBefore(SamplePosition position) const noexcept
{
    const Marker* best = nullptr;
    for (const auto& m : markers)
        if (m.position < position && (best == nullptr || m.position > best->position))
            best = &m;
    return best;
}

const Marker* Project::markerAfter(SamplePosition position) const noexcept
{
    const Marker* best = nullptr;
    for (const auto& m : markers)
        if (m.position > position && (best == nullptr || m.position < best->position))
            best = &m;
    return best;
}

bool Project::busReaches(Id fromBus, Id toBus) const noexcept
{
    if (fromBus == invalidId || toBus == invalidId)
        return false;
    std::vector<Id> stack{fromBus};
    std::vector<Id> seen;
    while (!stack.empty())
    {
        const Id bus = stack.back();
        stack.pop_back();
        if (bus == toBus)
            return true;
        if (std::find(seen.begin(), seen.end(), bus) != seen.end())
            continue;
        seen.push_back(bus);
        const auto* b = findBus(bus);
        if (b == nullptr)
            continue;
        if (b->outputBus != invalidId)
            stack.push_back(b->outputBus);
        for (const auto& s : b->sends)
            stack.push_back(s.bus);
    }
    return false;
}

bool Project::channelReaches(Id fromChannel, Id toChannel) const noexcept
{
    if (fromChannel == invalidId || toChannel == invalidId)
        return false;
    std::vector<Id> stack{fromChannel};
    std::vector<Id> seen;
    while (!stack.empty())
    {
        const Id id = stack.back();
        stack.pop_back();
        if (id == toChannel)
            return true;
        if (std::find(seen.begin(), seen.end(), id) != seen.end())
            continue;
        seen.push_back(id);
        const auto* c = findChannel(id);
        if (c == nullptr)
            continue;
        if (c->outputBus != invalidId)
            stack.push_back(c->outputBus);
        for (const auto& s : c->sends)
            stack.push_back(s.bus);
        // Key edges: every channel with an insert keyed from this one hears it.
        const auto keyed = [&](const Channel& other)
        {
            for (const auto& i : other.inserts)
                if (i.sidechain == id)
                {
                    stack.push_back(other.id);
                    return;
                }
        };
        for (const auto& t : tracks)
            keyed(t);
        for (const auto& b : buses)
            keyed(b);
    }
    return false;
}

bool Project::keyWouldCycle(Id channelId, Id sourceId) const noexcept
{
    if (sourceId == invalidId)
        return false;
    return channelId == sourceId || channelReaches(channelId, sourceId);
}

bool Project::wouldCycle(Id channelId, Id busId) const noexcept
{
    if (busId == invalidId)
        return false;
    if (channelId == busId)
        return true;
    // Only a bus can be on a cycle: a track has no inputs.
    return findBus(channelId) != nullptr && busReaches(busId, channelId);
}

SamplePosition Project::length() const noexcept
{
    SamplePosition end = 0;
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            end = std::max(end, c.end());
    for (const auto& t : tracks)
        for (const auto& c : t.midiClips)
            end = std::max(end, c.end());
    return end;
}

double samplesPerBeat(double sampleRate, double tempoBpm, TimeSignature signature) noexcept
{
    // tempoBpm counts quarter notes; scale to the signature's beat value.
    const double quarterNotesPerBeat = 4.0 / static_cast<double>(std::max(1, signature.denominator));
    return sampleRate * 60.0 / std::max(1.0, tempoBpm) * quarterNotesPerBeat;
}

SamplePosition gridSamples(double sampleRate,
                           double tempoBpm,
                           TimeSignature signature,
                           double samplesPerPixel,
                           int minPixels) noexcept
{
    const double beat = samplesPerBeat(sampleRate, tempoBpm, signature);
    const double bar = beat * std::max(1, signature.numerator);
    const double minimum = std::max(1.0, samplesPerPixel * std::max(1, minPixels));
    for (const double division : {beat / 8.0, beat / 4.0, beat / 2.0, beat, bar})
        if (division >= minimum)
            return std::max<SamplePosition>(1, static_cast<SamplePosition>(std::llround(division)));
    // Even a bar is too fine at this zoom: whole multiples of bars.
    double division = bar;
    while (division < minimum)
        division *= 2.0;
    return std::max<SamplePosition>(1, static_cast<SamplePosition>(std::llround(division)));
}

SamplePosition snapToGrid(SamplePosition position, SamplePosition grid) noexcept
{
    if (grid <= 1)
        return position;
    const auto lower = (position / grid) * grid;
    const auto upper = lower + grid;
    return position - lower < upper - position ? lower : upper;
}

std::uint16_t MusicalKey::mask() const noexcept
{
    // Intervals above the root, as bits.
    std::uint16_t intervals = 0;
    const auto set = [&intervals](std::initializer_list<int> steps)
    {
        for (int s : steps)
            intervals = static_cast<std::uint16_t>(intervals | (1u << s));
    };
    switch (scale)
    {
        case ScaleKind::major:
            set({0, 2, 4, 5, 7, 9, 11});
            break;
        case ScaleKind::minor:
            set({0, 2, 3, 5, 7, 8, 10});
            break;
        case ScaleKind::harmonicMinor:
            set({0, 2, 3, 5, 7, 8, 11});
            break;
        case ScaleKind::dorian:
            set({0, 2, 3, 5, 7, 9, 10});
            break;
        case ScaleKind::mixolydian:
            set({0, 2, 4, 5, 7, 9, 10});
            break;
        case ScaleKind::majorPentatonic:
            set({0, 2, 4, 7, 9});
            break;
        case ScaleKind::minorPentatonic:
            set({0, 3, 5, 7, 10});
            break;
        case ScaleKind::chromatic:
            set({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
            break;
    }
    if (!isSet())
        return 0x0fff;
    // Rotate the intervals up to the root.
    const int r = root % 12;
    return static_cast<std::uint16_t>(((intervals << r) | (intervals >> (12 - r))) & 0x0fff);
}

const char* scaleName(ScaleKind scale) noexcept
{
    switch (scale)
    {
        case ScaleKind::major:
            return "major";
        case ScaleKind::minor:
            return "minor";
        case ScaleKind::harmonicMinor:
            return "harmonic minor";
        case ScaleKind::dorian:
            return "Dorian";
        case ScaleKind::mixolydian:
            return "Mixolydian";
        case ScaleKind::majorPentatonic:
            return "major pentatonic";
        case ScaleKind::minorPentatonic:
            return "minor pentatonic";
        case ScaleKind::chromatic:
            return "chromatic";
    }
    return "";
}

const char* pitchClassName(int pitchClass) noexcept
{
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[((pitchClass % 12) + 12) % 12];
}

BarsBeats samplesToBarsBeats(SamplePosition position,
                             double sampleRate,
                             double tempoBpm,
                             TimeSignature signature) noexcept
{
    const double spb = samplesPerBeat(sampleRate, tempoBpm, signature);
    const int beatsPerBar = std::max(1, signature.numerator);
    const double beatsTotal = static_cast<double>(std::max<SamplePosition>(0, position)) / spb;
    const auto wholeBeats = static_cast<std::int64_t>(std::floor(beatsTotal));
    BarsBeats out;
    out.bar = static_cast<int>(wholeBeats / beatsPerBar) + 1;
    out.beat = static_cast<int>(wholeBeats % beatsPerBar) + 1;
    out.tick = static_cast<int>(
        std::floor((beatsTotal - static_cast<double>(wholeBeats)) * BarsBeats::ticksPerBeat));
    return out;
}

SamplePosition
barsBeatsToSamples(BarsBeats position, double sampleRate, double tempoBpm, TimeSignature signature) noexcept
{
    const double spb = samplesPerBeat(sampleRate, tempoBpm, signature);
    const int beatsPerBar = std::max(1, signature.numerator);
    const double beats = static_cast<double>((position.bar - 1) * beatsPerBar + (position.beat - 1)) +
                         static_cast<double>(position.tick) / BarsBeats::ticksPerBeat;
    return static_cast<SamplePosition>(std::llround(beats * spb));
}

} // namespace aural::core
