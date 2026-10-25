#include "ui/edit/ClipOps.h"

#include "core/Command.h"
#include "core/Comping.h"

#include <algorithm>

namespace northlight::ui::edit
{

std::optional<Span> spanOf(const core::Project& project, const ClipRef& ref)
{
    const auto* track = project.findTrack(ref.track);
    if (track == nullptr)
        return std::nullopt;
    if (ref.midi)
    {
        if (const auto* m = track->findMidiClip(ref.clip))
            return Span{m->start, m->length};
        return std::nullopt;
    }
    if (const auto* a = track->findClip(ref.clip))
        return Span{a->start, a->length};
    return std::nullopt;
}

bool takesClip(const core::Track& track, bool midi) noexcept
{
    return track.isMidi() == midi;
}

const core::Track* landingTrack(const core::Project& project, const ClipRef& ref, int rows)
{
    const int from = project.trackIndex(ref.track);
    if (from < 0 || project.tracks.empty())
        return nullptr;
    const int to = std::clamp(from + rows, 0, static_cast<int>(project.tracks.size()) - 1);
    const auto& target = project.tracks[static_cast<size_t>(to)];
    return takesClip(target, ref.midi) ? &target : nullptr;
}

bool contains(const Selection& selection, const ClipRef& ref)
{
    return std::find(selection.begin(), selection.end(), ref) != selection.end();
}

Edit deleteClips(const core::Project& project, const Selection& selection, bool ripple)
{
    Edit edit;
    edit.name = ripple ? "Ripple delete" : "Delete clips";
    for (const auto& t : project.tracks)
    {
        std::vector<const core::AudioClip*> doomed;
        for (const auto& c : t.clips)
            if (contains(selection, {t.id, c.id}))
                doomed.push_back(&c);
        std::sort(
            doomed.begin(), doomed.end(), [](const auto* a, const auto* b) { return a->start > b->start; });
        for (const auto* c : doomed)
        {
            edit.commands.push_back(std::make_unique<core::RemoveClip>(t.id, c->id));
            if (ripple)
                edit.commands.push_back(std::make_unique<core::ShiftClips>(t.id, c->end(), -c->length));
        }
        for (const auto& m : t.midiClips)
            if (contains(selection, {t.id, m.id, true}))
                edit.commands.push_back(std::make_unique<core::RemoveMidiClip>(t.id, m.id));
    }
    return edit;
}

Edit duplicateClips(const core::Project& project, const Selection& selection)
{
    Edit edit;
    edit.name = "Duplicate clips";
    for (const auto& ref : selection)
    {
        if (!spanOf(project, ref).has_value())
            continue;
        if (ref.midi)
            edit.commands.push_back(std::make_unique<core::DuplicateMidiClip>(ref.track, ref.clip));
        else
            edit.commands.push_back(std::make_unique<core::DuplicateClip>(ref.track, ref.clip));
    }
    return edit;
}

namespace
{
void splitTrackAt(Edit& edit, const core::Track& t, const Selection& selection, core::SamplePosition at)
{
    for (const auto& c : t.clips)
        if (at > c.start && at < c.end() && (selection.empty() || contains(selection, {t.id, c.id})))
            edit.commands.push_back(std::make_unique<core::SplitClip>(t.id, c.id, at));
    for (const auto& m : t.midiClips)
        if (at > m.start && at < m.end() && (selection.empty() || contains(selection, {t.id, m.id, true})))
            edit.commands.push_back(std::make_unique<core::SplitMidiClip>(t.id, m.id, at));
}
} // namespace

Edit splitAt(const core::Project& project, const Selection& selection, core::SamplePosition at)
{
    Edit edit;
    edit.name = "Split clips";
    for (const auto& t : project.tracks)
        splitTrackAt(edit, t, selection, at);
    return edit;
}

Edit splitAtRange(const core::Project& project,
                  const Selection& selection,
                  core::SamplePosition start,
                  core::SamplePosition end)
{
    Edit edit;
    edit.name = "Split at selection";
    if (end <= start)
        return edit;
    for (const auto& t : project.tracks)
        for (const auto at : {end, start}) // the later cut first so ids stay valid
            splitTrackAt(edit, t, selection, at);
    return edit;
}

Edit joinClips(const core::Project& project, const Selection& selection)
{
    Edit edit;
    edit.name = "Join clips";
    for (const auto& t : project.tracks)
    {
        const core::AudioClip* left = nullptr;
        core::AudioClip merged;       // what `left` will look like after the joins so far
        for (const auto& c : t.clips) // sorted by start
        {
            if (!contains(selection, {t.id, c.id}))
                continue;
            if (left != nullptr && core::JoinClips::canJoin(merged, c))
            {
                edit.commands.push_back(std::make_unique<core::JoinClips>(t.id, left->id, c.id));
                merged.length += c.length;
                merged.fadeOut = c.fadeOut;
                continue;
            }
            left = &c;
            merged = c;
        }
        const core::MidiClip* first = nullptr;
        for (const auto& m : t.midiClips) // sorted by start
        {
            if (!contains(selection, {t.id, m.id, true}))
                continue;
            if (first == nullptr)
                first = &m;
            else if (core::JoinMidiClips::canJoin(*first, m))
                edit.commands.push_back(std::make_unique<core::JoinMidiClips>(t.id, first->id, m.id));
        }
    }
    return edit;
}

std::optional<ClipRef> nextClip(const core::Project& project, const ClipRef& ref)
{
    const auto* track = project.findTrack(ref.track);
    if (track == nullptr)
        return std::nullopt;
    const auto after = [&](const auto& clips) -> std::optional<ClipRef>
    {
        for (size_t i = 0; i + 1 < clips.size(); ++i) // sorted by start
            if (clips[i].id == ref.clip)
                return ClipRef{ref.track, clips[i + 1].id, ref.midi};
        return std::nullopt;
    };
    return ref.midi ? after(track->midiClips) : after(track->clips);
}

bool canJoinWithNext(const core::Project& project, const ClipRef& ref)
{
    const auto next = nextClip(project, ref);
    const auto* track = project.findTrack(ref.track);
    if (!next || track == nullptr)
        return false;
    if (ref.midi)
    {
        const auto* left = track->findMidiClip(ref.clip);
        const auto* right = track->findMidiClip(next->clip);
        return left != nullptr && right != nullptr && core::JoinMidiClips::canJoin(*left, *right);
    }
    const auto* left = track->findClip(ref.clip);
    const auto* right = track->findClip(next->clip);
    return left != nullptr && right != nullptr && core::JoinClips::canJoin(*left, *right);
}

Edit joinWithNext(const core::Project& project, const ClipRef& ref)
{
    Edit edit;
    edit.name = "Join clips";
    if (!canJoinWithNext(project, ref))
        return edit;
    const auto next = *nextClip(project, ref);
    if (ref.midi)
        edit.commands.push_back(std::make_unique<core::JoinMidiClips>(ref.track, ref.clip, next.clip));
    else
        edit.commands.push_back(std::make_unique<core::JoinClips>(ref.track, ref.clip, next.clip));
    return edit;
}

bool isMuted(const core::Project& project, const ClipRef& ref)
{
    const auto* track = project.findTrack(ref.track);
    if (track == nullptr)
        return false;
    if (ref.midi)
    {
        const auto* m = track->findMidiClip(ref.clip);
        return m != nullptr && m->muted;
    }
    const auto* a = track->findClip(ref.clip);
    return a != nullptr && a->muted;
}

Edit setMuted(const core::Project& project, const Selection& selection, bool muted)
{
    Edit edit;
    edit.name = muted ? "Mute clips" : "Unmute clips";
    for (const auto& ref : selection)
        if (spanOf(project, ref).has_value() && isMuted(project, ref) != muted)
            edit.commands.push_back(
                std::make_unique<core::SetClipMute>(ref.track, ref.clip, ref.midi, muted));
    return edit;
}

Edit toggleMuted(const core::Project& project, const Selection& selection)
{
    const bool anyPlaying = std::any_of(
        selection.begin(),
        selection.end(),
        [&](const ClipRef& ref) { return spanOf(project, ref).has_value() && !isMuted(project, ref); });
    return setMuted(project, selection, anyPlaying);
}

Edit crossfadeClips(const core::Project& project, const Selection& selection)
{
    Edit edit;
    edit.name = "Crossfade clips";
    for (const auto& t : project.tracks)
    {
        const core::AudioClip* previous = nullptr;
        for (const auto& c : t.clips)
        {
            if (!contains(selection, {t.id, c.id}))
                continue;
            if (previous != nullptr && c.start > previous->start && c.start < previous->end())
                edit.commands.push_back(std::make_unique<core::CrossfadeClips>(t.id, previous->id, c.id));
            previous = &c;
        }
    }
    return edit;
}

Edit nudgeClips(const core::Project& project, const Selection& selection, core::SamplePosition step)
{
    Edit edit;
    edit.name = "Nudge clips";
    for (const auto& ref : selection)
        if (const auto span = spanOf(project, ref))
        {
            const auto start = std::max<core::SamplePosition>(0, span->start + step);
            if (ref.midi)
                edit.commands.push_back(
                    std::make_unique<core::MoveMidiClip>(ref.track, ref.clip, ref.track, start));
            else
                edit.commands.push_back(
                    std::make_unique<core::MoveClip>(ref.track, ref.clip, ref.track, start));
        }
    return edit;
}

Move moveClips(const core::Project& project, const Selection& selection, core::SamplePosition delta, int rows)
{
    Move move;
    if (delta == 0 && rows == 0)
        return move;
    std::vector<core::Id> movedGroups;
    for (const auto& ref : selection)
    {
        const int from = project.trackIndex(ref.track);
        const auto span = spanOf(project, ref);
        const auto* target = landingTrack(project, ref, rows);
        if (from < 0 || !span.has_value() || target == nullptr)
            continue;
        // A comp's clip moves its whole take group (takes, comp and clips) once.
        if (const auto* group =
                ref.midi ? nullptr : project.tracks[static_cast<size_t>(from)].takeGroupOwning(ref.clip))
        {
            if (std::find(movedGroups.begin(), movedGroups.end(), group->id) == movedGroups.end())
            {
                movedGroups.push_back(group->id);
                move.edit.commands.push_back(
                    std::make_unique<core::MoveTakeGroup>(ref.track, group->id, target->id, delta));
            }
            move.moved.push_back({target->id, ref.clip, false});
            continue;
        }
        const auto start = std::max<core::SamplePosition>(0, span->start + delta);
        if (ref.midi)
            move.edit.commands.push_back(
                std::make_unique<core::MoveMidiClip>(ref.track, ref.clip, target->id, start));
        else
            move.edit.commands.push_back(
                std::make_unique<core::MoveClip>(ref.track, ref.clip, target->id, start));
        move.moved.push_back({target->id, ref.clip, ref.midi});
    }
    move.edit.name = move.edit.commands.size() > 1 ? "Move clips" : "Move clip";
    return move;
}

namespace
{
const core::AudioClip* audioClip(const core::Project& project, const ClipRef& ref)
{
    const auto* track = project.findTrack(ref.track);
    return track != nullptr && !ref.midi ? track->findClip(ref.clip) : nullptr;
}
} // namespace

Edit trimClip(const core::Project& project,
              const ClipRef& ref,
              core::SamplePosition start,
              core::SamplePosition length,
              core::SamplePosition sourceOffset)
{
    Edit edit;
    edit.name = "Trim clip";
    if (!spanOf(project, ref).has_value())
        return edit;
    if (ref.midi)
        edit.commands.push_back(std::make_unique<core::TrimMidiClip>(ref.track, ref.clip, start, length));
    else
        edit.commands.push_back(
            std::make_unique<core::TrimClip>(ref.track, ref.clip, start, length, sourceOffset));
    return edit;
}

Edit stretchClip(const core::Project& project, const ClipRef& ref, core::SamplePosition length)
{
    Edit edit;
    edit.name = "Stretch clip";
    const auto* clip = audioClip(project, ref);
    if (clip == nullptr || length == clip->length)
        return edit;
    const double stretch = clip->stretch * static_cast<double>(length) /
                           static_cast<double>(std::max<core::SamplePosition>(1, clip->length));
    edit.commands.push_back(std::make_unique<core::SetClipStretch>(
        ref.track, ref.clip, stretch, clip->transpose, length, clip->followTempo));
    return edit;
}

Edit setFade(
    const core::Project& project, const ClipRef& ref, bool fadeIn, core::SamplePosition length, float curve)
{
    Edit edit;
    edit.name = "Set fade";
    const auto* clip = audioClip(project, ref);
    if (clip == nullptr)
        return edit;
    edit.commands.push_back(std::make_unique<core::SetClipFades>(ref.track,
                                                                 ref.clip,
                                                                 fadeIn ? length : clip->fadeIn,
                                                                 fadeIn ? clip->fadeOut : length,
                                                                 fadeIn ? curve : clip->fadeInCurve,
                                                                 fadeIn ? clip->fadeOutCurve : curve));
    return edit;
}

Edit setClipGain(const core::Project& project, const ClipRef& ref, float gain)
{
    Edit edit;
    edit.name = "Set clip gain";
    if (audioClip(project, ref) != nullptr)
        edit.commands.push_back(std::make_unique<core::SetClipGain>(ref.track, ref.clip, gain));
    return edit;
}

std::vector<ClipboardItem> copyClips(const core::Project& project, const Selection& selection)
{
    std::vector<ClipboardItem> items;
    int firstRow = -1;
    for (const auto& ref : selection)
        if (const int row = project.trackIndex(ref.track); row >= 0)
            firstRow = firstRow < 0 ? row : std::min(firstRow, row);
    for (const auto& ref : selection)
    {
        const int row = project.trackIndex(ref.track);
        if (row < 0)
            continue;
        const auto& track = project.tracks[static_cast<size_t>(row)];
        ClipboardItem item;
        item.midi = ref.midi;
        item.trackOffset = row - firstRow;
        if (ref.midi)
        {
            const auto* m = track.findMidiClip(ref.clip);
            if (m == nullptr)
                continue;
            item.midiClip = *m;
        }
        else
        {
            const auto* a = track.findClip(ref.clip);
            if (a == nullptr)
                continue;
            item.audio = *a;
        }
        items.push_back(std::move(item));
    }
    return items;
}

Edit pasteClips(const core::Project& project,
                const std::vector<ClipboardItem>& clipboard,
                int row,
                core::SamplePosition at)
{
    Edit edit;
    edit.name = "Paste clips";
    if (clipboard.empty() || project.tracks.empty())
        return edit;
    row = std::max(0, row);
    core::SamplePosition earliest = -1;
    for (const auto& item : clipboard)
    {
        const auto start = item.midi ? item.midiClip.start : item.audio.start;
        earliest = earliest < 0 ? start : std::min(earliest, start);
    }
    for (const auto& item : clipboard)
    {
        const int target = std::clamp(row + item.trackOffset, 0, static_cast<int>(project.tracks.size()) - 1);
        const auto& track = project.tracks[static_cast<size_t>(target)];
        if (!takesClip(track, item.midi))
            continue;
        if (item.midi)
        {
            auto clip = item.midiClip;
            clip.id = core::invalidId;
            for (auto& n : clip.notes)
                n.id = core::invalidId;
            for (auto& c : clip.controllers)
                c.id = core::invalidId;
            clip.start = at + (clip.start - earliest);
            edit.commands.push_back(std::make_unique<core::AddMidiClip>(track.id, clip));
        }
        else
        {
            auto clip = item.audio;
            clip.id = core::invalidId;
            clip.start = at + (clip.start - earliest);
            edit.commands.push_back(std::make_unique<core::AddClip>(track.id, clip));
        }
    }
    return edit;
}

} // namespace northlight::ui::edit
