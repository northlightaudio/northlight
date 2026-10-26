#include "core/ArrangeCommands.h"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace northlight::core
{

namespace
{
bool listed(const std::vector<Id>& ids, Id id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
} // namespace

// ----- placement --------------------------------------------------------------------------

int spanEnd(const Project& project, Id track)
{
    const int index = project.trackIndex(track);
    if (index < 0)
        return -1;
    int end = index + 1;
    const auto& t = project.tracks[static_cast<size_t>(index)];
    if (t.isFolder())
        while (end < static_cast<int>(project.tracks.size()) &&
               project.insideFolder(project.tracks[static_cast<size_t>(end)].id, t.id))
            ++end;
    return end;
}

TrackPlace placeAfter(const Project& project, Id selected)
{
    const auto* track = project.findTrack(selected);
    if (track == nullptr)
        return {};
    return {spanEnd(project, selected), track->isFolder() ? track->id : track->folder};
}

std::vector<Id> withFolderContents(const Project& project, const std::vector<Id>& tracks)
{
    std::vector<Id> out;
    for (const auto& t : project.tracks)
    {
        bool in = listed(tracks, t.id);
        for (const auto id : tracks)
            in = in || (id != t.id && project.insideFolder(t.id, id));
        if (in)
            out.push_back(t.id);
    }
    return out;
}

// ----- parts of clips ---------------------------------------------------------------------

std::optional<AudioClip> clipPart(const AudioClip& clip, SamplePosition from, SamplePosition to)
{
    const auto s = std::max(clip.start, from);
    const auto e = std::min(clip.end(), to);
    if (e <= s)
        return std::nullopt;
    AudioClip part = clip;
    part.start = s;
    part.length = e - s;
    part.sourceOffset = clip.sourceAt(s - clip.start);
    part.fadeIn = s == clip.start ? std::min(clip.fadeIn, part.length) : 0;
    part.fadeOut = e == clip.end() ? std::min(clip.fadeOut, part.length) : 0;
    if (part.fadeIn == 0)
        part.fadeInCurve = 0.0f;
    if (part.fadeOut == 0)
        part.fadeOutCurve = 0.0f;
    return part;
}

std::optional<MidiClip> midiClipPart(const MidiClip& clip, SamplePosition from, SamplePosition to)
{
    const auto s = std::max(clip.start, from);
    const auto e = std::min(clip.end(), to);
    if (e <= s)
        return std::nullopt;
    MidiClip part = clip;
    part.start = s;
    part.length = e - s;
    part.notes.clear();
    part.controllers.clear();
    for (const auto& n : clip.notes)
    {
        const auto at = clip.start + n.start;
        if (at < s || at >= e)
            continue;
        auto note = n;
        note.start = at - s;
        note.length = std::max<SamplePosition>(1, std::min(n.length, e - at));
        part.notes.push_back(note);
    }
    // Each controller's value at the cut opens the part (unless an event sits there).
    std::vector<MidiControllerEvent> chase;
    for (const auto& c : clip.controllers)
    {
        const auto at = clip.start + c.time;
        if (at < s)
        {
            auto it =
                std::find_if(chase.begin(),
                             chase.end(),
                             [&](const MidiControllerEvent& x) { return x.sameLane(c.type, c.number); });
            if (it == chase.end())
                chase.push_back(c);
            else
                *it = c;
            continue;
        }
        if (at >= e)
            continue;
        auto event = c;
        event.time = at - s;
        part.controllers.push_back(event);
    }
    for (auto c : chase)
    {
        const bool atStart = std::any_of(part.controllers.begin(),
                                         part.controllers.end(),
                                         [&](const MidiControllerEvent& x)
                                         { return x.time == 0 && x.sameLane(c.type, c.number); });
        if (atStart)
            continue;
        c.id = invalidId;
        c.time = 0;
        part.controllers.push_back(c);
    }
    part.sortNotes();
    part.sortControllers();
    return part;
}

// ----- duplicate --------------------------------------------------------------------------

DuplicateTrack::DuplicateTrack(Id trackId) : trackId_(trackId) {}

bool DuplicateTrack::apply(Project& p)
{
    const auto* source = p.findTrack(trackId_);
    if (source == nullptr)
        return false;
    if (!made_)
    {
        copy_ = *source;
        std::unordered_map<Id, Id> ids;
        const auto renew = [&](Id& id)
        {
            if (id == invalidId)
                return;
            const auto fresh = p.allocateId();
            ids[id] = fresh;
            id = fresh;
        };
        renew(copy_.id);
        const auto renewAudio = [&](AudioClip& c)
        {
            renew(c.id);
            for (auto& n : c.vocal.notes)
                renew(n.id);
            for (auto& s : c.vocal.segments)
                renew(s.id);
        };
        for (auto& c : copy_.clips)
            renewAudio(c);
        for (auto& g : copy_.takeGroups)
        {
            renew(g.id);
            for (auto& take : g.takes)
                renewAudio(take.clip);
        }
        for (auto& c : copy_.midiClips)
        {
            renew(c.id);
            for (auto& n : c.notes)
                renew(n.id);
            for (auto& e : c.controllers)
                renew(e.id);
        }
        if (copy_.hasInstrument())
            renew(copy_.instrument.id);
        for (auto* list : {&copy_.inserts, &copy_.strip})
            for (auto& i : *list)
                renew(i.id);
        for (auto& l : copy_.automation)
            renew(l.id);
        // What pointed inside the track points inside the copy.
        const auto mapped = [&](Id id)
        {
            const auto it = ids.find(id);
            return it != ids.end() ? it->second : id;
        };
        for (auto& g : copy_.takeGroups)
        {
            for (auto& s : g.comp)
                s.take = mapped(s.take);
            for (auto& c : g.clips)
                c = mapped(c);
        }
        for (auto& l : copy_.automation)
            l.insert = mapped(l.insert);
        copy_.name = source->name + " copy";
        copy_.frozenMedia.clear();
        copy_.armed = false;
        made_ = true;
    }
    p.nextId = std::max(p.nextId, copy_.id + 1);
    const int at = std::clamp(spanEnd(p, trackId_), 0, static_cast<int>(p.tracks.size()));
    copy_.folder = p.findTrack(trackId_)->folder;
    p.tracks.insert(p.tracks.begin() + at, copy_);
    return true;
}

bool DuplicateTrack::revert(Project& p)
{
    const int i = p.trackIndex(copy_.id);
    if (i < 0)
        return false;
    p.tracks.erase(p.tracks.begin() + i);
    return true;
}

// ----- moving tracks and buses ------------------------------------------------------------

MoveTracks::MoveTracks(std::vector<Id> tracks, Id before, Id folder)
    : ids_(std::move(tracks)), before_(before), folder_(folder)
{
}

bool MoveTracks::apply(Project& p)
{
    if (ids_.empty() || listed(ids_, before_) || listed(ids_, folder_))
        return false;
    for (const auto id : ids_)
        if (p.findTrack(id) == nullptr || (folder_ != invalidId && p.insideFolder(folder_, id)))
            return false;
    if (before_ != invalidId && p.findTrack(before_) == nullptr)
        return false;
    if (folder_ != invalidId)
        if (const auto* f = p.findTrack(folder_); f == nullptr || !f->isFolder())
            return false;
    previousOrder_.clear();
    previousFolders_.clear();
    std::vector<Track> moved, rest;
    for (auto& t : p.tracks)
    {
        previousOrder_.push_back(t.id);
        (listed(ids_, t.id) ? moved : rest).push_back(std::move(t));
    }
    // Those not inside another moved folder take the new folder.
    for (auto& t : moved)
    {
        previousFolders_.emplace_back(t.id, t.folder);
        if (!listed(ids_, t.folder))
            t.folder = folder_;
    }
    size_t at = rest.size();
    for (size_t i = 0; i < rest.size(); ++i)
        if (rest[i].id == before_)
            at = i;
    rest.insert(rest.begin() + static_cast<std::ptrdiff_t>(at),
                std::make_move_iterator(moved.begin()),
                std::make_move_iterator(moved.end()));
    p.tracks = std::move(rest);
    return true;
}

bool MoveTracks::revert(Project& p)
{
    std::vector<Track> ordered;
    ordered.reserve(p.tracks.size());
    for (const auto id : previousOrder_)
        if (const int i = p.trackIndex(id); i >= 0)
            ordered.push_back(std::move(p.tracks[static_cast<size_t>(i)]));
    if (ordered.size() != p.tracks.size())
        return false; // (cannot happen in a linear history)
    p.tracks = std::move(ordered);
    for (const auto& [id, folder] : previousFolders_)
        if (auto* t = p.findTrack(id))
            t->folder = folder;
    return true;
}

MoveBus::MoveBus(Id busId, int newIndex) : busId_(busId), newIndex_(newIndex) {}

bool MoveBus::apply(Project& p)
{
    oldIndex_ = p.busIndex(busId_);
    if (oldIndex_ < 0)
        return false;
    newIndex_ = std::clamp(newIndex_, 0, static_cast<int>(p.buses.size()) - 1);
    Bus moved = std::move(p.buses[static_cast<size_t>(oldIndex_)]);
    p.buses.erase(p.buses.begin() + oldIndex_);
    p.buses.insert(p.buses.begin() + newIndex_, std::move(moved));
    return true;
}

bool MoveBus::revert(Project& p)
{
    if (newIndex_ < 0 || newIndex_ >= static_cast<int>(p.buses.size()))
        return false;
    Bus moved = std::move(p.buses[static_cast<size_t>(newIndex_)]);
    p.buses.erase(p.buses.begin() + newIndex_);
    p.buses.insert(p.buses.begin() + oldIndex_, std::move(moved));
    return true;
}

// ----- clip name and colour ---------------------------------------------------------------

SetClipName::SetClipName(Id trackId, Id clipId, bool midi, std::string name)
    : trackId_(trackId), clipId_(clipId), midi_(midi), value_(std::move(name))
{
}

std::string* SetClipName::field(Project& p) const
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return nullptr;
    if (midi_)
    {
        auto* c = t->findMidiClip(clipId_);
        return c != nullptr ? &c->name : nullptr;
    }
    auto* c = t->findClip(clipId_);
    return c != nullptr ? &c->name : nullptr;
}

bool SetClipName::apply(Project& p)
{
    auto* name = field(p);
    if (name == nullptr)
        return false;
    previous_ = *name;
    *name = value_;
    return true;
}

bool SetClipName::revert(Project& p)
{
    auto* name = field(p);
    if (name == nullptr)
        return false;
    *name = previous_;
    return true;
}

SetClipColour::SetClipColour(Id trackId, Id clipId, bool midi, std::uint32_t colour)
    : trackId_(trackId), clipId_(clipId), midi_(midi), value_(colour)
{
}

std::uint32_t* SetClipColour::field(Project& p) const
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return nullptr;
    if (midi_)
    {
        auto* c = t->findMidiClip(clipId_);
        return c != nullptr ? &c->colour : nullptr;
    }
    auto* c = t->findClip(clipId_);
    return c != nullptr ? &c->colour : nullptr;
}

bool SetClipColour::apply(Project& p)
{
    auto* colour = field(p);
    if (colour == nullptr)
        return false;
    previous_ = *colour;
    *colour = value_;
    return true;
}

bool SetClipColour::revert(Project& p)
{
    auto* colour = field(p);
    if (colour == nullptr)
        return false;
    *colour = previous_;
    return true;
}

// ----- removing a time range --------------------------------------------------------------

RemoveTimeRange::RemoveTimeRange(std::vector<Id> tracks,
                                 SamplePosition start,
                                 SamplePosition end,
                                 bool ripple,
                                 bool automation,
                                 std::string name)
    : tracks_(std::move(tracks)), start_(start), end_(end), ripple_(ripple), automation_(automation),
      name_(name.empty() ? (ripple ? "Ripple delete range" : "Delete range") : std::move(name))
{
}

RemoveTimeRange::State RemoveTimeRange::capture(const Track& t)
{
    return {t.id, t.clips, t.midiClips, t.takeGroups, t.automation};
}

void RemoveTimeRange::restore(Track& t, const State& s)
{
    t.clips = s.clips;
    t.midiClips = s.midiClips;
    t.takeGroups = s.takeGroups;
    t.automation = s.automation;
}

bool RemoveTimeRange::apply(Project& p)
{
    if (end_ <= start_ || tracks_.empty())
        return false;
    if (!after_.empty())
    {
        // Redo: put back what the first apply made.
        for (const auto& s : after_)
            if (auto* t = p.findTrack(s.track))
                restore(*t, s);
        p.nextId = std::max(p.nextId, nextIdAfter_);
        return true;
    }
    const auto length = end_ - start_;
    const auto shift = [&](SamplePosition s) { return ripple_ && s >= end_ ? s - length : s; };
    before_.clear();
    for (const auto id : tracks_)
    {
        auto* t = p.findTrack(id);
        if (t == nullptr)
            continue;
        before_.push_back(capture(*t));

        std::vector<AudioClip> clips;
        for (const auto& c : t->clips)
        {
            if (c.end() <= start_ || c.start >= end_)
            {
                auto kept = c;
                kept.start = shift(c.start);
                clips.push_back(kept);
                continue;
            }
            auto left = clipPart(c, c.start, start_);
            auto right = clipPart(c, end_, c.end());
            if (left)
                clips.push_back(*left);
            if (right)
            {
                if (left)
                {
                    right->id = p.allocateId();
                    for (auto& n : right->vocal.notes)
                        n.id = p.allocateId();
                    for (auto& s : right->vocal.segments)
                        s.id = p.allocateId();
                }
                right->start = shift(right->start);
                clips.push_back(*right);
            }
        }
        t->clips = std::move(clips);
        t->sortClips();

        std::vector<MidiClip> midi;
        for (const auto& c : t->midiClips)
        {
            if (c.end() <= start_ || c.start >= end_)
            {
                auto kept = c;
                kept.start = shift(c.start);
                midi.push_back(kept);
                continue;
            }
            auto left = midiClipPart(c, c.start, start_);
            auto right = midiClipPart(c, end_, c.end());
            if (left)
            {
                for (auto& e : left->controllers)
                    if (e.id == invalidId)
                        e.id = p.allocateId();
                midi.push_back(*left);
            }
            if (right)
            {
                if (left)
                    right->id = p.allocateId();
                for (auto& n : right->notes)
                    n.id = p.allocateId();
                for (auto& e : right->controllers)
                    e.id = p.allocateId();
                right->start = shift(right->start);
                midi.push_back(*right);
            }
        }
        t->midiClips = std::move(midi);
        t->sortMidiClips();

        if (ripple_)
            for (auto& g : t->takeGroups)
                if (g.start() >= end_)
                {
                    for (auto& take : g.takes)
                        take.clip.start -= length;
                    for (auto& s : g.comp)
                    {
                        s.start -= length;
                        s.end -= length;
                    }
                }

        if (automation_)
            for (auto& lane : t->automation)
            {
                if (lane.points.empty())
                    continue;
                const auto atStart = lane.valueAt(start_);
                const auto atEnd = lane.valueAt(end_);
                // [start, end): a point at the end stays (it is after the range).
                const bool before = lane.points.front().position < start_;
                const bool after = lane.points.back().position > end_;
                const bool atEndPoint =
                    std::any_of(lane.points.begin(),
                                lane.points.end(),
                                [&](const AutomationPoint& pt) { return pt.position == end_; });
                std::vector<AutomationPoint> points;
                for (const auto& pt : lane.points)
                    if (pt.position < start_)
                        points.push_back(pt);
                if (before)
                    points.push_back({start_, atStart});
                if (after && !atEndPoint)
                    points.push_back({shift(end_), atEnd});
                for (const auto& pt : lane.points)
                    if (pt.position >= end_)
                        points.push_back({shift(pt.position), pt.value});
                std::stable_sort(points.begin(),
                                 points.end(),
                                 [](const AutomationPoint& a, const AutomationPoint& b)
                                 { return a.position < b.position; });
                lane.points = std::move(points);
            }
    }
    if (before_.empty())
        return false;
    after_.clear();
    for (const auto& s : before_)
        if (const auto* t = p.findTrack(s.track))
            after_.push_back(capture(*t));
    nextIdAfter_ = p.nextId;
    return true;
}

bool RemoveTimeRange::revert(Project& p)
{
    for (const auto& s : before_)
        if (auto* t = p.findTrack(s.track))
            restore(*t, s);
    return true;
}

std::size_t RemoveTimeRange::footprint() const noexcept
{
    std::size_t bytes = sizeof(*this);
    for (const auto* list : {&before_, &after_})
        for (const auto& s : *list)
            bytes += sizeof(State) + heapBytes(s.clips) + heapBytes(s.midiClips) + heapBytes(s.takeGroups) +
                     heapBytes(s.automation);
    return bytes;
}

} // namespace northlight::core
