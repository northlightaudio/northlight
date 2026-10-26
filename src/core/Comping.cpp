#include "core/Comping.h"

#include <algorithm>
#include <cmath>

namespace northlight::core
{

namespace
{
void sortGroups(Track& t)
{
    std::stable_sort(t.takeGroups.begin(),
                     t.takeGroups.end(),
                     [](const TakeGroup& a, const TakeGroup& b) { return a.start() < b.start(); });
}
} // namespace

SamplePosition defaultCompCrossfade(double sampleRate) noexcept
{
    return static_cast<SamplePosition>(std::llround(std::max(0.0, sampleRate) * 0.010));
}

std::vector<AudioClip> compClips(const TakeGroup& group)
{
    std::vector<AudioClip> out;
    const auto& segs = group.comp;
    const size_t n = segs.size();
    // The crossfade at each boundary k (between segments k - 1 and k), 0 for a butt splice.
    std::vector<SamplePosition> fade(n + 1, 0);
    for (size_t k = 1; k < n; ++k)
    {
        const auto* left = group.findTake(segs[k - 1].take);
        const auto* right = group.findTake(segs[k].take);
        if (left == nullptr || right == nullptr || left == right)
            continue;
        const auto x = std::min({group.crossfade, segs[k - 1].length() / 2, segs[k].length() / 2});
        if (x <= 0)
            continue;
        const auto from = segs[k].start - x / 2;
        if (right->clip.start > from || left->clip.end() < from + x)
            continue; // a take does not reach across the boundary
        fade[k] = x;
    }
    for (size_t k = 0; k < n; ++k)
    {
        const auto* take = group.findTake(segs[k].take);
        if (take == nullptr)
            continue;
        const auto& source = take->clip;
        auto start = segs[k].start - (fade[k] > 0 ? fade[k] / 2 : 0);
        auto end = segs[k].end + (fade[k + 1] > 0 ? fade[k + 1] - fade[k + 1] / 2 : 0);
        start = std::max(start, source.start);
        end = std::min(end, source.end());
        if (end <= start)
            continue;
        AudioClip c = source;
        c.id = invalidId;
        c.vocal = VocalEdit{};
        c.start = start;
        c.length = end - start;
        c.sourceOffset =
            source.sourceAt(start - source.start); // a stretched take reads its source at its speed
        c.fadeIn = fade[k] > 0 ? fade[k] : (start == source.start ? source.fadeIn : 0);
        c.fadeOut = fade[k + 1] > 0 ? fade[k + 1] : (end == source.end() ? source.fadeOut : 0);
        c.fadeIn = std::min(c.fadeIn, c.length);
        c.fadeOut = std::min(c.fadeOut, c.length - c.fadeIn);
        out.push_back(std::move(c));
    }
    return out;
}

std::vector<MidiClip> compMidiClips(const TakeGroup& group)
{
    std::vector<MidiClip> out;
    for (const auto& s : group.comp)
    {
        const auto* take = group.findTake(s.take);
        if (take == nullptr)
            continue;
        const auto& frame = take->clip;
        const auto start = std::max(s.start, frame.start);
        const auto end = std::min(s.end, frame.end());
        if (end <= start)
            continue;
        MidiClip c;
        c.name = frame.name;
        c.start = start;
        c.length = end - start;
        for (const auto& n : take->notes)
        {
            const auto at = frame.start + n.start;
            if (at < start || at >= end)
                continue;
            auto note = n;
            note.start = at - start;
            note.length = std::max<SamplePosition>(1, std::min(n.length, end - at));
            c.notes.push_back(note);
        }
        // Each controller's value at the segment's start, then its events in it.
        std::vector<MidiControllerEvent> chased;
        for (const auto& e : take->controllers)
        {
            const auto at = frame.start + e.time;
            if (at >= start)
                continue;
            auto it =
                std::find_if(chased.begin(),
                             chased.end(),
                             [&e](const MidiControllerEvent& x) { return x.sameLane(e.type, e.number); });
            if (it == chased.end())
                chased.push_back(e);
            else
                *it = e;
        }
        for (const auto& e : take->controllers)
        {
            const auto at = frame.start + e.time;
            if (at < start || at >= end)
                continue;
            auto event = e;
            event.time = at - start;
            c.controllers.push_back(event);
        }
        for (auto e : chased)
        {
            const bool opens = std::any_of(c.controllers.begin(),
                                           c.controllers.end(),
                                           [&e](const MidiControllerEvent& x)
                                           { return x.time == 0 && x.sameLane(e.type, e.number); });
            if (opens)
                continue;
            e.id = invalidId; // a copy: its own id
            e.time = 0;
            c.controllers.push_back(e);
        }
        c.sortNotes();
        c.sortControllers();
        out.push_back(std::move(c));
    }
    return out;
}

void normaliseComp(TakeGroup& group)
{
    auto& comp = group.comp;
    if (group.takes.empty())
    {
        comp.clear();
        return;
    }
    const auto from = group.start(), to = group.end();
    std::erase_if(comp, [&](const CompSegment& s) { return group.findTake(s.take) == nullptr; });
    for (auto& s : comp)
    {
        s.start = std::max(s.start, from);
        s.end = std::min(s.end, to);
    }
    std::erase_if(comp, [](const CompSegment& s) { return s.end <= s.start; });
    std::stable_sort(comp.begin(),
                     comp.end(),
                     [](const CompSegment& a, const CompSegment& b) { return a.start < b.start; });
    if (comp.empty())
    {
        comp.push_back(CompSegment{from, to, group.takes.back().id()});
        return;
    }
    std::vector<CompSegment> out;
    for (auto s : comp)
    {
        if (!out.empty())
        {
            auto& previous = out.back();
            if (s.start < previous.end) // an overlap: the later segment wins
            {
                previous.end = s.start;
                if (previous.end <= previous.start)
                {
                    out.pop_back();
                    if (!out.empty())
                        out.back().end = s.start;
                }
            }
            else if (s.start > previous.end) // a gap: the earlier segment extends
                previous.end = s.start;
        }
        if (!out.empty() && out.back().take == s.take)
            out.back().end = s.end;
        else
            out.push_back(s);
    }
    out.front().start = from;
    out.back().end = to;
    comp = std::move(out);
}

bool selectTakeForRange(TakeGroup& group, Id take, SamplePosition start, SamplePosition end)
{
    const auto* t = group.findTake(take);
    if (t == nullptr)
        return false;
    start = std::max(start, t->clip.start);
    end = std::min(end, t->clip.end());
    if (end <= start)
        return false;
    const auto before = group.comp;
    std::vector<CompSegment> next;
    for (const auto& s : group.comp)
    {
        if (s.end <= start || s.start >= end)
        {
            next.push_back(s);
            continue;
        }
        if (s.start < start)
            next.push_back(CompSegment{s.start, start, s.take});
        if (s.end > end)
            next.push_back(CompSegment{end, s.end, s.take});
    }
    next.push_back(CompSegment{start, end, take});
    group.comp = std::move(next);
    normaliseComp(group);
    return group.comp != before;
}

bool moveCompBoundary(TakeGroup& group, SamplePosition boundary, SamplePosition to)
{
    auto& comp = group.comp;
    for (size_t k = 1; k < comp.size(); ++k)
        if (comp[k].start == boundary)
        {
            to = std::clamp(to, comp[k - 1].start, comp[k].end);
            if (to == boundary)
                return false;
            comp[k - 1].end = to;
            comp[k].start = to;
            normaliseComp(group);
            return true;
        }
    return false;
}

const CompSegment* segmentAt(const TakeGroup& group, SamplePosition position) noexcept
{
    for (const auto& s : group.comp)
        if (position >= s.start && position < s.end)
            return &s;
    return nullptr;
}

const TakeGroup* takeGroupAt(const Track& track, SamplePosition position) noexcept
{
    for (const auto& g : track.takeGroups)
        if (position >= g.start() && position < g.end())
            return &g;
    return nullptr;
}

bool recordingMakesTakes(const Track& track, const std::vector<AudioClip>& passes, bool punch)
{
    if (passes.empty())
        return false;
    if (passes.size() >= 2 || punch)
        return true;
    const auto& pass = passes.front();
    for (const auto& g : track.takeGroups)
        if (!g.midi && pass.start < g.end() && pass.end() > g.start())
            return true;
    return false;
}

bool recordingMakesTakes(const Track& track, const std::vector<MidiClip>& passes, bool punch)
{
    if (passes.empty())
        return false;
    if (passes.size() >= 2 || punch)
        return true;
    const auto& pass = passes.front();
    for (const auto& g : track.takeGroups)
        if (g.midi && pass.start < g.end() && pass.end() > g.start())
            return true;
    return false;
}

// ----- TakeGroupEdit ---------------------------------------------------------------------------

TakeGroupEdit::TakeGroupEdit(Id trackId, Id groupId) : trackId_(trackId), groupId_(groupId) {}

bool TakeGroupEdit::apply(Project& p)
{
    auto* track = p.findTrack(trackId_);
    if (track == nullptr)
        return false;
    auto& t = *track;
    if (!computed_)
    {
        computed_ = true;
        if (groupId_ == invalidId)
            groupId_ = chooseGroup(t);
        TakeGroup g;
        if (groupId_ != invalidId)
        {
            const auto* found = t.findTakeGroup(groupId_);
            if (found == nullptr)
            {
                result_ = Result::refuse;
                return true;
            }
            before_ = *found;
            g = *found;
        }
        else
        {
            g.id = p.allocateId();
            g.crossfade = defaultCompCrossfade(p.sampleRate);
        }
        result_ = edit(p, t, g);
        if (result_ == Result::refuse)
            return true;
        groupId_ = g.id;
        if (result_ == Result::rebuild)
        {
            normaliseComp(g);
            // The ids of the clips this replaces (those still on the track) go to
            // the new ones first, so a rebuilt comp keeps its identities.
            std::vector<Id> reuse;
            if (before_)
                for (const auto id : before_->clips)
                    if (g.midi ? t.findMidiClip(id) != nullptr : t.findClip(id) != nullptr)
                        reuse.push_back(id);
            size_t next = 0;
            g.clips.clear();
            if (g.midi)
            {
                addedMidi_ = compMidiClips(g);
                for (auto& c : addedMidi_)
                {
                    c.id = next < reuse.size() ? reuse[next++] : p.allocateId();
                    // Copies of the take's events: ids of their own (ids are unique
                    // in a project).
                    for (auto& n : c.notes)
                        n.id = p.allocateId();
                    for (auto& e : c.controllers)
                        e.id = p.allocateId();
                    g.clips.push_back(c.id);
                }
            }
            else
            {
                added_ = compClips(g);
                for (auto& c : added_)
                {
                    c.id = next < reuse.size() ? reuse[next++] : p.allocateId();
                    g.clips.push_back(c.id);
                }
            }
            after_ = std::move(g);
        }
    }
    if (result_ == Result::refuse)
        return true;

    removed_.clear();
    removedMidi_.clear();
    if (before_ && result_ != Result::flatten)
        for (const auto id : before_->clips)
        {
            if (const auto* c = t.findClip(id); c != nullptr && !before_->midi)
            {
                removed_.push_back(*c);
                std::erase_if(t.clips, [id](const AudioClip& clip) { return clip.id == id; });
            }
            if (const auto* c = t.findMidiClip(id); c != nullptr && before_->midi)
            {
                removedMidi_.push_back(*c);
                std::erase_if(t.midiClips, [id](const MidiClip& clip) { return clip.id == id; });
            }
        }
    std::erase_if(t.takeGroups, [this](const TakeGroup& g) { return g.id == groupId_; });
    if (after_)
        t.takeGroups.push_back(*after_);
    sortGroups(t);
    for (const auto& c : added_)
        t.clips.push_back(c);
    t.sortClips();
    for (const auto& c : addedMidi_)
        t.midiClips.push_back(c);
    t.sortMidiClips();
    return true;
}

std::size_t TakeGroupEdit::footprint() const noexcept
{
    return sizeof(*this) + (before_ ? heapBytes(*before_) : 0) + (after_ ? heapBytes(*after_) : 0) +
           heapBytes(removed_) + heapBytes(added_) + heapBytes(removedMidi_) + heapBytes(addedMidi_);
}

bool TakeGroupEdit::revert(Project& p)
{
    if (result_ == Result::refuse)
        return true;
    auto* track = p.findTrack(trackId_);
    if (track == nullptr)
        return false;
    auto& t = *track;
    for (const auto& c : added_)
        std::erase_if(t.clips, [&c](const AudioClip& clip) { return clip.id == c.id; });
    for (const auto& c : addedMidi_)
        std::erase_if(t.midiClips, [&c](const MidiClip& clip) { return clip.id == c.id; });
    std::erase_if(t.takeGroups, [this](const TakeGroup& g) { return g.id == groupId_; });
    if (before_)
        t.takeGroups.push_back(*before_);
    sortGroups(t);
    for (const auto& c : removed_)
        t.clips.push_back(c);
    t.sortClips();
    for (const auto& c : removedMidi_)
        t.midiClips.push_back(c);
    t.sortMidiClips();
    return true;
}

// ----- the comp commands -----------------------------------------------------------------------

RecordTakes::RecordTakes(Id trackId, std::vector<AudioClip> passes)
    : TakeGroupEdit(trackId, invalidId), passes_(std::move(passes))
{
}

RecordTakes::RecordTakes(Id trackId, std::vector<MidiClip> passes)
    : TakeGroupEdit(trackId, invalidId), midiPasses_(std::move(passes)), midi_(true)
{
}

Id RecordTakes::chooseGroup(const Track& track) const
{
    // The range the passes cover.
    std::vector<std::pair<SamplePosition, SamplePosition>> ranges;
    for (const auto& c : passes_)
        ranges.emplace_back(c.start, c.end());
    for (const auto& c : midiPasses_)
        ranges.emplace_back(c.start, c.end());
    if (ranges.empty())
        return invalidId;
    SamplePosition from = ranges.front().first, to = ranges.front().second;
    for (const auto& [a, b] : ranges)
    {
        from = std::min(from, a);
        to = std::max(to, b);
    }
    for (const auto& g : track.takeGroups)
        if (g.midi == midi_ && from < g.end() && to > g.start())
            return g.id;
    return invalidId;
}

TakeGroupEdit::Result RecordTakes::edit(Project& p, Track&, TakeGroup& g)
{
    int number = 0;
    for (const auto& t : g.takes)
        number = std::max(number, t.number);
    if (g.takes.empty())
        g.midi = midi_;
    if (g.midi != midi_)
        return Result::refuse;
    std::vector<Id> added;
    for (auto clip : passes_)
    {
        if (clip.length <= 0)
            continue;
        clip.id = p.allocateId();
        Take take;
        take.number = ++number;
        clip.name = "Take " + std::to_string(take.number);
        take.clip = std::move(clip);
        added.push_back(take.id());
        g.takes.push_back(std::move(take));
    }
    for (auto& pass : midiPasses_)
    {
        if (pass.length <= 0)
            continue;
        Take take;
        take.number = ++number;
        take.clip.id = p.allocateId();
        take.clip.name = "Take " + std::to_string(take.number);
        take.clip.start = pass.start;
        take.clip.length = pass.length;
        take.notes = pass.notes;
        take.controllers = pass.controllers;
        for (auto& n : take.notes)
            if (n.id == invalidId)
                n.id = p.allocateId();
        for (auto& e : take.controllers)
            if (e.id == invalidId)
                e.id = p.allocateId();
        added.push_back(take.id());
        g.takes.push_back(std::move(take));
    }
    if (added.empty())
        return Result::refuse;
    // The comp follows the new range first, then each newer pass plays over its own.
    normaliseComp(g);
    for (const auto id : added)
    {
        const auto& clip = g.findTake(id)->clip;
        selectTakeForRange(g, id, clip.start, clip.end());
    }
    return Result::rebuild;
}

SelectTake::SelectTake(Id trackId, Id groupId, Id takeId, SamplePosition start, SamplePosition end)
    : TakeGroupEdit(trackId, groupId), takeId_(takeId), start_(start), end_(end)
{
}

TakeGroupEdit::Result SelectTake::edit(Project&, Track&, TakeGroup& g)
{
    const auto* take = g.findTake(takeId_);
    if (take == nullptr)
        return Result::refuse;
    const auto from = start_ < end_ ? start_ : take->clip.start;
    const auto to = start_ < end_ ? end_ : take->clip.end();
    return selectTakeForRange(g, takeId_, from, to) ? Result::rebuild : Result::refuse;
}

RemoveTake::RemoveTake(Id trackId, Id groupId, Id takeId) : TakeGroupEdit(trackId, groupId), takeId_(takeId)
{
}

TakeGroupEdit::Result RemoveTake::edit(Project&, Track&, TakeGroup& g)
{
    const int index = g.takeIndex(takeId_);
    if (index < 0)
        return Result::refuse;
    g.takes.erase(g.takes.begin() + index);
    if (g.takes.empty())
        return Result::remove;
    const auto newest = g.takes.back().id();
    for (auto& s : g.comp)
        if (s.take == takeId_)
            s.take = newest;
    return Result::rebuild;
}

MoveCompBoundary::MoveCompBoundary(Id trackId, Id groupId, SamplePosition boundary, SamplePosition to)
    : TakeGroupEdit(trackId, groupId), boundary_(boundary), to_(to)
{
}

TakeGroupEdit::Result MoveCompBoundary::edit(Project&, Track&, TakeGroup& g)
{
    return moveCompBoundary(g, boundary_, to_) ? Result::rebuild : Result::refuse;
}

FlattenComp::FlattenComp(Id trackId, Id groupId) : TakeGroupEdit(trackId, groupId) {}

TakeGroupEdit::Result FlattenComp::edit(Project&, Track&, TakeGroup&)
{
    return Result::flatten;
}

RenameTake::RenameTake(Id trackId, Id groupId, Id takeId, std::string name)
    : TakeGroupEdit(trackId, groupId), takeId_(takeId), name_(std::move(name))
{
}

TakeGroupEdit::Result RenameTake::edit(Project&, Track&, TakeGroup& g)
{
    auto* take = g.findTake(takeId_);
    if (take == nullptr || take->clip.name == name_)
        return Result::refuse;
    take->clip.name = name_;
    return Result::rebuild;
}

SetTakeLanesExpanded::SetTakeLanesExpanded(Id trackId, Id groupId, bool expanded)
    : trackId_(trackId), groupId_(groupId), expanded_(expanded)
{
}

bool SetTakeLanesExpanded::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    if (auto* g = t->findTakeGroup(groupId_))
    {
        previous_ = g->expanded;
        g->expanded = expanded_;
    }
    return true;
}

bool SetTakeLanesExpanded::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    if (auto* g = t->findTakeGroup(groupId_))
        g->expanded = previous_;
    return true;
}

RemoveTakeGroup::RemoveTakeGroup(Id trackId, Id groupId) : TakeGroupEdit(trackId, groupId) {}

TakeGroupEdit::Result RemoveTakeGroup::edit(Project&, Track&, TakeGroup&)
{
    return Result::remove;
}

MoveTakeGroup::MoveTakeGroup(Id fromTrackId, Id groupId, Id toTrackId, SamplePosition delta)
    : fromTrackId_(fromTrackId), groupId_(groupId), toTrackId_(toTrackId), delta_(delta)
{
}

bool MoveTakeGroup::move(Project& p, Id from, Id to, SamplePosition delta)
{
    auto* sourceTrack = p.findTrack(from);
    auto* targetTrack = p.findTrack(to);
    if (sourceTrack == nullptr || targetTrack == nullptr)
        return false;
    auto& source = *sourceTrack;
    auto& target = *targetTrack;
    auto* found = source.findTakeGroup(groupId_);
    if (found == nullptr)
        return true;
    TakeGroup g = std::move(*found);
    std::erase_if(source.takeGroups, [this](const TakeGroup& x) { return x.id == groupId_; });
    std::vector<AudioClip> clips;
    std::vector<MidiClip> midiClips;
    for (const auto id : g.clips)
    {
        if (const auto* c = source.findClip(id); c != nullptr && !g.midi)
        {
            clips.push_back(*c);
            std::erase_if(source.clips, [id](const AudioClip& clip) { return clip.id == id; });
        }
        if (const auto* c = source.findMidiClip(id); c != nullptr && g.midi)
        {
            midiClips.push_back(*c);
            std::erase_if(source.midiClips, [id](const MidiClip& clip) { return clip.id == id; });
        }
    }
    source.sortClips();
    source.sortMidiClips();
    for (auto& take : g.takes)
        take.clip.start += delta;
    for (auto& s : g.comp)
    {
        s.start += delta;
        s.end += delta;
    }
    for (auto& c : clips)
    {
        c.start += delta;
        target.clips.push_back(std::move(c));
    }
    for (auto& c : midiClips)
    {
        c.start += delta;
        target.midiClips.push_back(std::move(c));
    }
    target.takeGroups.push_back(std::move(g));
    sortGroups(target);
    target.sortClips();
    target.sortMidiClips();
    return true;
}

bool MoveTakeGroup::apply(Project& p)
{
    const auto* from = p.findTrack(fromTrackId_);
    const auto* to = p.findTrack(toTrackId_);
    if (from == nullptr || to == nullptr)
        return false;
    const auto* group = from->findTakeGroup(groupId_);
    moved_ = group != nullptr &&
             (group->midi ? to->isMidi() : to->hasAudio() && !to->isInstrument() && !to->isAux());
    if (!moved_)
        return true;
    // Pinned at zero: neither a take nor a comp clip may start before it.
    SamplePosition earliest = group->start();
    for (const auto& c : from->clips)
        if (group->ownsClip(c.id))
            earliest = std::min(earliest, c.start);
    for (const auto& c : from->midiClips)
        if (group->ownsClip(c.id))
            earliest = std::min(earliest, c.start);
    applied_ = std::max(delta_, -earliest);
    return move(p, fromTrackId_, toTrackId_, applied_);
}

bool MoveTakeGroup::revert(Project& p)
{
    return !moved_ || move(p, toTrackId_, fromTrackId_, -applied_);
}

std::vector<std::unique_ptr<Command>>
moveTakeToNewTrack(const Project& project, Id trackId, Id groupId, Id takeId)
{
    std::vector<std::unique_ptr<Command>> commands;
    const auto* track = project.findTrack(trackId);
    const auto* group = track != nullptr ? track->findTakeGroup(groupId) : nullptr;
    const auto* take = group != nullptr ? group->findTake(takeId) : nullptr;
    if (take == nullptr)
        return commands;
    Track t;
    t.name = track->name + " " + take->clip.name;
    t.colour = track->colour;
    t.monitor = track->monitor;
    if (group->midi)
    {
        // A MIDI track playing the same instrument, the take as its clip.
        t.kind = TrackKind::midi;
        t.midiTarget = track->midiTarget;
        t.midiChannel = track->midiChannel;
        t.midiInput = track->midiInput;
        t.midiRecordMode = track->midiRecordMode;
        MidiClip clip;
        clip.name = take->clip.name;
        clip.start = take->clip.start;
        clip.length = take->clip.length;
        clip.notes = take->notes;
        clip.controllers = take->controllers;
        t.midiClips.push_back(std::move(clip));
    }
    else
    {
        t.kind = TrackKind::audio;
        t.outputBus = track->outputBus;
        t.inputChannel = track->inputChannel;
        t.inputBus = track->inputBus;
        t.clips.push_back(take->clip);
    }
    commands.push_back(std::make_unique<AddTrack>(std::move(t), project.trackIndex(trackId) + 1));
    commands.push_back(std::make_unique<RemoveTake>(trackId, groupId, takeId));
    return commands;
}

// ----- MIDI merge and replace ------------------------------------------------------------------

namespace
{
SamplePosition overlap(const MidiClip& c, SamplePosition from, SamplePosition to) noexcept
{
    return std::max<SamplePosition>(0, std::min(c.end(), to) - std::max(c.start, from));
}

// One pass into the clips (see RecordMidiPasses).
void recordPass(Project& p, std::vector<MidiClip>& clips, MidiClip pass, bool replace)
{
    const auto from = pass.start, to = pass.end();
    MidiClip* target = nullptr;
    SamplePosition best = 0;
    for (auto& c : clips)
        if (const auto o = overlap(c, from, to); o > best)
        {
            best = o;
            target = &c;
        }
    for (auto& n : pass.notes)
        n.id = n.id == invalidId ? p.allocateId() : n.id;
    for (auto& e : pass.controllers)
        e.id = e.id == invalidId ? p.allocateId() : e.id;
    if (target == nullptr)
    {
        pass.id = p.allocateId();
        pass.sortNotes();
        pass.sortControllers();
        clips.push_back(std::move(pass));
        std::stable_sort(clips.begin(),
                         clips.end(),
                         [](const MidiClip& a, const MidiClip& b) { return a.start < b.start; });
        return;
    }
    auto& c = *target;
    // Grow the clip to hold the pass (its contents keep their timeline places).
    if (from < c.start)
    {
        const auto shift = c.start - from;
        for (auto& n : c.notes)
            n.start += shift;
        for (auto& e : c.controllers)
            e.time += shift;
        c.length += shift;
        c.start = from;
    }
    c.length = std::max(c.end(), to) - c.start;
    if (replace)
    {
        std::erase_if(c.notes,
                      [&](const MidiNote& n)
                      {
                          const auto at = c.start + n.start;
                          return at >= from && at < to;
                      });
        std::erase_if(c.controllers,
                      [&](const MidiControllerEvent& e)
                      {
                          const auto at = c.start + e.time;
                          if (at < from || at >= to)
                              return false;
                          return std::any_of(pass.controllers.begin(),
                                             pass.controllers.end(),
                                             [&e](const MidiControllerEvent& x)
                                             { return x.sameLane(e.type, e.number); });
                      });
    }
    for (auto n : pass.notes)
    {
        n.start += pass.start - c.start;
        c.notes.push_back(n);
    }
    for (auto e : pass.controllers)
    {
        e.time += pass.start - c.start;
        c.controllers.push_back(e);
    }
    c.sortNotes();
    c.sortControllers();
    std::stable_sort(
        clips.begin(), clips.end(), [](const MidiClip& a, const MidiClip& b) { return a.start < b.start; });
}
} // namespace

RecordMidiPasses::RecordMidiPasses(Id trackId, std::vector<MidiClip> passes, bool replace)
    : trackId_(trackId), passes_(std::move(passes)), replace_(replace)
{
}

bool RecordMidiPasses::apply(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr)
        return false;
    if (!after_)
    {
        before_ = t->midiClips;
        auto clips = t->midiClips;
        for (const auto& pass : passes_)
            recordPass(p, clips, pass, replace_);
        after_ = std::move(clips);
    }
    t->midiClips = *after_;
    return true;
}

bool RecordMidiPasses::revert(Project& p)
{
    auto* t = p.findTrack(trackId_);
    if (t == nullptr || !before_)
        return false;
    t->midiClips = *before_;
    return true;
}

std::size_t RecordMidiPasses::footprint() const noexcept
{
    return sizeof(*this) + heapBytes(passes_) + (before_ ? heapBytes(*before_) : 0) +
           (after_ ? heapBytes(*after_) : 0);
}

} // namespace northlight::core
