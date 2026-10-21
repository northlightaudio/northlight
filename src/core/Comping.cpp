#include "core/Comping.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aural::core
{

namespace
{
Track& trackOrThrow(Project& p, Id id)
{
    auto* t = p.findTrack(id);
    if (t == nullptr)
        throw std::logic_error("command references a track that does not exist");
    return *t;
}

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
        c.sourceOffset = source.sourceOffset + (start - source.start);
        c.fadeIn = fade[k] > 0 ? fade[k] : (start == source.start ? source.fadeIn : 0);
        c.fadeOut = fade[k + 1] > 0 ? fade[k + 1] : (end == source.end() ? source.fadeOut : 0);
        c.fadeIn = std::min(c.fadeIn, c.length);
        c.fadeOut = std::min(c.fadeOut, c.length - c.fadeIn);
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
        if (pass.start < g.end() && pass.end() > g.start())
            return true;
    return false;
}

// ----- TakeGroupEdit ---------------------------------------------------------------------------

TakeGroupEdit::TakeGroupEdit(Id trackId, Id groupId) : trackId_(trackId), groupId_(groupId) {}

void TakeGroupEdit::apply(Project& p)
{
    auto& t = trackOrThrow(p, trackId_);
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
                return;
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
            return;
        groupId_ = g.id;
        if (result_ == Result::rebuild)
        {
            normaliseComp(g);
            added_ = compClips(g);
            // The ids of the clips this replaces (those still on the track) go to
            // the new ones first, so a rebuilt comp keeps its identities.
            std::vector<Id> reuse;
            if (before_)
                for (const auto id : before_->clips)
                    if (t.findClip(id) != nullptr)
                        reuse.push_back(id);
            size_t next = 0;
            g.clips.clear();
            for (auto& c : added_)
            {
                c.id = next < reuse.size() ? reuse[next++] : p.allocateId();
                g.clips.push_back(c.id);
            }
            after_ = std::move(g);
        }
    }
    if (result_ == Result::refuse)
        return;

    removed_.clear();
    if (before_ && result_ != Result::flatten)
        for (const auto id : before_->clips)
            if (const auto* c = t.findClip(id))
            {
                removed_.push_back(*c);
                std::erase_if(t.clips, [id](const AudioClip& clip) { return clip.id == id; });
            }
    std::erase_if(t.takeGroups, [this](const TakeGroup& g) { return g.id == groupId_; });
    if (after_)
        t.takeGroups.push_back(*after_);
    sortGroups(t);
    for (const auto& c : added_)
        t.clips.push_back(c);
    t.sortClips();
}

void TakeGroupEdit::revert(Project& p)
{
    if (result_ == Result::refuse)
        return;
    auto& t = trackOrThrow(p, trackId_);
    for (const auto& c : added_)
        std::erase_if(t.clips, [&c](const AudioClip& clip) { return clip.id == c.id; });
    std::erase_if(t.takeGroups, [this](const TakeGroup& g) { return g.id == groupId_; });
    if (before_)
        t.takeGroups.push_back(*before_);
    sortGroups(t);
    for (const auto& c : removed_)
        t.clips.push_back(c);
    t.sortClips();
}

// ----- the comp commands -----------------------------------------------------------------------

RecordTakes::RecordTakes(Id trackId, std::vector<AudioClip> passes)
    : TakeGroupEdit(trackId, invalidId), passes_(std::move(passes))
{
}

Id RecordTakes::chooseGroup(const Track& track) const
{
    if (passes_.empty())
        return invalidId;
    SamplePosition from = passes_.front().start, to = passes_.front().end();
    for (const auto& c : passes_)
    {
        from = std::min(from, c.start);
        to = std::max(to, c.end());
    }
    for (const auto& g : track.takeGroups)
        if (from < g.end() && to > g.start())
            return g.id;
    return invalidId;
}

TakeGroupEdit::Result RecordTakes::edit(Project& p, Track&, TakeGroup& g)
{
    int number = 0;
    for (const auto& t : g.takes)
        number = std::max(number, t.number);
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

void SetTakeLanesExpanded::apply(Project& p)
{
    if (auto* g = trackOrThrow(p, trackId_).findTakeGroup(groupId_))
    {
        previous_ = g->expanded;
        g->expanded = expanded_;
    }
}

void SetTakeLanesExpanded::revert(Project& p)
{
    if (auto* g = trackOrThrow(p, trackId_).findTakeGroup(groupId_))
        g->expanded = previous_;
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
    t.kind = TrackKind::audio;
    t.name = track->name + " " + take->clip.name;
    t.colour = track->colour;
    t.outputBus = track->outputBus;
    t.inputChannel = track->inputChannel;
    t.monitor = track->monitor;
    t.clips.push_back(take->clip);
    commands.push_back(std::make_unique<AddTrack>(std::move(t), project.trackIndex(trackId) + 1));
    commands.push_back(std::make_unique<RemoveTake>(trackId, groupId, takeId));
    return commands;
}

} // namespace aural::core
